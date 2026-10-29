use super::{
    ConnectedDevice, DebugLogRow, DebugSettingRow, DebugValueRow, UpdaterWindow, WorkerCommand,
};
use hc32_updater::debug_link::{ConfigEntry, DebugClient, Error, Event, Tlv, ValueType};
use hc32_updater::nusb_transport::{DeviceMode, NusbTransport};
use slint::{ComponentHandle, Model, ModelRc, VecModel};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::mpsc::Sender;
use std::time::Duration;

const LOG_LIMIT: usize = 500;
const MAX_EVENTS_PER_POLL: usize = 64;
const TELEMETRY: u32 = 1;
const LOGS: u32 = 1 << 1;
const CONFIG: u32 = 1 << 2;
const STATS: u32 = 1 << 5;

pub enum Command {
    Open,
    Refresh,
    Start { period: String, logs: bool },
    Stop,
    Apply { key: u16, value: i32 },
    Reset,
}

struct Session {
    client: DebugClient<NusbTransport>,
    serial: String,
    capabilities: u32,
    settings: Vec<ConfigEntry>,
    streaming: bool,
    skipped: u64,
}

#[derive(Default)]
pub struct State {
    session: Option<Session>,
}

pub fn register(ui: &UpdaterWindow, sender: Sender<WorkerCommand>) {
    let pending = Rc::new(RefCell::new(None));
    // poll() appends to this VecModel; Clear swaps in a new empty one.
    ui.set_debug_logs(ModelRc::new(VecModel::<DebugLogRow>::default()));
    let weak = ui.as_weak();
    ui.on_debug_clear_logs(move || {
        if let Some(ui) = weak.upgrade() {
            ui.set_debug_logs(ModelRc::new(VecModel::<DebugLogRow>::default()));
        }
    });
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_open(move || send(&weak, &commands, Command::Open));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_refresh(move || send(&weak, &commands, Command::Refresh));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_start(move |period, logs| {
        send(
            &weak,
            &commands,
            Command::Start {
                period: period.into(),
                logs,
            },
        );
    });
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_stop(move || send(&weak, &commands, Command::Stop));

    let weak = ui.as_weak();
    ui.on_debug_edit_setting(move |index, text| {
        let Some(ui) = weak.upgrade() else { return };
        if ui.get_busy()
            || ui.get_debug_busy()
            || ui.get_debug_confirm_visible()
            || !ui.get_debug_connected()
        {
            return;
        }
        let Ok(index) = usize::try_from(index) else {
            return;
        };
        let model = ui.get_debug_settings();
        let Some(mut row) = model.row_data(index) else {
            return;
        };
        row.edit_value = text;
        model.set_row_data(index, row);
    });
    let weak = ui.as_weak();
    let mutation = pending.clone();
    ui.on_debug_apply_setting(move |index| {
        let Some(ui) = weak.upgrade() else { return };
        if ui.get_busy() || ui.get_debug_busy() || ui.get_debug_confirm_visible() || !ui.get_debug_connected() {
            return;
        }
        let Some(row) = usize::try_from(index).ok().and_then(|index| ui.get_debug_settings().row_data(index)) else { return };
        let value = match row.edit_value.trim().parse::<i32>() {
            Ok(value) => value,
            Err(_) => {
                ui.set_debug_status("Enter a signed integer (boolean settings use 0 or 1)".into());
                return;
            }
        };
        let Ok(key) = u16::try_from(row.key) else { return };
        *mutation.borrow_mut() = Some(Command::Apply { key, value });
        ui.set_debug_confirm_text(format!("Store {} = {value} on this device? Current stored value: {}. This changes persistent settings.", row.name, row.current).into());
        ui.set_debug_confirm_visible(true);
    });
    let weak = ui.as_weak();
    let mutation = pending.clone();
    ui.on_debug_reset(move || {
        let Some(ui) = weak.upgrade() else { return };
        if ui.get_busy() || ui.get_debug_busy() || ui.get_debug_confirm_visible() || !ui.get_debug_connected() {
            return;
        }
        *mutation.borrow_mut() = Some(Command::Reset);
        ui.set_debug_confirm_text("Reset every debug setting on this device to its default? This replaces persistent settings.".into());
        ui.set_debug_confirm_visible(true);
    });
    let weak = ui.as_weak();
    ui.on_debug_confirm(move || {
        if let Some(command) = pending.borrow_mut().take() {
            send(&weak, &sender, command);
        }
    });
}

fn send(ui: &slint::Weak<UpdaterWindow>, sender: &Sender<WorkerCommand>, command: Command) {
    let Some(ui) = ui.upgrade() else { return };
    if ui.get_busy() || ui.get_debug_busy() || ui.get_debug_confirm_visible() {
        return;
    }
    ui.set_debug_busy(true);
    ui.set_debug_status("Debug request in progress…".into());
    if sender.send(WorkerCommand::Debug(command)).is_err() {
        ui.set_debug_busy(false);
        ui.set_debug_status("Debug worker is unavailable".into());
    }
}

