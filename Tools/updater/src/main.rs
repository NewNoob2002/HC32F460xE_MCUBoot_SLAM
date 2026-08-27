mod nusb_transport;

use hc32_updater::{FirmwareImage, ProgressEvent, ProtocolV1Client, UpgradeWorkflow, Version};
use nusb_transport::NusbTransport;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Duration;

const VID: u16 = 0xfffe;
const PID: u16 = 0xffff;
const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const WAIT_POLL: Duration = Duration::from_millis(250);

enum CliCommand {
    Info,
    Install(PathBuf),
    Wait { version: Version, timeout: Duration },
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
        CliCommand::Info => {
            let transport = open_device()?.ok_or("USB device not found")?;
            let mut client = ProtocolV1Client::new(transport, REQUEST_TIMEOUT);
            print_info(&client.info()?);
        }
        CliCommand::Install(path) => {
            let image = FirmwareImage::parse(fs::read(path)?)?;
            let transport = open_device()?.ok_or("USB device not found")?;
            let mut client = ProtocolV1Client::new(transport, REQUEST_TIMEOUT);
            UpgradeWorkflow::install(&mut client, &image, print_progress)?;
        }
        CliCommand::Wait { version, timeout } => {
            let info = UpgradeWorkflow::wait_for_version(
                open_device,
                version,
                timeout,
                WAIT_POLL,
                REQUEST_TIMEOUT,
                print_progress,
            )?;
            print_info(&info);
        }
    }
    Ok(())
}

fn open_device() -> std::io::Result<Option<NusbTransport>> {
    NusbTransport::open_unique(VID, PID)
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
        "info" if args.next().is_none() => Ok(Some(CliCommand::Info)),
        "install" => {
            let path = args.next().map(PathBuf::from).ok_or(())?;
            if args.next().is_some() {
                return Err(());
            }
            Ok(Some(CliCommand::Install(path)))
        }
        "wait" => parse_wait(args).map(Some),
        _ => Err(()),
    }
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
    eprintln!("usage:");
    eprintln!("  hc32-updater info");
    eprintln!("  hc32-updater install <signed-image>");
    eprintln!("  hc32-updater wait --version <version> [--timeout <seconds>]");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_wait_arguments() {
        assert!(
            parse_command(["wait", "--version", "2.0.0"].map(String::from).into_iter()).is_ok()
        );
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
}
