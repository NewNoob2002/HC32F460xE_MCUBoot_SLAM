use hc32_updater::nusb_transport::{DeviceMode, NusbTransport, UpdaterDevice};
use hc32_updater::{
    DeviceInfo, FirmwareImage, HelloInfo, ProductConfig, ProgressEvent, ProtocolV1Client,
    UPGRADE_CAPABILITIES, UpgradeWorkflow,
};
use sha2::{Digest, Sha256};
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::thread;
use std::time::Duration;

const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const HEARTBEAT_INTERVAL: Duration = Duration::from_secs(2);
const WAIT_TIMEOUT: Duration = Duration::from_secs(30);
const WAIT_POLL: Duration = Duration::from_millis(250);
const BOOT_CONFIG_REQUIRED_WARNING: &str = "Device parameters are not configured. Open Device Setup in Boot Recovery and configure SN, Hardware Version, and App USB PID before upgrading.";
const APP_CONFIG_REQUIRED_WARNING: &str = "Device parameters are not configured. Reconnect the device in Boot Recovery, open Device Setup, and configure SN, Hardware Version, and App USB PID before upgrading.";

slint::include_modules!();

struct GuiDevice {
    info: DeviceInfo,
    mode: DeviceMode,
    product_config: Option<ProductConfig>,
}

struct ConnectedDevice {
    client: ProtocolV1Client<NusbTransport>,
    info: DeviceInfo,
    serial_number: String,
    mode: DeviceMode,
    product_config: Option<ProductConfig>,
}

impl ConnectedDevice {
    fn snapshot(&self) -> GuiDevice {
        GuiDevice {
            info: self.info.clone(),
            mode: self.mode,
            product_config: self.product_config.clone(),
        }
    }
}

struct GuiFirmware {
    version: String,
    sha256: String,
}

enum WorkerCommand {
    Refresh,
    Connect(i32),
    Disconnect,
    Configure {
        device_serial: String,
        hardware_version: String,
        application_pid: String,
    },
    Install(PathBuf),
}

fn main() -> Result<(), slint::PlatformError> {
    let ui = UpdaterWindow::new()?;
    let (commands, receiver) = mpsc::channel();
    let worker_ui = ui.as_weak();
    thread::spawn(move || worker_loop(receiver, worker_ui));

    let weak = ui.as_weak();
    let sender = commands.clone();
    ui.on_refresh_device(move || {
        if let Some(ui) = weak.upgrade() {
            ui.set_busy(true);
            ui.set_status_text("Enumerating devices...".into());
            let _ = sender.send(WorkerCommand::Refresh);
        }
    });

    let weak = ui.as_weak();
    let sender = commands.clone();
    ui.on_connect_device(move |index| {
        if let Some(ui) = weak.upgrade() {
            ui.set_busy(true);
            ui.set_status_text("Connecting...".into());
            let _ = sender.send(WorkerCommand::Connect(index));
        }
    });

    let weak = ui.as_weak();
    let sender = commands.clone();
    ui.on_disconnect_device(move || {
        if let Some(ui) = weak.upgrade() {
            ui.set_busy(true);
            ui.set_status_text("Disconnecting...".into());
            let _ = sender.send(WorkerCommand::Disconnect);
        }
    });

    let weak = ui.as_weak();
    let sender = commands.clone();
    ui.on_configure_device(move |device_serial, hardware_version, application_pid| {
        if let Some(ui) = weak.upgrade() {
            ui.set_busy(true);
            ui.set_status_text("Writing product configuration...".into());
            let _ = sender.send(WorkerCommand::Configure {
                device_serial: device_serial.into(),
                hardware_version: hardware_version.into(),
                application_pid: application_pid.into(),
            });
        }
    });

    let weak = ui.as_weak();
    ui.on_browse_firmware(move || {
        let Some(ui) = weak.upgrade() else {
            return;
        };
        if ui.get_busy() {
            return;
        }
        let Some(path) = rfd::FileDialog::new()
            .add_filter("MCUboot signed image", &["bin"])
            .pick_file()
        else {
            return;
        };
        match inspect_image(&path) {
            Ok(image) => {
                ui.set_firmware_path(path.to_string_lossy().into_owned().into());
                ui.set_firmware_version(image.version.into());
                ui.set_firmware_sha256(format!("✓ {}…", &image.sha256[..16]).into());
                ui.set_status_text("Firmware image ready".into());
            }
            Err(error) => {
                ui.set_firmware_version("Invalid image".into());
                ui.set_firmware_sha256("Failed".into());
                ui.set_status_text(format!("Image check failed: {error}").into());
            }
        }
    });

    let weak = ui.as_weak();
    ui.on_install_firmware(move |path| {
        let Some(ui) = weak.upgrade() else {
            return;
        };
        let path = path.trim();
        if path.is_empty() {
            ui.set_status_text("Choose a signed firmware image".into());
            return;
        }
        if !ui.get_device_provisioned() {
            ui.set_warning_text(BOOT_CONFIG_REQUIRED_WARNING.into());
            ui.set_warning_visible(true);
            ui.set_status_text("Upgrade blocked: device parameters are not configured".into());
            return;
        }
        ui.set_busy(true);
        ui.set_progress(0.0);
        ui.set_progress_label("0%".into());
        ui.set_status_text("Checking firmware image...".into());
        let _ = commands.send(WorkerCommand::Install(PathBuf::from(path)));
    });

    ui.run()
}

