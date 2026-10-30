// Windows builds start without a console window; the GUI never writes to stdout/stderr.
#![cfg_attr(windows, windows_subsystem = "windows")]
mod debug_gui;

use hc32_updater::admin::AdminKey;
use hc32_updater::nusb_transport::{DeviceMode, NusbTransport, UpdaterDevice};
use hc32_updater::{
    ADMIN_CAPABILITY, DeviceInfo, Error, FirmwareImage, HelloInfo, ProductConfig, ProgressEvent,
    ProtocolV1Client, RebootTarget, Status, UPGRADE_CAPABILITIES, UpgradeWorkflow, Version,
};
use sha2::{Digest, Sha256};
use slint::{ComponentHandle, ModelRc, SharedString, VecModel};
use std::cell::RefCell;
use std::fs;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::Arc;
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::thread;
use std::time::{Duration, Instant};

const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const HEARTBEAT_INTERVAL: Duration = Duration::from_secs(2);
const WAIT_TIMEOUT: Duration = Duration::from_secs(30);
const WAIT_POLL: Duration = Duration::from_millis(150);
/// How long a finished install keeps its Done/Failed result on the progress bar before it empties.
const PROGRESS_RESET_DELAY: Duration = Duration::from_secs(3);
/// Covers the device reset, Boot's boot_go() and USB enumeration of the other mode.
const RESTART_TIMEOUT: Duration = Duration::from_secs(15);
const BOOT_CONFIG_REQUIRED_WARNING: &str = "Device parameters are not configured. Open Device Setup in Boot Recovery and configure SN, Hardware Version, and App USB PID before upgrading.";
const APP_CONFIG_REQUIRED_WARNING: &str = "Device parameters are not configured. Reconnect the device in Boot Recovery, open Device Setup, and configure SN, Hardware Version, and App USB PID before upgrading.";

slint::include_modules!();

struct GuiDevice {
    info: DeviceInfo,
    mode: DeviceMode,
    capabilities: u32,
    product_config: Option<ProductConfig>,
}

struct ConnectedDevice {
    client: ProtocolV1Client<NusbTransport>,
    info: DeviceInfo,
    serial_number: String,
    mode: DeviceMode,
    capabilities: u32,
    product_config: Option<ProductConfig>,
}

impl ConnectedDevice {
    /// Wraps a client that has already answered DEVICE_INFO, for example one returned by a reconnect wait.
    fn from_client(
        mut client: ProtocolV1Client<NusbTransport>,
        info: DeviceInfo,
        serial_number: String,
        mode: DeviceMode,
    ) -> Result<Self, String> {
        let hello = client.hello().map_err(|error| error.to_string())?;
        validate_hello(&hello, mode)?;
        let product_config = client.product_config().ok();
        Ok(Self {
            client,
            info,
            serial_number,
            mode,
            capabilities: hello.capabilities,
            product_config,
        })
    }

    fn snapshot(&self) -> GuiDevice {
        GuiDevice {
            info: self.info.clone(),
            mode: self.mode,
            capabilities: self.capabilities,
            product_config: self.product_config.clone(),
        }
    }

    /// The serial both modes enumerate with: the provisioned serial, or the factory USB serial before setup.
    fn reconnect_serial(&self) -> String {
        self.product_config
            .as_ref()
            .filter(|config| config.provisioned)
            .map(|config| config.device_serial.clone())
            .unwrap_or_else(|| self.serial_number.clone())
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
    Reboot(RebootTarget),
    AdminVerify(Arc<AdminKey>),
    AdminErase(Arc<AdminKey>),
    Debug(debug_gui::Command),
}

fn main() -> Result<(), slint::PlatformError> {
    let ui = UpdaterWindow::new()?;
    ui.set_app_version(concat!("v", env!("CARGO_PKG_VERSION")).into());
    let (commands, receiver) = mpsc::channel();
    let worker_ui = ui.as_weak();
    thread::spawn(move || worker_loop(receiver, worker_ui));
    debug_gui::register(&ui, commands.clone());
    register_admin(&ui, commands.clone());

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
        ui.set_progress_tone(0);
        ui.set_progress_indeterminate(false);
        ui.set_progress(0.0);
        ui.set_progress_label("0%".into());
        ui.set_status_text("Checking firmware image...".into());
        let _ = commands.send(WorkerCommand::Install(PathBuf::from(path)));
    });

    ui.run()
}

