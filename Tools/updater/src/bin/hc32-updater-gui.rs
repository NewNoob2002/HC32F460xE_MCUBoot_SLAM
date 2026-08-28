#[path = "../nusb_transport.rs"]
mod nusb_transport;

use hc32_updater::{DeviceInfo, FirmwareImage, ProgressEvent, ProtocolV1Client, UpgradeWorkflow};
use nusb_transport::{DeviceMode, NusbTransport};
use sha2::{Digest, Sha256};
use slint::ComponentHandle;
use std::fs;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::Duration;

const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const WAIT_TIMEOUT: Duration = Duration::from_secs(30);
const WAIT_POLL: Duration = Duration::from_millis(250);

slint::include_modules!();

struct GuiDevice {
    info: DeviceInfo,
    serial_number: String,
    mode: DeviceMode,
}

struct GuiFirmware {
    version: String,
    sha256: String,
}

fn main() -> Result<(), slint::PlatformError> {
    let ui = UpdaterWindow::new()?;

    let weak = ui.as_weak();
    ui.on_refresh_device(move || {
        let Some(ui) = weak.upgrade() else {
            return;
        };
        ui.set_busy(true);
        ui.set_progress(0.0);
        ui.set_progress_label("0%".into());
        ui.set_status_text("Reading device information...".into());
        drop(ui);

        let worker_ui = weak.clone();
        thread::spawn(move || {
            let result = read_device();
            let _ = worker_ui.upgrade_in_event_loop(move |ui| {
                ui.set_busy(false);
                match result {
                    Ok(info) => {
                        let status = format!("{} ready", info.mode);
                        set_device(&ui, &info);
                        ui.set_status_text(status.into());
                    }
                    Err(error) => {
                        ui.set_device_connected(false);
                        ui.set_status_text(format!("Refresh failed: {error}").into());
                    }
                }
            });
        });
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
        let path = path.trim().to_owned();
        let Some(ui) = weak.upgrade() else {
            return;
        };
        if path.is_empty() {
            ui.set_status_text("Choose a signed firmware image".into());
            return;
        }
        ui.set_busy(true);
        ui.set_progress(0.0);
        ui.set_progress_label("0%".into());
        ui.set_status_text("Checking firmware image...".into());
        drop(ui);

        let worker_ui = weak.clone();
        thread::spawn(move || {
            let progress_ui = worker_ui.clone();
            let result = install_image(Path::new(&path), move |event| {
                post_progress(&progress_ui, event);
            });
            let _ = worker_ui.upgrade_in_event_loop(move |ui| {
                ui.set_busy(false);
                match result {
                    Ok(info) => {
                        ui.set_progress(1.0);
                        ui.set_progress_label("100%".into());
                        set_device(&ui, &info);
                        ui.set_status_text(
                            format!(
                                "Upgrade complete: application {}",
                                info.info.application_version
                            )
                            .into(),
                        );
                    }
                    Err(error) => ui.set_status_text(format!("Install failed: {error}").into()),
                }
            });
        });
    });

    ui.run()
}

fn open_device() -> Result<NusbTransport, String> {
    NusbTransport::open_unique_updater()
        .map_err(|error| error.to_string())?
        .ok_or_else(|| "USB device not found".to_owned())
}

fn read_device() -> Result<GuiDevice, String> {
    let transport = open_device()?;
    let mode = transport.mode();
    let serial_number = transport
        .serial_number()
        .unwrap_or("Not reported")
        .to_owned();
    let info = ProtocolV1Client::new(transport, REQUEST_TIMEOUT)
        .info()
        .map_err(|error| error.to_string())?;
    Ok(GuiDevice {
        info,
        serial_number,
        mode,
    })
}

fn install_image(
    path: &Path,
    mut progress: impl FnMut(ProgressEvent),
) -> Result<GuiDevice, String> {
    let image = FirmwareImage::parse(fs::read(path).map_err(|error| error.to_string())?)
        .map_err(|error| error.to_string())?;
    let transport = open_device()?;
    let serial_number = transport
        .serial_number()
        .unwrap_or("Not reported")
        .to_owned();
    let mut client = ProtocolV1Client::new(transport, REQUEST_TIMEOUT);
    UpgradeWorkflow::install(&mut client, &image, &mut progress)
        .map_err(|error| error.to_string())?;
    let info = UpgradeWorkflow::wait_for_version(
        NusbTransport::open_application,
        image.version,
        WAIT_TIMEOUT,
        WAIT_POLL,
        REQUEST_TIMEOUT,
        progress,
    )
    .map_err(|error| error.to_string())?;
    Ok(GuiDevice {
        info,
        serial_number,
        mode: DeviceMode::Application,
    })
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
            ui.set_status_text("Waiting for USB re-enumeration...".into());
        }
    });
}

fn set_device(ui: &UpdaterWindow, device: &GuiDevice) {
    ui.set_device_serial(device.serial_number.clone().into());
    set_device_info(ui, &device.info);
    ui.set_device_details(format_device_details(&device.info, Some(device.mode)).into());
}

fn set_device_info(ui: &UpdaterWindow, info: &DeviceInfo) {
    ui.set_device_connected(true);
    ui.set_device_version(info.application_version.to_string().into());
    ui.set_device_details(format_device_details(info, None).into());
}

fn format_device_details(info: &DeviceInfo, mode: Option<DeviceMode>) -> String {
    let mode = mode.map(|value| format!("{value} · ")).unwrap_or_default();
    format!(
        "{mode}Bootloader {} · HW 0x{:08x} · Board {}/{} · Capacity {} bytes",
        info.bootloader_version,
        info.hardware_id,
        info.board_id,
        info.board_revision,
        info.image_capacity
    )
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