fn worker_loop(receiver: Receiver<WorkerCommand>, ui: slint::Weak<UpdaterWindow>) {
    let mut devices = Vec::new();
    let mut connected: Option<ConnectedDevice> = None;

    loop {
        let command = if connected.is_some() {
            match receiver.recv_timeout(HEARTBEAT_INTERVAL) {
                Ok(command) => Some(command),
                Err(RecvTimeoutError::Timeout) => {
                    if heartbeat(&mut connected, &ui).is_err() {
                        connected = None;
                    }
                    None
                }
                Err(RecvTimeoutError::Disconnected) => return,
            }
        } else {
            match receiver.recv() {
                Ok(command) => Some(command),
                Err(_) => return,
            }
        };
        let Some(command) = command else {
            continue;
        };

        match command {
            WorkerCommand::Refresh => match NusbTransport::enumerate() {
                Ok(found) => {
                    let labels = found.iter().map(UpdaterDevice::label).collect();
                    devices = found;
                    post_devices(&ui, labels);
                }
                Err(error) => post_error(&ui, format!("Refresh failed: {error}"), false),
            },
            WorkerCommand::Connect(index) => {
                let result = usize::try_from(index)
                    .ok()
                    .and_then(|index| devices.get(index))
                    .ok_or_else(|| "Select a device from the current list".to_owned())
                    .and_then(connect_device);
                match result {
                    Ok(device) => {
                        let snapshot = device.snapshot();
                        connected = Some(device);
                        post_connected(&ui, snapshot, "Connected", false);
                    }
                    Err(error) => post_error(&ui, format!("Connect failed: {error}"), false),
                }
            }
            WorkerCommand::Disconnect => {
                connected = None;
                post_disconnected(&ui, "Disconnected");
            }
            WorkerCommand::Configure {
                device_serial,
                hardware_version,
                application_pid,
            } => {
                let result = configure_device(
                    connected.as_mut(),
                    &device_serial,
                    &hardware_version,
                    &application_pid,
                );
                match result {
                    Ok(device) => post_connected(
                        &ui,
                        device,
                        "Product configuration saved; App PID applies after Application restart",
                        false,
                    ),
                    Err(error) => post_error(&ui, format!("Configuration failed: {error}"), true),
                }
            }
            WorkerCommand::Install(path) => {
                if let Some(message) = connected.as_ref().and_then(|device| {
                    upgrade_block_reason(device.mode, device.product_config.as_ref())
                }) {
                    post_warning(&ui, message);
                    continue;
                }
                let result = install_image(connected.take(), &path, &ui);
                match result {
                    Ok(device) => {
                        let snapshot = device.snapshot();
                        let version = device.info.application_version;
                        connected = Some(device);
                        post_connected(
                            &ui,
                            snapshot,
                            &format!("Upgrade complete: application {version}"),
                            true,
                        );
                    }
                    Err(error) => post_disconnected(&ui, &format!("Install failed: {error}")),
                }
            }
        }
    }
}