/// The administrator key lives only on the UI thread and is handed to the worker per operation.
fn register_admin(ui: &UpdaterWindow, commands: mpsc::Sender<WorkerCommand>) {
    let key: Rc<RefCell<Option<Arc<AdminKey>>>> = Rc::default();

    let weak = ui.as_weak();
    let loaded = key.clone();
    ui.on_admin_load_key(move || {
        let Some(ui) = weak.upgrade() else { return };
        let Some(path) = rfd::FileDialog::new()
            .set_title("Administrator private key")
            .add_filter("PEM private key", &["pem", "key"])
            .pick_file()
        else {
            return;
        };
        match fs::read_to_string(&path)
            .map_err(|error| error.to_string())
            .and_then(|text| AdminKey::from_pem(&text).map_err(|error| error.to_string()))
        {
            Ok(admin) => {
                ui.set_admin_fingerprint(admin.fingerprint().into());
                ui.set_admin_unlocked(true);
                ui.set_admin_verified(false);
                ui.set_status_text(
                    "Administrator key loaded; it is checked by the device on use".into(),
                );
                *loaded.borrow_mut() = Some(Arc::new(admin));
            }
            Err(error) => ui.set_status_text(format!("Administrator key rejected: {error}").into()),
        }
    });

    let release = {
        let weak = ui.as_weak();
        let key = key.clone();
        move || {
            let ui = weak.upgrade()?;
            key.borrow_mut().take();
            ui.set_admin_unlocked(false);
            ui.set_admin_verified(false);
            ui.set_admin_fingerprint("No key loaded".into());
            Some(ui)
        }
    };
    let lock = release.clone();
    ui.on_admin_lock(move || {
        if let Some(ui) = lock() {
            ui.set_status_text("Administrator mode locked".into());
        }
    });
    // Admin rights end with the Boot session, so the host key must not outlive it either.
    ui.on_admin_release(move || {
        let _ = release();
    });

    let send_with_key = move |weak: slint::Weak<UpdaterWindow>,
                              key: Rc<RefCell<Option<Arc<AdminKey>>>>,
                              commands: mpsc::Sender<WorkerCommand>,
                              status: &'static str,
                              command: fn(Arc<AdminKey>) -> WorkerCommand| {
        move || {
            let Some(ui) = weak.upgrade() else { return };
            let Some(admin) = key.borrow().clone() else {
                ui.set_status_text("Load the administrator key first".into());
                return;
            };
            ui.set_busy(true);
            ui.set_status_text(status.into());
            let _ = commands.send(command(admin));
        }
    };
    ui.on_admin_verify(send_with_key(
        ui.as_weak(),
        key.clone(),
        commands.clone(),
        "Verifying the administrator key on the device...",
        WorkerCommand::AdminVerify,
    ));
    ui.on_admin_erase(send_with_key(
        ui.as_weak(),
        key,
        commands.clone(),
        "Erasing image slots and Product Config...",
        WorkerCommand::AdminErase,
    ));

    let weak = ui.as_weak();
    // Entering or leaving Boot needs no key: installs and administrator actions are only offered in Boot.
    ui.on_reboot_device(move |to_boot| {
        let Some(ui) = weak.upgrade() else { return };
        ui.set_busy(true);
        ui.set_status_text(
            if to_boot {
                "Restarting into Boot Recovery..."
            } else {
                "Restarting into the Application..."
            }
            .into(),
        );
        let _ = commands.send(WorkerCommand::Reboot(if to_boot {
            RebootTarget::Recovery
        } else {
            RebootTarget::Application
        }));
    });
}