impl State {
    pub fn streaming(&self) -> bool {
        self.session
            .as_ref()
            .is_some_and(|session| session.streaming)
    }

    pub fn close(&mut self, ui: &slint::Weak<UpdaterWindow>, reason: &str) {
        let error = self.session.take().and_then(|mut session| {
            if session.streaming {
                session.client.stream_set(0, 0).err()
            } else {
                None
            }
        });
        let status = match error {
            Some(error) => format!("{reason}; stopping stream failed: {error}"),
            None => reason.to_owned(),
        };
        let _ = ui.upgrade_in_event_loop(move |ui| {
            ui.set_debug_connected(false);
            ui.set_debug_busy(false);
            ui.set_debug_streaming(false);
            ui.set_debug_confirm_visible(false);
            ui.set_debug_telemetry(ModelRc::default());
            ui.set_debug_stats(ModelRc::default());
            ui.set_debug_settings(ModelRc::default());
            ui.set_debug_logs(ModelRc::new(VecModel::<DebugLogRow>::default()));
            ui.set_debug_status(status.into());
        });
    }

    pub fn handle(
        &mut self,
        command: Command,
        connected: Option<&ConnectedDevice>,
        ui: &slint::Weak<UpdaterWindow>,
    ) {
        let result = if matches!(command, Command::Open) {
            self.open(connected, ui)
        } else {
            self.session
                .as_mut()
                .ok_or_else(|| "Open Debug first".to_owned())
                .and_then(|session| {
                    match command {
                        Command::Open => unreachable!(),
                        Command::Refresh => {
                            session.refresh(ui)?;
                            Ok("Debug snapshot, settings and statistics refreshed".to_owned())
                        }
                        Command::Start { period, logs } => {
                            let period = parse_period(&period)?;
                            if session.capabilities & TELEMETRY == 0 {
                                return Err(
                                    "Device does not support telemetry streaming".to_owned()
                                );
                            }
                            if logs && session.capabilities & LOGS == 0 {
                                return Err(
                                    "Device does not support log streaming; switch Logs off"
                                        .to_owned(),
                                );
                            }
                            session
                                .client
                                .stream_set(period, if logs { 3 } else { 1 })
                                .map_err(|error| error.to_string())?;
                            session.streaming = true;
                            Ok(format!(
                                "Streaming every {period} ms{}",
                                if logs { " with logs" } else { "" }
                            ))
                        }
                        Command::Stop => {
                            session
                                .client
                                .stream_set(0, 0)
                                .map_err(|error| error.to_string())?;
                            session.streaming = false;
                            Ok("Debug stream stopped".to_owned())
                        }
                        Command::Apply { key, value } => {
                            let entry = session
                                .settings
                                .iter()
                                .find(|entry| entry.key == key)
                                .ok_or_else(|| {
                                    "Setting is no longer in the device table; refresh settings"
                                        .to_owned()
                                })?;
                            validate_setting(entry, value)?;
                            // Always unlock with the real USB serial, never the GUI product-config fallback.
                            session
                                .client
                                .unlock(&session.serial)
                                .map_err(|error| error.to_string())?;
                            let stored = session
                                .client
                                .config_set(key, value)
                                .map_err(|error| error.to_string())?;
                            if let Some(entry) =
                                session.settings.iter_mut().find(|entry| entry.key == key)
                            {
                                entry.current = stored;
                            }
                            post_settings(ui, &session.settings);
                            Ok(format!("Setting 0x{key:04x} stored as {stored}"))
                        }
                        Command::Reset => {
                            if session.capabilities & CONFIG == 0 {
                                return Err("Device does not support settings".to_owned());
                            }
                            session
                                .client
                                .unlock(&session.serial)
                                .map_err(|error| error.to_string())?;
                            session
                                .client
                                .config_reset()
                                .map_err(|error| error.to_string())?;
                            session.settings = session.client.config_list().map_err(|error| {
                                format!("Reset succeeded, but reloading settings failed: {error}")
                            })?;
                            post_settings(ui, &session.settings);
                            Ok("All debug settings reset to defaults".to_owned())
                        }
                    }
                })
        };
        let streaming = self.streaming();
        let connected = self.session.is_some();
        let status = result.unwrap_or_else(|error| format!("Debug error: {error}"));
        let _ = ui.upgrade_in_event_loop(move |ui| {
            ui.set_debug_busy(false);
            ui.set_debug_connected(connected);
            ui.set_debug_streaming(streaming);
            ui.set_debug_status(status.into());
        });
    }