fn connect_device(device: &UpdaterDevice) -> Result<ConnectedDevice, String> {
    let transport = NusbTransport::open(device).map_err(|error| error.to_string())?;
    let mut client = ProtocolV1Client::new(transport, REQUEST_TIMEOUT);
    let hello = client.hello().map_err(|error| error.to_string())?;
    validate_hello(&hello)?;
    let info = client.info().map_err(|error| error.to_string())?;
    let product_config = client.product_config().ok();
    Ok(ConnectedDevice {
        client,
        info,
        serial_number: device.serial_number().to_owned(),
        mode: device.mode(),
        product_config,
    })
}

fn validate_hello(hello: &HelloInfo) -> Result<(), String> {
    if hello.capabilities & UPGRADE_CAPABILITIES != UPGRADE_CAPABILITIES {
        return Err(format!(
            "missing required capabilities: 0x{:08x}",
            hello.capabilities
        ));
    }
    if hello.session_timeout_ms <= HEARTBEAT_INTERVAL.as_millis() as u32 {
        return Err(format!(
            "device session timeout {} ms is too short for the 2000 ms heartbeat",
            hello.session_timeout_ms
        ));
    }
    Ok(())
}

fn heartbeat(
    connected: &mut Option<ConnectedDevice>,
    ui: &slint::Weak<UpdaterWindow>,
) -> Result<(), ()> {
    let Some(device) = connected.as_mut() else {
        return Ok(());
    };
    match device.client.info() {
        Ok(info) => {
            device.info = info.clone();
            let _ = ui.upgrade_in_event_loop(move |ui| set_device_info(&ui, &info));
            Ok(())
        }
        Err(error) => {
            post_disconnected(ui, &format!("Heartbeat failed: {error}"));
            Err(())
        }
    }
}

fn configure_device(
    connected: Option<&mut ConnectedDevice>,
    device_serial: &str,
    hardware_version: &str,
    application_pid: &str,
) -> Result<GuiDevice, String> {
    let device = connected.ok_or_else(|| "Device is not connected".to_owned())?;
    if device.mode != DeviceMode::BootRecovery {
        return Err("Product configuration is writable only in Boot recovery".to_owned());
    }
    if device
        .product_config
        .as_ref()
        .is_some_and(|config| config.provisioned)
    {
        return Err("Product configuration is already provisioned".to_owned());
    }
    let application_pid = parse_application_pid(application_pid)?;
    let config = device
        .client
        .provision_product_config(
            device_serial.trim(),
            hardware_version.trim(),
            application_pid,
        )
        .map_err(|error| error.to_string())?;
    device.product_config = Some(config);
    Ok(device.snapshot())
}

fn upgrade_block_reason(
    mode: DeviceMode,
    product_config: Option<&ProductConfig>,
) -> Option<&'static str> {
    if product_config.is_some_and(|config| config.provisioned) {
        None
    } else {
        Some(match mode {
            DeviceMode::BootRecovery => BOOT_CONFIG_REQUIRED_WARNING,
            DeviceMode::Application => APP_CONFIG_REQUIRED_WARNING,
        })
    }
}

fn install_image(
    connected: Option<ConnectedDevice>,
    path: &Path,
    ui: &slint::Weak<UpdaterWindow>,
) -> Result<ConnectedDevice, String> {
    let image = FirmwareImage::parse(fs::read(path).map_err(|error| error.to_string())?)
        .map_err(|error| error.to_string())?;
    let mut device = connected.ok_or_else(|| "Device is not connected".to_owned())?;
    let serial_number = device
        .product_config
        .as_ref()
        .filter(|config| config.provisioned)
        .map(|config| config.device_serial.clone())
        .unwrap_or_else(|| device.serial_number.clone());
    UpgradeWorkflow::install(&mut device.client, &image, |event| post_progress(ui, event))
        .map_err(|error| error.to_string())?;
    drop(device);

    let (info, mut client) = UpgradeWorkflow::wait_for_version(
        || NusbTransport::open_application(&serial_number),
        image.version,
        WAIT_TIMEOUT,
        WAIT_POLL,
        REQUEST_TIMEOUT,
        |event| post_progress(ui, event),
    )
    .map_err(|error| error.to_string())?;
    validate_hello(&client.hello().map_err(|error| error.to_string())?)?;
    let product_config = client.product_config().ok();
    Ok(ConnectedDevice {
        client,
        info,
        serial_number,
        mode: DeviceMode::Application,
        product_config,
    })
}