fn worker_loop(receiver: Receiver<WorkerCommand>, ui: slint::Weak<UpdaterWindow>) {
    let mut devices = Vec::new();
    let mut connected: Option<ConnectedDevice> = None;
    let mut debug = debug_gui::State::default();
    let mut last_heartbeat = Instant::now();

    loop {
        let command = if connected.is_some() {
            let wait = HEARTBEAT_INTERVAL.saturating_sub(last_heartbeat.elapsed());
            let wait = if debug.streaming() {
                wait.min(Duration::from_millis(20))
            } else {
                wait
            };
            match receiver.recv_timeout(wait) {
                Ok(command) => Some(command),
                Err(RecvTimeoutError::Timeout) => None,
                Err(RecvTimeoutError::Disconnected) => {
                    debug.close(&ui, "Debug closed");
                    close_session(&mut connected);
                    return;
                }
            }
        } else {
            match receiver.recv() {
                Ok(command) => Some(command),
                Err(_) => {
                    debug.close(&ui, "Debug closed");
                    return;
                }
            }
        };

        if let Some(command) = command {
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
                    debug.close(&ui, "Open Debug on the connected Application device");
                    close_session(&mut connected);
                    let result = usize::try_from(index)
                        .ok()
                        .and_then(|index| devices.get(index))
                        .ok_or_else(|| "Select a device from the current list".to_owned())
                        .and_then(connect_device);
                    match result {
                        Ok(device) => {
                            let snapshot = device.snapshot();
                            connected = Some(device);
                            last_heartbeat = Instant::now();
                            post_connected(&ui, snapshot, "Connected", false);
                        }
                        Err(error) => post_error(&ui, format!("Connect failed: {error}"), false),
                    }
                }
                WorkerCommand::Disconnect => {
                    debug.close(&ui, "Debug disconnected");
                    close_session(&mut connected);
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
                        Err(error) => {
                            post_error(&ui, format!("Configuration failed: {error}"), true)
                        }
                    }
                }
                WorkerCommand::Install(path) => {
                    if let Some(message) = connected.as_ref().and_then(|device| {
                        upgrade_block_reason(device.mode, device.product_config.as_ref())
                    }) {
                        post_warning(&ui, message);
                        continue;
                    }
                    debug.close(
                        &ui,
                        "Debug closed for firmware installation; reopen after reconnect",
                    );
                    let result = install_image(connected.take(), &path, &ui);
                    match result {
                        Ok(device) => {
                            relist_devices(&mut devices, &device, &ui);
                            let snapshot = device.snapshot();
                            let version = device.info.application_version;
                            connected = Some(device);
                            last_heartbeat = Instant::now();
                            post_connected(
                                &ui,
                                snapshot,
                                &format!("Upgrade complete: application {version}"),
                                true,
                            );
                        }
                        Err(error) => {
                            post_disconnected(&ui, &format!("Install failed: {error}"));
                            let _ = ui.upgrade_in_event_loop(|ui| finish_progress(&ui, false));
                        }
                    }
                }
                WorkerCommand::Reboot(target) => {
                    debug.close(&ui, "Monitor closed for restart; reopen after reconnect");
                    match restart_device(connected.take(), target, &ui) {
                        Ok(device) => {
                            relist_devices(&mut devices, &device, &ui);
                            let snapshot = device.snapshot();
                            connected = Some(device);
                            last_heartbeat = Instant::now();
                            post_connected(
                                &ui,
                                snapshot,
                                "Device restarted and reconnected",
                                false,
                            );
                        }
                        Err(error) => post_disconnected(&ui, &format!("Restart failed: {error}")),
                    }
                }
                WorkerCommand::AdminVerify(key) => match connected.as_mut() {
                    Some(device) if device.mode == DeviceMode::BootRecovery => {
                        match device.client.admin_authenticate(&key) {
                            Ok(()) => post_admin_verified(
                                &ui,
                                true,
                                "Administrator key verified by the device",
                            ),
                            Err(error) => post_admin_verified(&ui, false, &admin_error(&error)),
                        }
                    }
                    _ => {
                        post_admin_verified(&ui, false, "Connect the device in Boot Recovery first")
                    }
                },
                WorkerCommand::AdminErase(key) => {
                    let result = match connected.as_mut() {
                        Some(device) if device.mode == DeviceMode::BootRecovery => device
                            .client
                            // A fresh challenge per erase: earlier verification rights are not relied on.
                            .admin_authenticate(&key)
                            .and_then(|()| device.client.factory_erase())
                            .map_err(|error| admin_error(&error)),
                        _ => Err("Connect the device in Boot Recovery first".to_owned()),
                    };
                    match result {
                        Ok(()) => {
                            connected = None;
                            post_disconnected(
                                &ui,
                                "Factory erase done. The device restarts unprovisioned in Boot Recovery with its factory serial: Refresh, connect, then use Device Setup.",
                            );
                        }
                        Err(error) => post_admin_verified(
                            &ui,
                            false,
                            &format!("Factory erase failed: {error}"),
                        ),
                    }
                }
                WorkerCommand::Debug(command) => debug.handle(command, connected.as_ref(), &ui),
            }
        }
        debug.poll(&ui);
        if connected.is_some() && last_heartbeat.elapsed() >= HEARTBEAT_INTERVAL {
            last_heartbeat = Instant::now();
            if heartbeat(&mut connected, &ui).is_err() {
                debug.close(&ui, "Debug closed: updater connection lost");
                connected = None;
            }
        }
    }
}

