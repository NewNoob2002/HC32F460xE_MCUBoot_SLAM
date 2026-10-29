mod debug_cli;

use hc32_updater::admin::AdminKey;
use hc32_updater::nusb_transport::{DeviceMode, NusbTransport};
use hc32_updater::{
    FirmwareImage, ProductConfig, ProgressEvent, ProtocolV1Client, RebootTarget, UpgradeWorkflow,
    Version,
};
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const WAIT_POLL: Duration = Duration::from_millis(250);
const RECOVERY_TIMEOUT: Duration = Duration::from_secs(15);

enum CliCommand {
    Version,
    Info,
    ConfigGet,
    ConfigSet(String, String, u16),
    Install(PathBuf),
    Reboot(RebootTarget),
    FactoryErase(PathBuf),
    Wait { version: Version, timeout: Duration },
    Debug(debug_cli::Arguments),
}

fn main() -> ExitCode {
    let command = match parse_command(env::args().skip(1)) {
        Ok(Some(command)) => command,
        Ok(None) => {
            usage();
            return ExitCode::SUCCESS;
        }
        Err(()) => {
            usage();
            return ExitCode::from(2);
        }
    };

    match run(command) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("FAIL: {error}");
            ExitCode::from(1)
        }
    }
}

fn run(command: CliCommand) -> Result<(), Box<dyn std::error::Error>> {
    match command {
        CliCommand::Version => println!("hc32-updater {}", env!("CARGO_PKG_VERSION")),
        CliCommand::Info => {
            let transport = open_device()?.ok_or("USB device not found")?;
            let mode = transport.mode();
            let serial_number = transport
                .serial_number()
                .unwrap_or("not-reported")
                .to_owned();
            let info = with_client(transport, |client| client.info())?;
            print_info(&info);
            println!("mode={mode}");
            println!("serial={serial_number}");
        }
        CliCommand::Install(path) => {
            let image = FirmwareImage::parse(fs::read(path)?)?;
            finish(open_recovery_client()?, |client| {
                UpgradeWorkflow::install(client, &image, print_progress)
            })?;
        }
        CliCommand::Reboot(target) => {
            let transport = open_device()?.ok_or("USB device not found")?;
            with_client(transport, |client| client.reboot(target))?;
            println!("reboot requested");
        }
        CliCommand::FactoryErase(key_path) => {
            let key = AdminKey::from_pem(&fs::read_to_string(key_path)?)?;
            finish(open_recovery_client()?, |client| {
                client.admin_authenticate(&key)?;
                client.factory_erase()
            })?;
            println!("factory erase accepted; the device restarts unprovisioned in Boot recovery");
        }
        CliCommand::ConfigGet => {
            let transport = open_device()?.ok_or("USB device not found")?;
            print_product_config(&with_client(transport, |client| client.product_config())?);
        }
        CliCommand::ConfigSet(device_serial, hardware_version, application_pid) => {
            let transport = open_device()?.ok_or("USB device not found")?;
            print_product_config(&with_client(transport, |client| {
                client.provision_product_config(&device_serial, &hardware_version, application_pid)
            })?);
        }
        CliCommand::Wait { version, timeout } => {
            let (info, mut client) = UpgradeWorkflow::wait_for_version(
                NusbTransport::open_unique_application,
                version,
                timeout,
                WAIT_POLL,
                REQUEST_TIMEOUT,
                print_progress,
            )?;
            client.close();
            print_info(&info);
        }
        CliCommand::Debug(arguments) => debug_cli::run(arguments)?,
    }
    Ok(())
}

/// Runs one command and always ends the device session, also on failure, so the next CLI process is not
/// rejected until the device inactivity timeout.
fn with_client<R>(
    transport: NusbTransport,
    operation: impl FnOnce(&mut ProtocolV1Client<NusbTransport>) -> Result<R, hc32_updater::Error>,
) -> Result<R, hc32_updater::Error> {
    finish(ProtocolV1Client::new(transport, REQUEST_TIMEOUT), operation)
}

fn finish<R>(
    mut client: ProtocolV1Client<NusbTransport>,
    operation: impl FnOnce(&mut ProtocolV1Client<NusbTransport>) -> Result<R, hc32_updater::Error>,
) -> Result<R, hc32_updater::Error> {
    let result = operation(&mut client);
    client.close();
    result
}

fn open_device() -> std::io::Result<Option<NusbTransport>> {
    NusbTransport::open_unique_updater()
}