fn post_devices(ui: &slint::Weak<UpdaterWindow>, labels: Vec<String>) {
    let count = labels.len();
    let labels: Vec<_> = labels.into_iter().map(SharedString::from).collect();
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        ui.set_device_model(ModelRc::new(VecModel::from(labels)));
        ui.set_selected_device(if count == 0 { -1 } else { 0 });
        ui.set_can_connect(count != 0);
        ui.set_status_text(
            if count == 0 {
                "No updater devices found".to_owned()
            } else {
                format!("Found {count} updater device(s)")
            }
            .into(),
        );
    });
}

fn post_connected(
    ui: &slint::Weak<UpdaterWindow>,
    device: GuiDevice,
    status: &str,
    upgrade_complete: bool,
) {
    let status = status.to_owned();
    let _ = ui.upgrade_in_event_loop(move |ui| {
        let provisioned = device
            .product_config
            .as_ref()
            .is_some_and(|config| config.provisioned);
        ui.set_busy(false);
        ui.set_device_connected(true);
        ui.set_device_provisioned(provisioned);
        ui.set_can_connect(false);
        ui.set_can_disconnect(true);
        if upgrade_complete {
            ui.set_progress(1.0);
            ui.set_progress_label("100%".into());
        }
        set_device_info(&ui, &device.info);
        set_product_config_info(&ui, device.product_config.as_ref());
        if let Some(config) = device.product_config {
            ui.set_config_device_serial(config.device_serial.into());
            ui.set_config_hardware_version(config.hardware_version.into());
            ui.set_config_application_pid(format!("0x{:04x}", config.application_pid).into());
            ui.set_product_config_status(
                if config.provisioned {
                    "Provisioned"
                } else {
                    "Not provisioned"
                }
                .into(),
            );
            ui.set_can_configure(device.mode == DeviceMode::BootRecovery && !config.provisioned);
        } else {
            ui.set_product_config_status("Not supported by device".into());
            ui.set_can_configure(false);
        }
        ui.set_status_text(status.into());
    });
}

fn post_disconnected(ui: &slint::Weak<UpdaterWindow>, status: &str) {
    let status = status.to_owned();
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        ui.set_device_connected(false);
        ui.set_device_provisioned(false);
        ui.set_can_connect(ui.get_selected_device() >= 0);
        ui.set_can_disconnect(false);
        ui.set_can_configure(false);
        ui.set_status_text(status.into());
    });
}

fn post_error(ui: &slint::Weak<UpdaterWindow>, status: String, keep_connection: bool) {
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        if !keep_connection {
            ui.set_device_connected(false);
            ui.set_device_provisioned(false);
            ui.set_can_connect(ui.get_selected_device() >= 0);
            ui.set_can_disconnect(false);
            ui.set_can_configure(false);
        }
        ui.set_status_text(status.into());
    });
}

fn post_warning(ui: &slint::Weak<UpdaterWindow>, message: &'static str) {
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        ui.set_warning_text(message.into());
        ui.set_warning_visible(true);
        ui.set_status_text("Upgrade blocked: device parameters are not configured".into());
    });
}

