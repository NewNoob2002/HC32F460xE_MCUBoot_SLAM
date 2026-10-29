use super::{
    BatteryCard, ConnectedDevice, DebugSettingRow, DebugValueRow, UpdaterWindow, WorkerCommand,
};
use hc32_updater::debug_link::{
    BatteryStatus, ChargerStatus, ConfigEntry, DebugClient, Error, Event, TelemetryValue, Tlv,
    ValueType, reset_cause_names,
};
use hc32_updater::nusb_transport::{DeviceMode, NusbTransport};
use slint::{ComponentHandle, Model, ModelRc, VecModel};
use std::cell::RefCell;
use std::rc::Rc;
use std::sync::mpsc::Sender;
use std::time::Duration;

const MAX_EVENTS_PER_POLL: usize = 64;
const TELEMETRY: u32 = 1;
const CONFIG: u32 = 1 << 2;
/// STREAM_SET mask: telemetry only. Log events are not requested; rendering them was the GUI hot spot.
const STREAM_TELEMETRY: u32 = 1;

pub enum Command {
    Open,
    Close,
    Refresh,
    Start { period: String },
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
    last_telemetry: Vec<Tlv>,
}

#[derive(Default)]
pub struct State {
    session: Option<Session>,
}

pub fn register(ui: &UpdaterWindow, sender: Sender<WorkerCommand>) {
    let pending = Rc::new(RefCell::new(None));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_open(move || send(&weak, &commands, Command::Open));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_refresh(move || send(&weak, &commands, Command::Refresh));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_start(move |period| {
        send(
            &weak,
            &commands,
            Command::Start {
                period: period.into(),
            },
        );
    });
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_stop(move || send(&weak, &commands, Command::Stop));
    let weak = ui.as_weak();
    let commands = sender.clone();
    ui.on_debug_close(move || send(&weak, &commands, Command::Close));

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
    ui.set_debug_status("Monitor request in progress…".into());
    if sender.send(WorkerCommand::Debug(command)).is_err() {
        ui.set_debug_busy(false);
        ui.set_debug_status("Monitor worker is unavailable".into());
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
            ui.set_debug_batteries(ModelRc::default());
            ui.set_debug_charger(ModelRc::default());
            ui.set_debug_settings(ModelRc::default());
            ui.set_debug_status(status.into());
        });
    }

    pub fn handle(
        &mut self,
        command: Command,
        connected: Option<&ConnectedDevice>,
        ui: &slint::Weak<UpdaterWindow>,
    ) {
        if matches!(command, Command::Close) {
            // close() stops an active stream and releases the Debug interface; the updater session stays open.
            self.close(ui, "Monitor closed");
            return;
        }
        let result = if matches!(command, Command::Open) {
            self.open(connected, ui)
        } else {
            self.session
                .as_mut()
                .ok_or_else(|| "Open the monitor first".to_owned())
                .and_then(|session| {
                    match command {
                        Command::Open | Command::Close => unreachable!(),
                        Command::Refresh => {
                            session.refresh(ui)?;
                            Ok("Status and settings refreshed".to_owned())
                        }
                        Command::Start { period } => {
                            let period = parse_period(&period)?;
                            if session.capabilities & TELEMETRY == 0 {
                                return Err(
                                    "Device does not support telemetry streaming".to_owned()
                                );
                            }
                            session
                                .client
                                .stream_set(period, STREAM_TELEMETRY)
                                .map_err(|error| error.to_string())?;
                            session.streaming = true;
                            Ok(format!("Live status every {period} ms"))
                        }
                        Command::Stop => {
                            session
                                .client
                                .stream_set(0, 0)
                                .map_err(|error| error.to_string())?;
                            session.streaming = false;
                            Ok("Live status stopped".to_owned())
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
                            Ok("All settings reset to defaults".to_owned())
                        }
                    }
                })
        };
        let streaming = self.streaming();
        let connected = self.session.is_some();
        let status = result.unwrap_or_else(|error| format!("Monitor error: {error}"));
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
            return Err("The monitor is already open".to_owned());
        }
        let device = connected.ok_or_else(|| "Connect an Application device first".to_owned())?;
        if device.mode != DeviceMode::Application {
            return Err("The monitor is available only in Application mode".to_owned());
        }
        let transport = NusbTransport::open_debug(&device.serial_number)
            .map_err(|error| error.to_string())?
            .ok_or_else(|| {
                "Application debug interface not found for this USB serial".to_owned()
            })?;
        let serial = transport
            .serial_number()
            .filter(|serial| !serial.is_empty())
            .ok_or_else(|| "The monitor requires a device with a USB serial number".to_owned())?
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
            last_telemetry: Vec::new(),
        };
        session.refresh(ui)?;
        let status = format!(
            "Debug Link {}.{} · USB {} · capabilities 0x{:08x} · telemetry v{}",
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
        let mut failure = None;
        // Drain everything queued and keep only the newest snapshot, so the UI thread gets at most one update per
        // worker tick however far the host fell behind.
        for _ in 0..MAX_EVENTS_PER_POLL {
            match session.client.poll_event(Duration::from_millis(1)) {
                Ok(Some(Event::Telemetry { fields, .. })) => telemetry = Some(fields),
                // Logs are never requested; tolerate them from a stream another host started.
                Ok(Some(Event::Log { .. })) => {}
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
        if let Some(fields) = telemetry.filter(|fields| *fields != session.last_telemetry) {
            session.last_telemetry = fields.clone();
            post_telemetry(ui, fields);
        }
        if let Some(error) = failure {
            self.close(ui, &format!("Debug stream failed: {error}"));
        }
    }
}

/// Splits one snapshot into the battery, charger and system cards.
fn post_telemetry(ui: &slint::Weak<UpdaterWindow>, fields: Vec<Tlv>) {
    let batteries = battery_cards(&fields);
    let (charger, system): (Vec<_>, Vec<_>) = fields
        .into_iter()
        .filter(|field| !matches!(field.value, TelemetryValue::Battery(_)))
        .partition(|field| CHARGER_TAGS.contains(&field.tag));
    let charger = charger.into_iter().flat_map(charger_rows).collect();
    let system = value_rows(system);
    let _ = ui.upgrade_in_event_loop(move |ui| {
        update_rows(&ui.get_debug_telemetry(), system, |model| {
            ui.set_debug_telemetry(model)
        });
        update_rows(&ui.get_debug_batteries(), batteries, |model| {
            ui.set_debug_batteries(model)
        });
        update_rows(&ui.get_debug_charger(), charger, |model| {
            ui.set_debug_charger(model)
        });
    });
}

/// Updates an existing row model in place so Slint only re-renders changed rows; a new model is created only when
/// the row count changes.
fn update_rows<T: Clone + PartialEq + 'static>(
    current: &ModelRc<T>,
    rows: Vec<T>,
    replace: impl FnOnce(ModelRc<T>),
) {
    match current.as_any().downcast_ref::<VecModel<T>>() {
        Some(model) if model.row_count() == rows.len() => {
            for (index, row) in rows.into_iter().enumerate() {
                if model.row_data(index).as_ref() != Some(&row) {
                    model.set_row_data(index, row);
                }
            }
        }
        _ => replace(ModelRc::new(VecModel::from(rows))),
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
        post_settings(ui, &self.settings);
        self.last_telemetry = telemetry.clone();
        post_telemetry(ui, telemetry);
        Ok(())
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

// Icon ids match Icons.by-id() in ui/app.slint.
const ICON_CLOCK: i32 = 1;
const ICON_LOOP: i32 = 2;
const ICON_CHIP: i32 = 3;
const ICON_BATTERY: i32 = 4;
const ICON_BOLT: i32 = 5;
const ICON_ALERT: i32 = 6;
const ICON_DOWNLOAD: i32 = 7;
const ICON_POWER: i32 = 8;
const ICON_CHART: i32 = 9;
const ICON_PLUG: i32 = 10;
const TONE_NEUTRAL: i32 = 0;
const TONE_GOOD: i32 = 1;
const TONE_WARNING: i32 = 2;

/// Power probe mask bits follow the REG_READ device order in DEBUG_LINK_V1.md.
const POWER_DEVICES: [&str; 4] = ["MP2762A", "BQ40Z50 0x0B", "BQ40Z50 0x0C", "HUSB238"];
const MANAGER_STATES: [&str; 10] = [
    "Idle",
    "Negotiating",
    "Preparing",
    "Receiving",
    "Verifying",
    "Ready to commit",
    "Committing",
    "Completed",
    "Aborted",
    "Error",
];

/// Battery tags 0x50 + slot are shown as pack cards, not generic rows.
const BATTERY_TAG: u8 = 0x50;
const BATTERY_ADDRESSES: [u8; 2] = [0x0b, 0x0c];

/// MP2762A registers plus its PG/INT# pins form the charger card.
const CHARGER_TAGS: [u8; 4] = [0x12, 0x20, 0x21, 0x22];

fn value_rows(fields: Vec<Tlv>) -> Vec<DebugValueRow> {
    fields.into_iter().map(value_row).collect()
}

fn charger_rows(field: Tlv) -> Vec<DebugValueRow> {
    let TelemetryValue::Charger(charger) = field.value else {
        return vec![value_row(field)];
    };
    charger_status_rows(&charger)
}

fn charger_status_rows(charger: &ChargerStatus) -> Vec<DebugValueRow> {
    let row = |name: &str, icon, tone, value: String| DebugValueRow {
        name: name.into(),
        value: value.into(),
        icon,
        tone,
    };
    let state = charger.charge_state();
    vec![
        row(
            "Charge state",
            ICON_BOLT,
            if state == "Not charging" {
                TONE_NEUTRAL
            } else {
                TONE_GOOD
            },
            state.to_owned(),
        ),
        row(
            "Input",
            ICON_PLUG,
            TONE_NEUTRAL,
            format!(
                "{:.2} V · {:.2} A · {:.2} W",
                charger.input_voltage_mv() / 1000.0,
                charger.input_current_ma() / 1000.0,
                charger.input_power_mw() / 1000.0
            ),
        ),
        row(
            "Charge current",
            ICON_BOLT,
            TONE_NEUTRAL,
            format!("{:.0} mA", charger.charge_current_ma()),
        ),
        row(
            "Battery voltage (charger)",
            ICON_BATTERY,
            TONE_NEUTRAL,
            format!("{:.2} V", charger.battery_voltage_mv() / 1000.0),
        ),
        row(
            "System voltage",
            ICON_POWER,
            TONE_NEUTRAL,
            format!("{:.2} V", charger.system_voltage_mv() / 1000.0),
        ),
        row(
            "Fault register",
            ICON_ALERT,
            if charger.fault == 0 {
                TONE_GOOD
            } else {
                TONE_WARNING
            },
            if charger.fault == 0 {
                "None".to_owned()
            } else {
                format!("0x{:02x}", charger.fault)
            },
        ),
    ]
}

/// Always one card per pack address so a missing pack stays visible as "Not detected".
fn battery_cards(fields: &[Tlv]) -> Vec<BatteryCard> {
    BATTERY_ADDRESSES
        .iter()
        .zip(BATTERY_TAG..)
        .map(|(address, tag)| {
            let title = format!("Pack 0x{address:02X}");
            match fields.iter().find_map(|field| match field.value {
                TelemetryValue::Battery(battery) if field.tag == tag => Some(battery),
                _ => None,
            }) {
                Some(battery) => battery_card(title, &battery),
                None => BatteryCard {
                    title: title.into(),
                    state: "Not detected".into(),
                    ..Default::default()
                },
            }
        })
        .collect()
}

fn battery_card(title: String, battery: &BatteryStatus) -> BatteryCard {
    let (remaining, full, unit) = battery.capacity();
    let alarms = battery.alarms();
    let state = battery.state();
    let tone = if !alarms.is_empty() {
        TONE_WARNING
    } else if state == "Charging" || state == "Full" {
        TONE_GOOD
    } else {
        TONE_NEUTRAL
    };
    BatteryCard {
        title: title.into(),
        present: true,
        state: state.into(),
        tone,
        soc: i32::from(battery.relative_soc_percent),
        capacity: format!("{remaining} / {full} {unit}").into(),
        voltage: format!("{:.2} V", f32::from(battery.voltage_mv) / 1000.0).into(),
        current: format!("{:+} mA", battery.current_ma).into(),
        power: format!("{:+.2} W", battery.power_mw() as f32 / 1000.0).into(),
        temperature: format!("{:.1} °C", battery.temperature_c()).into(),
        charge_target: format!(
            "{:.2} V / {} mA",
            f32::from(battery.charging_voltage_mv) / 1000.0,
            battery.charging_current_ma
        )
        .into(),
        cycles: battery.cycle_count.to_string().into(),
        alarms: alarms.join(", ").into(),
    }
}

fn value_row(field: Tlv) -> DebugValueRow {
    let number = match &field.value {
        TelemetryValue::U8(value) => Some(u32::from(*value)),
        TelemetryValue::U32(value) => Some(*value),
        _ => None,
    };
    let (name, icon, tone, value) = match (field.tag, number) {
        (0x01, Some(ms)) => ("Uptime", ICON_CLOCK, TONE_NEUTRAL, format_uptime(ms)),
        (0x02, Some(count)) => (
            "Main loop passes",
            ICON_LOOP,
            TONE_NEUTRAL,
            count.to_string(),
        ),
        (0x10, Some(mask)) => {
            let found = (0..POWER_DEVICES.len())
                .filter(|bit| mask & (1 << bit) != 0)
                .count();
            let missing: Vec<_> = POWER_DEVICES
                .iter()
                .enumerate()
                .filter(|(bit, _)| mask & (1 << bit) == 0)
                .map(|(_, name)| *name)
                .collect();
            let value = if missing.is_empty() {
                format!("{found}/{} found", POWER_DEVICES.len())
            } else {
                format!(
                    "{found}/{} · missing {}",
                    POWER_DEVICES.len(),
                    missing.join(", ")
                )
            };
            let tone = if missing.is_empty() {
                TONE_GOOD
            } else {
                TONE_WARNING
            };
            ("Power devices", ICON_CHIP, tone, value)
        }
        (0x20, Some(pg)) => (
            "Charger input",
            ICON_BOLT,
            if pg != 0 { TONE_GOOD } else { TONE_NEUTRAL },
            if pg != 0 { "Power good" } else { "No input" }.to_owned(),
        ),
        // The firmware already decodes the active-low INT# pin: 1 means asserted (DEBUG_LINK_V1.md).
        (0x21, Some(int)) => (
            "Charger interrupt",
            ICON_ALERT,
            if int != 0 { TONE_WARNING } else { TONE_NEUTRAL },
            if int != 0 { "Asserted" } else { "Idle" }.to_owned(),
        ),
        (0x22, Some(count)) => (
            "Charger changes",
            ICON_BOLT,
            TONE_NEUTRAL,
            count.to_string(),
        ),
        (0x30, Some(state)) => (
            "Update manager",
            ICON_DOWNLOAD,
            if state >= 8 {
                TONE_WARNING
            } else {
                TONE_NEUTRAL
            },
            MANAGER_STATES
                .get(state as usize)
                .map_or_else(|| state.to_string(), |name| (*name).to_owned()),
        ),
        (0x31, Some(errors)) => (
            "USB update errors",
            ICON_ALERT,
            if errors != 0 { TONE_WARNING } else { TONE_GOOD },
            errors.to_string(),
        ),
        (0x40, Some(dropped)) => (
            "Debug events dropped",
            ICON_CHART,
            if dropped != 0 {
                TONE_WARNING
            } else {
                TONE_NEUTRAL
            },
            dropped.to_string(),
        ),
        (0x41, Some(cause)) => {
            let names = reset_cause_names(cause);
            let unexpected = cause & 0x1e7c != 0; // brown-out, PVD, watchdogs and hardware faults
            (
                "Last reset cause",
                ICON_POWER,
                if unexpected {
                    TONE_WARNING
                } else {
                    TONE_NEUTRAL
                },
                if names.is_empty() {
                    "None recorded".to_owned()
                } else {
                    names.join(", ")
                },
            )
        }
        (0x11, _) => match &field.value {
            TelemetryValue::Identity(identity) => (
                "Battery pack identity",
                ICON_BATTERY,
                TONE_GOOD,
                format!(
                    "type 0x{:04x} · fw 0x{:04x} · hw 0x{:04x} · chem 0x{:04x}",
                    identity.device_type,
                    identity.firmware_version,
                    identity.hardware_version,
                    identity.chemistry_id
                ),
            ),
            other => (
                "Battery pack identity",
                ICON_BATTERY,
                TONE_NEUTRAL,
                other.to_string(),
            ),
        },
        _ => {
            return DebugValueRow {
                name: format!("Tag 0x{:02x}", field.tag).into(),
                value: field.value.to_string().into(),
                icon: 0,
                tone: TONE_NEUTRAL,
            };
        }
    };
    DebugValueRow {
        name: name.into(),
        value: value.into(),
        icon,
        tone,
    }
}

fn format_uptime(ms: u32) -> String {
    let seconds = ms / 1000;
    let (hours, minutes, seconds) = (seconds / 3600, seconds / 60 % 60, seconds % 60);
    if hours != 0 {
        format!("{hours}h {minutes:02}m {seconds:02}s")
    } else if minutes != 0 {
        format!("{minutes}m {seconds:02}s")
    } else {
        format!("{seconds}.{:01}s", ms % 1000 / 100)
    }
}

#[cfg(test)]
mod tests {
    use super::{
        battery_cards, charger_status_rows, format_uptime, parse_period, validate_setting,
        value_row,
    };
    use hc32_updater::debug_link::{
        BatteryStatus, ConfigEntry, TelemetryValue, Tlv, Unit, ValueType,
    };

    #[test]
    fn battery_cards_cover_both_packs() {
        let battery = BatteryStatus {
            battery_mode: 0,
            voltage_mv: 8000,
            current_ma: 1500,
            temperature_dk: 2982,
            relative_soc_percent: 64,
            remaining_capacity: 2000,
            full_charge_capacity: 3125,
            charging_voltage_mv: 8400,
            charging_current_ma: 2000,
            battery_status: 0,
            cycle_count: 3,
        };
        let cards = battery_cards(&[Tlv {
            tag: 0x51,
            value: TelemetryValue::Battery(battery),
        }]);
        assert_eq!(cards.len(), 2);
        assert!(!cards[0].present);
        assert_eq!(cards[0].state.as_str(), "Not detected");
        let pack = &cards[1];
        assert!(pack.present && pack.tone == 1 && pack.soc == 64);
        assert_eq!(pack.title.as_str(), "Pack 0x0C");
        assert_eq!(pack.capacity.as_str(), "2000 / 3125 mAh");
        assert_eq!(pack.voltage.as_str(), "8.00 V");
        assert_eq!(pack.current.as_str(), "+1500 mA");
        assert_eq!(pack.power.as_str(), "+12.00 W");
        // 0.1 K minus 273.15 always lands on a half-tenth, so compare numerically.
        let celsius: f32 = pack.temperature.trim_end_matches(" °C").parse().unwrap();
        assert!((celsius - 25.05).abs() <= 0.051);
        assert_eq!(pack.charge_target.as_str(), "8.40 V / 2000 mA");
    }

    #[test]
    fn telemetry_rows_are_readable() {
        let row = |tag, value| {
            value_row(Tlv {
                tag,
                value: TelemetryValue::U8(value),
            })
        };
        assert_eq!(
            row(0x10, 0b1011).value.as_str(),
            "3/4 · missing BQ40Z50 0x0C"
        );
        assert_eq!(row(0x10, 0b1111).tone, 1);
        assert_eq!(row(0x20, 1).value.as_str(), "Power good");
        assert_eq!(row(0x21, 1).value.as_str(), "Asserted");
        assert_eq!(row(0x21, 1).tone, 2);
        assert_eq!(row(0x21, 0).value.as_str(), "Idle");
        assert_eq!(row(0x30, 3).value.as_str(), "Receiving");
        assert_eq!(row(0x7f, 9).name.as_str(), "Tag 0x7f");
        let reset = value_row(Tlv {
            tag: 0x41,
            value: TelemetryValue::U32(0x0101),
        });
        assert_eq!(reset.value.as_str(), "Power-on, Software");
        assert_eq!(reset.tone, 0);
        let watchdog = value_row(Tlv {
            tag: 0x41,
            value: TelemetryValue::U32(0x0020),
        });
        assert_eq!(watchdog.tone, 2);
        let charger = charger_status_rows(&hc32_updater::debug_link::ChargerStatus {
            status: 0x0c,
            fault: 0x10,
            battery_voltage: 672 << 6,
            system_voltage: 0,
            charge_current: 0,
            input_voltage: 200 << 6,
            input_current: 160 << 6,
        });
        assert_eq!(charger[0].value.as_str(), "Charge done");
        assert_eq!(charger[1].value.as_str(), "5.00 V · 1.00 A · 5.00 W");
        assert_eq!(charger[3].value.as_str(), "8.40 V");
        assert_eq!((charger[5].value.as_str(), charger[5].tone), ("0x10", 2));
        assert_eq!(format_uptime(12_345), "12.3s");
        assert_eq!(format_uptime(125_000), "2m 05s");
        assert_eq!(format_uptime(3_723_000), "1h 02m 03s");
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