/// Opens the unique device in Boot recovery, first asking an Application device to restart into recovery. The
/// returned client may already hold the session opened while waiting, so it must be used, not re-created.
fn open_recovery_client() -> Result<ProtocolV1Client<NusbTransport>, Box<dyn std::error::Error>> {
    let transport = open_device()?.ok_or("USB device not found")?;
    if transport.mode() == DeviceMode::BootRecovery {
        return Ok(ProtocolV1Client::new(transport, REQUEST_TIMEOUT));
    }
    let serial_number = transport
        .serial_number()
        .filter(|serial| !serial.is_empty())
        .ok_or("entering Boot recovery requires a device with a USB serial number")?
        .to_owned();
    with_client(transport, |client| client.reboot(RebootTarget::Recovery))?;
    println!("entering Boot recovery");
    let (_, client) = UpgradeWorkflow::wait_for_recovery(
        || NusbTransport::open_recovery(&serial_number),
        RECOVERY_TIMEOUT,
        WAIT_POLL,
        REQUEST_TIMEOUT,
        print_progress,
    )?;
    Ok(client)
}

fn print_info(info: &hc32_updater::DeviceInfo) {
    println!(
        "application={} bootloader={}",
        info.application_version, info.bootloader_version
    );
    println!(
        "hardware=0x{:08x} board={} revision={} capacity={} write_alignment={} erase_alignment={}",
        info.hardware_id,
        info.board_id,
        info.board_revision,
        info.image_capacity,
        info.write_alignment,
        info.erase_alignment
    );
}

fn print_product_config(config: &ProductConfig) {
    println!(
        "provisioned={} device_serial={} hardware_version={} app_pid=0x{:04x}",
        config.provisioned, config.device_serial, config.hardware_version, config.application_pid
    );
}

fn print_progress(event: ProgressEvent) {
    match event {
        ProgressEvent::Device(info) => println!("device application={}", info.application_version),
        ProgressEvent::Transferring { sent, total } => println!("transfer {sent}/{total}"),
        ProgressEvent::Verifying => println!("verify"),
        ProgressEvent::Committing => println!("commit"),
        ProgressEvent::WaitingForReenumeration => println!("wait re-enumeration"),
    }
}

fn parse_command(args: impl Iterator<Item = String>) -> Result<Option<CliCommand>, ()> {
    let mut args = args.peekable();
    let Some(command) = args.next() else {
        return Ok(None);
    };
    match command.as_str() {
        "help" | "--help" | "-h" if args.next().is_none() => Ok(None),
        "--version" | "-V" if args.next().is_none() => Ok(Some(CliCommand::Version)),
        "info" if args.next().is_none() => Ok(Some(CliCommand::Info)),
        "config" => parse_config(args).map(Some),
        "install" => {
            let path = args.next().map(PathBuf::from).ok_or(())?;
            if args.next().is_some() {
                return Err(());
            }
            Ok(Some(CliCommand::Install(path)))
        }
        "reboot" => {
            let target = match args.next().as_deref() {
                Some("app") => RebootTarget::Application,
                Some("boot") => RebootTarget::Recovery,
                _ => return Err(()),
            };
            if args.next().is_some() {
                return Err(());
            }
            Ok(Some(CliCommand::Reboot(target)))
        }
        "factory-erase" => match (args.next().as_deref(), args.next(), args.next()) {
            (Some("--admin-key"), Some(path), None) => {
                Ok(Some(CliCommand::FactoryErase(PathBuf::from(path))))
            }
            _ => Err(()),
        },
        "debug" => debug_cli::parse(args).map(CliCommand::Debug).map(Some),
        "wait" => parse_wait(args).map(Some),
        _ => Err(()),
    }
}

fn parse_config(mut args: impl Iterator<Item = String>) -> Result<CliCommand, ()> {
    match args.next().as_deref() {
        Some("get") if args.next().is_none() => Ok(CliCommand::ConfigGet),
        Some("set") => {
            let mut device_serial = None;
            let mut hardware_version = None;
            let mut application_pid = None;
            while let Some(argument) = args.next() {
                match argument.as_str() {
                    "--device-serial" if device_serial.is_none() => {
                        device_serial = Some(args.next().ok_or(())?);
                    }
                    "--hardware-version" if hardware_version.is_none() => {
                        hardware_version = Some(args.next().ok_or(())?);
                    }
                    "--app-pid" if application_pid.is_none() => {
                        application_pid = Some(parse_application_pid(&args.next().ok_or(())?)?);
                    }
                    _ => return Err(()),
                }
            }
            Ok(CliCommand::ConfigSet(
                device_serial.ok_or(())?,
                hardware_version.ok_or(())?,
                application_pid.ok_or(())?,
            ))
        }
        _ => Err(()),
    }
}

fn parse_application_pid(value: &str) -> Result<u16, ()> {
    let digits = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .unwrap_or(value);
    u16::from_str_radix(digits, 16).map_err(|_| ())
}

