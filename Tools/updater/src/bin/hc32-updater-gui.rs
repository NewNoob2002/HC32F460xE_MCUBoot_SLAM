#[path = "../nusb_transport.rs"]
mod nusb_transport;

use hc32_updater::{DeviceInfo, FirmwareImage, ProgressEvent, ProtocolV1Client, UpgradeWorkflow};
use nusb_transport::NusbTransport;
use slint::ComponentHandle;
use std::fs;
use std::path::Path;
use std::thread;
use std::time::Duration;

const VID: u16 = 0xfffe;
const PID: u16 = 0xffff;
const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const WAIT_TIMEOUT: Duration = Duration::from_secs(30);
const WAIT_POLL: Duration = Duration::from_millis(250);

slint::include_modules!();

fn main() -> Result<(), slint::PlatformError> {
    let ui = UpdaterWindow::new()?;

    let weak = ui.as_weak();
    ui.on_refresh_device(move || {
        let Some(ui) = weak.upgrade() else {
            return;
        };
        ui.set_busy(true);
        ui.set_progress(0.0);
        ui.set_status_text("Reading device information...".into());
        drop(ui);

        let worker_ui = weak.clone();
        thread::spawn(move || {
            let result = read_device();
            let _ = worker_ui.upgrade_in_event_loop(move |ui| {
                ui.set_busy(false);
                match result {
                    Ok(info) => {
                        ui.set_device_summary(format_device(&info).into());
                        ui.set_status_text("Device ready".into());
                    }
                    Err(error) => ui.set_status_text(format!("Refresh failed: {error}").into()),
                }
            });
        });
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
                        ui.set_device_summary(format_device(&info).into());
                        ui.set_status_text(
                            format!("Upgrade complete: application {}", info.application_version)
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
    NusbTransport::open_unique(VID, PID)
        .map_err(|error| error.to_string())?
        .ok_or_else(|| "USB device not found".to_owned())
}

fn read_device() -> Result<DeviceInfo, String> {
    ProtocolV1Client::new(open_device()?, REQUEST_TIMEOUT)
        .info()
        .map_err(|error| error.to_string())
}

fn install_image(
    path: &Path,
    mut progress: impl FnMut(ProgressEvent),
) -> Result<DeviceInfo, String> {
    let image = FirmwareImage::parse(fs::read(path).map_err(|error| error.to_string())?)
        .map_err(|error| error.to_string())?;
    let mut client = ProtocolV1Client::new(open_device()?, REQUEST_TIMEOUT);
    UpgradeWorkflow::install(&mut client, &image, &mut progress)
        .map_err(|error| error.to_string())?;
    UpgradeWorkflow::wait_for_version(
        || NusbTransport::open_unique(VID, PID),
        image.version,
        WAIT_TIMEOUT,
        WAIT_POLL,
        REQUEST_TIMEOUT,
        progress,
    )
    .map_err(|error| error.to_string())
}

fn post_progress(ui: &slint::Weak<UpdaterWindow>, event: ProgressEvent) {
    let _ = ui.upgrade_in_event_loop(move |ui| match event {
        ProgressEvent::Device(info) => {
            ui.set_device_summary(format_device(&info).into());
            ui.set_status_text(format!("Device application {}", info.application_version).into());
        }
        ProgressEvent::Transferring { sent, total } => {
            ui.set_progress(sent as f32 / total as f32);
            ui.set_status_text(format!("Transferring {sent}/{total} bytes").into());
        }
        ProgressEvent::Verifying => {
            ui.set_progress(0.98);
            ui.set_status_text("Verifying image...".into());
        }
        ProgressEvent::Committing => {
            ui.set_progress(0.99);
            ui.set_status_text("Committing upgrade...".into());
        }
        ProgressEvent::WaitingForReenumeration => {
            ui.set_progress(0.99);
            ui.set_status_text("Waiting for USB re-enumeration...".into());
        }
    });
}

fn format_device(info: &DeviceInfo) -> String {
    format!(
        "Application {} · Bootloader {} · HW 0x{:08x} · Board {}/{} · Capacity {} bytes",
        info.application_version,
        info.bootloader_version,
        info.hardware_id,
        info.board_id,
        info.board_revision,
        info.image_capacity
    )
}