fn post_progress(ui: &slint::Weak<UpdaterWindow>, event: ProgressEvent) {
    let _ = ui.upgrade_in_event_loop(move |ui| match event {
        ProgressEvent::Device(info) => {
            set_device_info(&ui, &info);
            ui.set_status_text(format!("Device application {}", info.application_version).into());
        }
        ProgressEvent::Transferring { sent, total } => {
            ui.set_progress(sent as f32 / total as f32);
            ui.set_progress_label(format!("{}%", sent * 100 / total).into());
            ui.set_status_text(format!("Transferring {sent}/{total} bytes").into());
        }
        ProgressEvent::Verifying => {
            ui.set_progress(0.98);
            ui.set_progress_label("98%".into());
            ui.set_status_text("Verifying image...".into());
        }
        ProgressEvent::Committing => {
            ui.set_progress(0.99);
            ui.set_progress_label("99%".into());
            ui.set_status_text("Committing upgrade...".into());
        }
        ProgressEvent::WaitingForReenumeration => {
            ui.set_progress(0.99);
            ui.set_progress_label("99%".into());
            ui.set_status_text("Waiting for the same device to re-enumerate...".into());
        }
    });
}

fn set_device_info(ui: &UpdaterWindow, info: &DeviceInfo) {
    ui.set_device_version(info.application_version.to_string().into());
}

fn set_product_config_info(ui: &UpdaterWindow, config: Option<&ProductConfig>) {
    let (device_serial, hardware_version, application_pid) = product_config_summary(config);
    ui.set_device_serial(device_serial.into());
    ui.set_device_hardware_version(hardware_version.into());
    ui.set_device_application_pid(application_pid.into());
}

fn product_config_summary(config: Option<&ProductConfig>) -> (String, String, String) {
    let Some(config) = config else {
        return (
            "Unavailable".to_owned(),
            "Unavailable".to_owned(),
            "Unavailable".to_owned(),
        );
    };
    (
        if config.device_serial.is_empty() {
            "—".to_owned()
        } else {
            config.device_serial.clone()
        },
        if config.hardware_version.is_empty() {
            "—".to_owned()
        } else {
            config.hardware_version.clone()
        },
        format!("0x{:04x}", config.application_pid),
    )
}

fn parse_application_pid(value: &str) -> Result<u16, String> {
    let value = value.trim();
    let digits = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .unwrap_or(value);
    u16::from_str_radix(digits, 16).map_err(|_| format!("Invalid hexadecimal App PID: {value}"))
}

fn inspect_image(path: &PathBuf) -> Result<GuiFirmware, String> {
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
    let sha256 = format!("{:x}", Sha256::digest(&bytes));
    let image = FirmwareImage::parse(bytes).map_err(|error| error.to_string())?;
    Ok(GuiFirmware {
        version: image.version.to_string(),
        sha256,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_product_config_for_device_summary() {
        let config = ProductConfig {
            device_serial: "SN12AB34".to_owned(),
            hardware_version: "A1.2".to_owned(),
            application_pid: 0x0020,
            provisioned: true,
        };
        assert_eq!(
            product_config_summary(Some(&config)),
            (
                "SN12AB34".to_owned(),
                "A1.2".to_owned(),
                "0x0020".to_owned()
            )
        );
    }

    #[test]
    fn formats_unprovisioned_product_config_without_usb_serial_fallback() {
        let config = ProductConfig {
            device_serial: String::new(),
            hardware_version: String::new(),
            application_pid: 0x0002,
            provisioned: false,
        };
        assert_eq!(
            product_config_summary(Some(&config)),
            ("—".to_owned(), "—".to_owned(), "0x0002".to_owned())
        );
    }

    #[test]
    fn blocks_upgrade_until_product_config_is_provisioned() {
        let unprovisioned = ProductConfig {
            device_serial: String::new(),
            hardware_version: String::new(),
            application_pid: 0x0002,
            provisioned: false,
        };
        let provisioned = ProductConfig {
            provisioned: true,
            ..unprovisioned.clone()
        };

        assert_eq!(
            upgrade_block_reason(DeviceMode::BootRecovery, Some(&unprovisioned)),
            Some(BOOT_CONFIG_REQUIRED_WARNING)
        );
        assert_eq!(
            upgrade_block_reason(DeviceMode::Application, Some(&unprovisioned)),
            Some(APP_CONFIG_REQUIRED_WARNING)
        );
        assert_eq!(
            upgrade_block_reason(DeviceMode::BootRecovery, None),
            Some(BOOT_CONFIG_REQUIRED_WARNING)
        );
        assert_eq!(
            upgrade_block_reason(DeviceMode::BootRecovery, Some(&provisioned)),
            None
        );
    }
}