fn parse_wait(mut args: impl Iterator<Item = String>) -> Result<CliCommand, ()> {
    let mut version = None;
    let mut timeout = Duration::from_secs(30);
    let mut timeout_seen = false;
    while let Some(argument) = args.next() {
        match argument.as_str() {
            "--version" if version.is_none() => {
                version = Some(args.next().ok_or(())?.parse().map_err(|_| ())?);
            }
            "--timeout" if !timeout_seen => {
                let seconds = args.next().ok_or(())?.parse::<u64>().map_err(|_| ())?;
                if seconds == 0 {
                    return Err(());
                }
                timeout = Duration::from_secs(seconds);
                timeout_seen = true;
            }
            _ => return Err(()),
        }
    }
    Ok(CliCommand::Wait {
        version: version.ok_or(())?,
        timeout,
    })
}

fn usage() {
    eprintln!("hc32-updater {}", env!("CARGO_PKG_VERSION"));
    eprintln!("usage:");
    eprintln!("  hc32-updater --version");
    eprintln!("  hc32-updater info");
    eprintln!("  hc32-updater config get");
    eprintln!(
        "  hc32-updater config set --device-serial <serial> --hardware-version <version> --app-pid <hex-pid>"
    );
    eprintln!(
        "  hc32-updater install <signed-image>   (restarts an Application device into Boot first)"
    );
    eprintln!("  hc32-updater reboot <app|boot>");
    eprintln!("  hc32-updater factory-erase --admin-key <private-key.pem>");
    eprintln!("  hc32-updater wait --version <version> [--timeout <seconds>]");
    debug_cli::usage();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn routes_debug_arguments_without_changing_update_commands() {
        assert!(matches!(
            parse_command(
                ["debug", "info", "--serial", "SN"]
                    .map(String::from)
                    .into_iter()
            ),
            Ok(Some(CliCommand::Debug(_)))
        ));
        assert!(parse_command(["debug", "info"].map(String::from).into_iter()).is_err());
        assert!(matches!(
            parse_command(["info"].map(String::from).into_iter()),
            Ok(Some(CliCommand::Info))
        ));
        assert!(matches!(
            parse_command(["install", "image.bin"].map(String::from).into_iter()),
            Ok(Some(CliCommand::Install(_)))
        ));
        assert!(matches!(
            parse_command(["reboot", "boot"].map(String::from).into_iter()),
            Ok(Some(CliCommand::Reboot(RebootTarget::Recovery)))
        ));
        assert!(parse_command(["reboot", "x"].map(String::from).into_iter()).is_err());
        assert!(matches!(
            parse_command(
                ["factory-erase", "--admin-key", "k.pem"]
                    .map(String::from)
                    .into_iter()
            ),
            Ok(Some(CliCommand::FactoryErase(_)))
        ));
        assert!(parse_command(["factory-erase"].map(String::from).into_iter()).is_err());
    }

    #[test]
    fn validates_wait_arguments() {
        assert!(
            parse_command(["wait", "--version", "2.0.0"].map(String::from).into_iter()).is_ok()
        );
        assert!(matches!(
            parse_command(["--version"].map(String::from).into_iter()),
            Ok(Some(super::CliCommand::Version))
        ));
        assert!(parse_command(["--version", "x"].map(String::from).into_iter()).is_err());
        assert!(
            parse_command(
                ["wait", "--timeout", "30", "--version", "2.0.0"]
                    .map(String::from)
                    .into_iter()
            )
            .is_ok()
        );
        assert!(parse_command(["wait", "--version", "2.0"].map(String::from).into_iter()).is_err());
        assert!(
            parse_command(
                ["wait", "--version", "2.0.0", "--timeout", "0"]
                    .map(String::from)
                    .into_iter()
            )
            .is_err()
        );
    }

    #[test]
    fn validates_product_config_arguments() {
        assert!(parse_command(["config", "get"].map(String::from).into_iter()).is_ok());
        assert!(
            parse_command(
                [
                    "config",
                    "set",
                    "--device-serial",
                    "SN12AB34",
                    "--hardware-version",
                    "A1.2",
                    "--app-pid",
                    "0x0020",
                ]
                .map(String::from)
                .into_iter()
            )
            .is_ok()
        );
        assert!(
            parse_command(
                ["config", "set", "--device-serial", "SN12AB34"]
                    .map(String::from)
                    .into_iter()
            )
            .is_err()
        );
        assert!(
            parse_command(
                [
                    "config",
                    "set",
                    "--device-serial",
                    "SN12AB34",
                    "--hardware-version",
                    "A1.2",
                    "--app-pid",
                    "0x10000",
                ]
                .map(String::from)
                .into_iter()
            )
            .is_err()
        );
    }
}