    fn open(
        &mut self,
        connected: Option<&ConnectedDevice>,
        ui: &slint::Weak<UpdaterWindow>,
    ) -> Result<String, String> {
        if self.session.is_some() {
            return Err("Debug is already open".to_owned());
        }
        let device = connected.ok_or_else(|| "Connect an Application device first".to_owned())?;
        if device.mode != DeviceMode::Application {
            return Err("Debug is available only in Application mode".to_owned());
        }
        let transport = NusbTransport::open_debug(&device.serial_number)
            .map_err(|error| error.to_string())?
            .ok_or_else(|| {
                "Application debug interface not found for this USB serial".to_owned()
            })?;
        let serial = transport
            .serial_number()
            .filter(|serial| !serial.is_empty())
            .ok_or_else(|| "Debug requires a device with a USB serial number".to_owned())?
            .to_owned();
        let mut client = DebugClient::new(transport, super::REQUEST_TIMEOUT);
        let hello = client.hello().map_err(|error| error.to_string())?;
        let mut session = Session {
            client,
            serial,
            capabilities: hello.capabilities,
            settings: Vec::new(),
            streaming: false,
            skipped: 0,
        };
        session.refresh(ui)?;
        let status = format!(
            "Debug {}.{} · USB {} · capabilities 0x{:08x} · telemetry v{}",
            hello.major, hello.minor, session.serial, hello.capabilities, hello.telemetry_version
        );
        self.session = Some(session);
        Ok(status)
    }

    pub fn poll(&mut self, ui: &slint::Weak<UpdaterWindow>) {
        let Some(session) = self.session.as_mut().filter(|session| session.streaming) else {
            return;
        };
        let mut telemetry = None;
        let mut lines = Vec::new();
        let mut failure = None;
        // Drain everything already queued so log bursts do not fall behind the 20 ms worker tick.
        for _ in 0..MAX_EVENTS_PER_POLL {
            match session.client.poll_event(Duration::from_millis(1)) {
                Ok(Some(Event::Telemetry { fields, .. })) => telemetry = Some(value_rows(fields)),
                Ok(Some(Event::Log {
                    level,
                    uptime_ms,
                    text,
                    ..
                })) => lines.push(log_line(level, uptime_ms, &text)),
                Ok(None) => break,
                // A malformed frame is skipped; only a USB failure ends the session.
                Err(Error::Io(error)) => {
                    failure = Some(error.to_string());
                    break;
                }
                Err(error) => {
                    session.skipped += 1;
                    let status = format!(
                        "Skipped {} malformed debug frame(s); last: {error}",
                        session.skipped
                    );
                    let _ = ui.upgrade_in_event_loop(move |ui| ui.set_debug_status(status.into()));
                    break;
                }
            }
        }
        if telemetry.is_some() || !lines.is_empty() {
            let _ = ui.upgrade_in_event_loop(move |ui| {
                if let Some(rows) = telemetry {
                    ui.set_debug_telemetry(ModelRc::new(VecModel::from(rows)));
                }
                let model = ui.get_debug_logs();
                if let Some(logs) = model.as_any().downcast_ref::<VecModel<DebugLogRow>>() {
                    for line in lines {
                        if logs.row_count() >= LOG_LIMIT {
                            logs.remove(0);
                        }
                        logs.push(line);
                    }
                }
            });
        }
        if let Some(error) = failure {
            self.close(ui, &format!("Debug stream failed: {error}"));
        }
    }
}

impl Session {
    fn refresh(&mut self, ui: &slint::Weak<UpdaterWindow>) -> Result<(), String> {
        let telemetry = if self.capabilities & TELEMETRY != 0 {
            self.client.telemetry().map_err(|error| error.to_string())?
        } else {
            Vec::new()
        };
        if self.capabilities & CONFIG != 0 {
            self.settings = self
                .client
                .config_list()
                .map_err(|error| error.to_string())?;
        }
        let stats = if self.capabilities & STATS != 0 {
            self.client.stats().map_err(|error| error.to_string())?
        } else {
            Vec::new()
        };
        post_settings(ui, &self.settings);
        let telemetry = value_rows(telemetry);
        let stats = value_rows(stats);
        let _ = ui.upgrade_in_event_loop(move |ui| {
            ui.set_debug_telemetry(ModelRc::new(VecModel::from(telemetry)));
            ui.set_debug_stats(ModelRc::new(VecModel::from(stats)));
        });
        Ok(())
    }
}