/// Ends the open update session (ABORT before COMMIT) so the next connection is not rejected until the
/// device inactivity timeout. A lost connection is simply dropped by the heartbeat path instead.
fn close_session(connected: &mut Option<ConnectedDevice>) {
    if let Some(mut device) = connected.take() {
        device.client.close();
    }
}

fn connect_device(device: &UpdaterDevice) -> Result<ConnectedDevice, String> {
    let transport = NusbTransport::open(device).map_err(|error| error.to_string())?;
    let mut client = ProtocolV1Client::new(transport, REQUEST_TIMEOUT);
    let info = client.info().map_err(|error| error.to_string())?;
    ConnectedDevice::from_client(
        client,
        info,
        device.serial_number().to_owned(),
        device.mode(),
    )
}

fn validate_hello(hello: &HelloInfo, mode: DeviceMode) -> Result<(), String> {
    // Images are installed only by Boot recovery; the Application must at least be able to restart into it.
    let required = match mode {
        DeviceMode::BootRecovery => UPGRADE_CAPABILITIES,
        DeviceMode::Application => hc32_updater::REBOOT_CAPABILITY,
    };
    if hello.capabilities & required != required {
        return Err(format!(
            "missing required capabilities: 0x{:08x}; update the device firmware or use a matching updater",
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

/// Sends REBOOT and reconnects to the same serial in the target mode. The returned device already holds a
/// session, so it is used directly instead of reconnecting again.
fn restart_device(
    connected: Option<ConnectedDevice>,
    target: RebootTarget,
    ui: &slint::Weak<UpdaterWindow>,
) -> Result<ConnectedDevice, String> {
    let mut device = connected.ok_or_else(|| "Device is not connected".to_owned())?;
    let serial_number = device.reconnect_serial();
    if let Err(error) = device.client.reboot(target) {
        device.client.close();
        return Err(error.to_string());
    }
    drop(device);
    // A restart is not an install step: report reconnection in the status line only and leave the install
    // progress bar alone.
    let progress = |event| post_restart_progress(ui, event);
    let (info, client, mode) = match target {
        RebootTarget::Recovery => {
            let (info, client) = UpgradeWorkflow::wait_for_recovery(
                || NusbTransport::open_recovery(&serial_number),
                RESTART_TIMEOUT,
                WAIT_POLL,
                REQUEST_TIMEOUT,
                progress,
            )
            .map_err(|error| error.to_string())?;
            (info, client, DeviceMode::BootRecovery)
        }
        RebootTarget::Application => {
            let (info, client) = UpgradeWorkflow::wait_for(
                || NusbTransport::open_application(&serial_number),
                |_| true,
                Error::ReconnectTimeout("the Application"),
                RESTART_TIMEOUT,
                WAIT_POLL,
                REQUEST_TIMEOUT,
                progress,
            )
            .map_err(|error| error.to_string())?;
            (info, client, DeviceMode::Application)
        }
    };
    ConnectedDevice::from_client(client, info, serial_number, mode)
}

fn admin_error(error: &Error) -> String {
    match error {
        Error::Device(Status::VerifyError) => {
            "the device rejected this administrator key (wrong key or a Boot built for another key)"
                .to_owned()
        }
        Error::Capability(_) => "this Boot was built without administrator mode".to_owned(),
        error => error.to_string(),
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
    let serial_number = device.reconnect_serial();
    if device.mode == DeviceMode::Application {
        let _ = ui.upgrade_in_event_loop(|ui| {
            ui.set_status_text("Restarting into Boot Recovery for installation...".into());
        });
        device = restart_device(Some(device), RebootTarget::Recovery, ui)
            .map_err(|error| format!("could not enter Boot Recovery: {error}"))?;
    }
    if let Err(error) =
        UpgradeWorkflow::install(&mut device.client, &image, |event| post_progress(ui, event))
    {
        // Leave the device in IDLE instead of RECEIVING/ERROR so a reconnect can HELLO at once.
        device.client.close();
        return Err(error.to_string());
    }
    drop(device);

    let (info, client) = UpgradeWorkflow::wait_for_version(
        || NusbTransport::open_application(&serial_number),
        image.version,
        WAIT_TIMEOUT,
        WAIT_POLL,
        REQUEST_TIMEOUT,
        |event| post_progress(ui, event),
    )
    .map_err(|error| error.to_string())?;
    ConnectedDevice::from_client(client, info, serial_number, DeviceMode::Application)
}

/// Re-lists devices after a restart so the selector shows the connected device in its new mode instead of
/// the stale entry it was connected from.
fn relist_devices(
    devices: &mut Vec<UpdaterDevice>,
    device: &ConnectedDevice,
    ui: &slint::Weak<UpdaterWindow>,
) {
    let Ok(found) = NusbTransport::enumerate() else {
        return;
    };
    let selected = found
        .iter()
        .position(|listed| {
            listed.mode() == device.mode && listed.serial_number() == device.serial_number
        })
        .map_or(-1, |index| index as i32);
    let labels: Vec<SharedString> = found.iter().map(|listed| listed.label().into()).collect();
    *devices = found;
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_device_model(ModelRc::new(VecModel::from(labels)));
        ui.set_selected_device(selected);
    });
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
        ui.set_reconnecting(false);
        ui.set_device_connected(true);
        ui.set_device_mode(
            match device.mode {
                DeviceMode::Application => "Application",
                DeviceMode::BootRecovery => "Boot Recovery",
            }
            .into(),
        );
        ui.set_device_in_boot(device.mode == DeviceMode::BootRecovery);
        ui.set_device_admin_capable(device.capabilities & ADMIN_CAPABILITY != 0);
        ui.set_admin_verified(false);
        ui.set_debug_available(device.mode == DeviceMode::Application);
        ui.set_device_provisioned(provisioned);
        ui.set_can_connect(false);
        ui.set_can_disconnect(true);
        // A finished install briefly shows Done; any other connection or restart leaves an empty bar.
        if upgrade_complete {
            finish_progress(&ui, true);
        } else if ui.get_progress_tone() == 0 {
            ui.set_progress_indeterminate(false);
            ui.set_progress(0.0);
            ui.set_progress_label("0%".into());
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

fn post_admin_verified(ui: &slint::Weak<UpdaterWindow>, verified: bool, status: &str) {
    let status = status.to_owned();
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        ui.set_admin_verified(verified);
        ui.set_status_text(status.into());
    });
}

/// Shows the install result on the bar, then lets it ease back to empty so it never sticks at 100%.
fn finish_progress(ui: &UpdaterWindow, succeeded: bool) {
    ui.set_progress_indeterminate(false);
    ui.set_progress_tone(if succeeded { 1 } else { 2 });
    if succeeded {
        ui.set_progress(1.0);
        ui.set_progress_label("Done".into());
    } else {
        ui.set_progress_label("Failed".into());
    }
    let weak = ui.as_weak();
    slint::Timer::single_shot(PROGRESS_RESET_DELAY, move || {
        let Some(ui) = weak.upgrade() else { return };
        // A new install has already taken over the bar.
        if ui.get_busy() || ui.get_progress_tone() == 0 {
            return;
        }
        ui.set_progress_tone(0);
        ui.set_progress(0.0);
        ui.set_progress_label("0%".into());
    });
}

fn clear_device_mode(ui: &UpdaterWindow) {
    ui.set_device_mode("—".into());
    ui.set_device_in_boot(false);
    ui.set_device_admin_capable(false);
    ui.set_admin_verified(false);
}

fn post_disconnected(ui: &slint::Weak<UpdaterWindow>, status: &str) {
    let status = status.to_owned();
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_busy(false);
        ui.set_reconnecting(false);
        clear_device_mode(&ui);
        ui.set_device_connected(false);
        ui.set_debug_available(false);
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
        ui.set_reconnecting(false);
        if !keep_connection {
            clear_device_mode(&ui);
            ui.set_device_connected(false);
            ui.set_debug_available(false);
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
            ui.set_status_text(
                format!(
                    "Device is back with application {}; reconnecting...",
                    info.application_version
                )
                .into(),
            );
        }
        ProgressEvent::Transferring { sent, total } => {
            ui.set_reconnecting(false);
            ui.set_progress_indeterminate(false);
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
            // Boot swaps the slots before the new Application enumerates and that time is unknown, so the
            // bar runs indeterminate with an elapsed-time label instead of freezing at 99%.
            ui.set_reconnecting(true);
            ui.set_progress_indeterminate(true);
            ui.set_status_text(
                "Device is restarting: Boot swaps in the new image, then the Application reconnects..."
                    .into(),
            );
        }
    });
}

fn post_restart_progress(ui: &slint::Weak<UpdaterWindow>, event: ProgressEvent) {
    let status = match event {
        ProgressEvent::WaitingForReenumeration => "Waiting for the device to restart...",
        ProgressEvent::Device(_) => "Device found; reconnecting...",
        _ => return,
    };
    let _ = ui.upgrade_in_event_loop(move |ui| {
        ui.set_reconnecting(true);
        ui.set_status_text(status.into());
    });
}

fn set_device_info(ui: &UpdaterWindow, info: &DeviceInfo) {
    ui.set_device_version(version_label(info.application_version, "Not installed").into());
    // A Boot built before the fixed boot-info record reports 0.0.0.
    ui.set_device_boot_version(version_label(info.bootloader_version, "Unknown").into());
}

/// Protocol V1 uses 0.0.0 for "no version": an empty Primary slot in Boot, or a Boot without its info record.
fn version_label(version: Version, missing: &str) -> String {
    if version == Version::default() {
        missing.to_owned()
    } else {
        version.to_string()
    }
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