/// Level numbers follow EasyLogger: 0 assert, 1 error, 2 warn, 3 info, 4 debug, 5 verbose.
fn log_line(level: u8, uptime_ms: u32, text: &str) -> DebugLogRow {
    let letter = ["A", "E", "W", "I", "D", "V"]
        .get(usize::from(level))
        .copied()
        .unwrap_or("?");
    DebugLogRow {
        time: format!("{:>9.3}", f64::from(uptime_ms) / 1000.0).into(),
        level: letter.into(),
        text: text.trim_end().into(),
    }
}

fn parse_period(text: &str) -> Result<u16, String> {
    let period = text
        .trim()
        .parse::<u16>()
        .map_err(|_| "Period must be an integer from 100 to 5000 ms".to_owned())?;
    if !(100..=5000).contains(&period) {
        return Err("Period must be from 100 to 5000 ms; use Stop to disable streaming".to_owned());
    }
    Ok(period)
}

fn validate_setting(entry: &ConfigEntry, value: i32) -> Result<(), String> {
    if value < entry.min
        || value > entry.max
        || (matches!(entry.value_type, ValueType::Bool) && value != 0 && value != 1)
    {
        return Err(format!(
            "{} must be from {} to {} {}",
            entry.name, entry.min, entry.max, entry.unit
        ));
    }
    Ok(())
}

fn post_settings(ui: &slint::Weak<UpdaterWindow>, settings: &[ConfigEntry]) {
    let rows: Vec<_> = settings
        .iter()
        .map(|entry| DebugSettingRow {
            key: i32::from(entry.key),
            name: entry.name.clone().into(),
            current: entry.current.to_string().into(),
            edit_value: entry.current.to_string().into(),
            detail: format!(
                "0x{:04x} · {}..{} {} · default {} · {} · {}",
                entry.key,
                entry.min,
                entry.max,
                entry.unit,
                entry.default,
                if entry.flags & 1 != 0 {
                    "persistent"
                } else {
                    "volatile"
                },
                if entry.flags & 4 != 0 {
                    "applied to hardware"
                } else {
                    "not applied to hardware"
                }
            )
            .into(),
        })
        .collect();
    let _ = ui
        .upgrade_in_event_loop(move |ui| ui.set_debug_settings(ModelRc::new(VecModel::from(rows))));
}

fn value_rows(fields: Vec<Tlv>) -> Vec<DebugValueRow> {
    fields
        .into_iter()
        .map(|field| DebugValueRow {
            name: match field.tag {
                0x01 => "Uptime (ms)".into(),
                0x02 => "Loop count".into(),
                0x10 => "Power probe mask".into(),
                0x11 => "BQ identity block".into(),
                0x20 => "Charger PG".into(),
                0x21 => "Charger INT#".into(),
                0x22 => "Charger change count".into(),
                0x30 => "Update manager state".into(),
                0x31 => "USB update errors".into(),
                0x40 => "Debug events dropped".into(),
                0x41 => "Last reset cause".into(),
                tag => format!("Tag 0x{tag:02x}").into(),
            },
            value: field.value.to_string().into(),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::{log_line, parse_period, validate_setting};
    use hc32_updater::debug_link::{ConfigEntry, Unit, ValueType};

    #[test]
    fn log_rows_use_easylogger_level_letters() {
        let row = log_line(1, 12_345, "E/core [12345] fault\r\n");
        assert_eq!(row.level.as_str(), "E");
        assert_eq!(row.time.as_str(), "   12.345");
        assert_eq!(row.text.as_str(), "E/core [12345] fault");
        assert_eq!(log_line(9, 0, "x").level.as_str(), "?");
    }

    #[test]
    fn stream_period_accepts_only_wire_range() {
        assert_eq!(parse_period("100").unwrap(), 100);
        assert_eq!(parse_period(" 5000 ").unwrap(), 5000);
        for invalid in ["0", "99", "5001", "65536", "-1", "100.5", "abc"] {
            assert!(parse_period(invalid).is_err(), "{invalid}");
        }
    }

    #[test]
    fn setting_validation_enforces_advertised_range_and_boolean_type() {
        let mut entry = ConfigEntry {
            key: 1,
            value_type: ValueType::I32,
            flags: 3,
            min: 100,
            max: 2000,
            default: 500,
            current: 500,
            unit: Unit::Milliamp,
            name: "charge_current_limit".to_owned(),
        };
        assert!(validate_setting(&entry, 100).is_ok());
        assert!(validate_setting(&entry, 2000).is_ok());
        assert!(validate_setting(&entry, 99).is_err());
        assert!(validate_setting(&entry, 2001).is_err());
        entry.value_type = ValueType::Bool;
        entry.min = -1;
        entry.max = 2;
        assert!(validate_setting(&entry, 0).is_ok());
        assert!(validate_setting(&entry, 1).is_ok());
        assert!(validate_setting(&entry, -1).is_err());
        assert!(validate_setting(&entry, 2).is_err());
    }
}
