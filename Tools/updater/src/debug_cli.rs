use hc32_updater::Transport;
use hc32_updater::debug_link::{DebugClient, Event};
use hc32_updater::nusb_transport::NusbTransport;
use std::fs::File;
use std::io::{self, BufWriter, Write};
use std::path::PathBuf;
use std::time::{Duration, Instant};

const REQUEST_TIMEOUT: Duration = Duration::from_secs(2);
const EVENT_POLL: Duration = Duration::from_millis(250);
type CliResult<T> = Result<T, Box<dyn std::error::Error>>;

#[derive(Debug, PartialEq, Eq)]
pub(crate) struct Arguments {
    serial: String,
    yes: bool,
    command: Command,
}

#[derive(Debug, PartialEq, Eq)]
enum Command {
    Info,
    Monitor(MonitorOptions),
    ConfigList,
    ConfigGet(Key),
    ConfigSet(Key, i32),
    ConfigReset,
    RegRead { device: u8, reg: u8, len: u8 },
    Stats,
    Reboot,
}

#[derive(Debug, PartialEq, Eq)]
enum Key {
    Number(u16),
    Name(String),
}

#[derive(Debug, PartialEq, Eq)]
struct MonitorOptions {
    period_ms: u16,
    log: bool,
    csv: Option<PathBuf>,
    duration: Option<Duration>,
}

impl Command {
    fn mutates(&self) -> bool {
        matches!(self, Self::ConfigSet(..) | Self::ConfigReset | Self::Reboot)
    }
}

pub(crate) fn parse(args: impl Iterator<Item = String>) -> Result<Arguments, ()> {
    let mut args = args.peekable();
    let mut serial = None;
    let mut yes = false;
    let mut positional = Vec::new();
    while let Some(argument) = args.next() {
        match argument.as_str() {
            "--serial" if serial.is_none() => {
                let value = args.next().ok_or(())?;
                if value.is_empty() || value.starts_with('-') {
                    return Err(());
                }
                serial = Some(value);
            }
            "--yes" if !yes => yes = true,
            "--serial" | "--yes" => return Err(()),
            _ => positional.push(argument),
        }
    }
    let command = match positional.as_slice() {
        [command] if command == "info" => Command::Info,
        [command, options @ ..] if command == "monitor" => {
            Command::Monitor(parse_monitor(options)?)
        }
        [command, operation] if command == "config" && operation == "list" => Command::ConfigList,
        [command, operation, key] if command == "config" && operation == "get" => {
            Command::ConfigGet(parse_key(key)?)
        }
        [command, operation, key, value] if command == "config" && operation == "set" => {
            Command::ConfigSet(parse_key(key)?, parse_value(value)?)
        }
        [command, operation] if command == "config" && operation == "reset" => Command::ConfigReset,
        [command, device, reg, len] if command == "reg-read" => {
            let device = u8::try_from(parse_number(device)?).map_err(|_| ())?;
            let reg = u8::try_from(parse_number(reg)?).map_err(|_| ())?;
            let len = u8::try_from(parse_number(len)?).map_err(|_| ())?;
            if device > 3 || !(1..=32).contains(&len) {
                return Err(());
            }
            Command::RegRead { device, reg, len }
        }
        [command] if command == "stats" => Command::Stats,
        [command] if command == "reboot" => Command::Reboot,
        _ => return Err(()),
    };
    if yes && !command.mutates() {
        return Err(());
    }
    Ok(Arguments {
        serial: serial.ok_or(())?,
        yes,
        command,
    })
}

fn parse_monitor(args: &[String]) -> Result<MonitorOptions, ()> {
    let mut options = MonitorOptions {
        period_ms: 1000,
        log: false,
        csv: None,
        duration: None,
    };
    let mut period_seen = false;
    let mut args = args.iter();
    while let Some(argument) = args.next() {
        match argument.as_str() {
            "--period" if !period_seen => {
                options.period_ms =
                    u16::try_from(parse_number(args.next().ok_or(())?)?).map_err(|_| ())?;
                if !(100..=5000).contains(&options.period_ms) {
                    return Err(());
                }
                period_seen = true;
            }
            "--log" if !options.log => options.log = true,
            "--csv" if options.csv.is_none() => {
                let path = args.next().ok_or(())?;
                if path.is_empty() || path.starts_with('-') {
                    return Err(());
                }
                options.csv = Some(PathBuf::from(path));
            }
            "--duration" if options.duration.is_none() => {
                let seconds = args.next().ok_or(())?.parse::<u64>().map_err(|_| ())?;
                if seconds == 0 {
                    return Err(());
                }
                options.duration = Some(Duration::from_secs(seconds));
            }
            _ => return Err(()),
        }
    }
    Ok(options)
}

fn parse_number(value: &str) -> Result<u32, ()> {
    if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u32::from_str_radix(hex, 16).map_err(|_| ())
    } else {
        value.parse().map_err(|_| ())
    }
}

fn parse_value(value: &str) -> Result<i32, ()> {
    let (negative, digits) = match value.strip_prefix('-') {
        Some(digits) => (true, digits),
        None => (false, value),
    };
    if digits.starts_with("0x") || digits.starts_with("0X") {
        let magnitude = i64::from(parse_number(digits)?);
        i32::try_from(if negative { -magnitude } else { magnitude }).map_err(|_| ())
    } else {
        value.parse().map_err(|_| ())
    }
}

fn parse_key(value: &str) -> Result<Key, ()> {
    if value.bytes().all(|byte| byte.is_ascii_digit())
        || value.starts_with("0x")
        || value.starts_with("0X")
    {
        return u16::try_from(parse_number(value)?)
            .map(Key::Number)
            .map_err(|_| ());
    }
    if !value.is_ascii()
        || value.bytes().any(|byte| byte.is_ascii_control())
        || value.starts_with('-')
    {
        return Err(());
    }
    Ok(Key::Name(value.to_owned()))
}

pub(crate) fn run(arguments: Arguments) -> CliResult<()> {
    let transport = NusbTransport::open_debug(&arguments.serial)?
        .ok_or("Application USB device with the requested serial not found")?;
    let serial = transport
        .serial_number()
        .filter(|serial| !serial.is_empty())
        .ok_or("Application USB device did not report its serial")?
        .to_owned();
    let mut client = DebugClient::new(transport, REQUEST_TIMEOUT);
    let hello = client.hello()?;
    match arguments.command {
        Command::Info => {
            println!("serial={serial} mode=application");
            println!(
                "debug={}.{} max_payload={} capabilities=0x{:08x} telemetry_version={}",
                hello.major,
                hello.minor,
                hello.max_payload,
                hello.capabilities,
                hello.telemetry_version
            );
        }
        Command::Monitor(options) => monitor(&mut client, &options)?,
        Command::ConfigList => {
            for entry in client.config_list()? {
                println!(
                    "0x{:04x} {}={} type={:?} unit={} min={} max={} default={} flags=0x{:02x}",
                    entry.key,
                    entry.name,
                    entry.current,
                    entry.value_type,
                    entry.unit,
                    entry.min,
                    entry.max,
                    entry.default,
                    entry.flags
                );
            }
        }
        Command::ConfigGet(key) => {
            let key = resolve_key(&mut client, &key)?;
            println!("0x{key:04x}={}", client.config_get(key)?);
        }
        Command::ConfigSet(key, value) => {
            let key = resolve_key(&mut client, &key)?;
            unlock(
                &mut client,
                &serial,
                arguments.yes,
                &format!("set config 0x{key:04x} to {value}"),
            )?;
            println!("0x{key:04x}={}", client.config_set(key, value)?);
        }
        Command::ConfigReset => {
            unlock(&mut client, &serial, arguments.yes, "reset all settings")?;
            client.config_reset()?;
            println!("settings reset");
        }
        Command::RegRead { device, reg, len } => {
            let bytes = client.reg_read(device, reg, len)?;
            print!("device={device} reg=0x{reg:02x}:");
            for byte in bytes {
                print!(" {byte:02x}");
            }
            println!();
        }
        Command::Stats => {
            for field in client.stats()? {
                println!("{field}");
            }
        }
        Command::Reboot => {
            unlock(&mut client, &serial, arguments.yes, "reboot")?;
            client.reboot()?;
            println!("reboot requested");
        }
    }
    Ok(())
}

fn resolve_key<T: Transport>(client: &mut DebugClient<T>, key: &Key) -> CliResult<u16> {
    match key {
        Key::Number(key) => Ok(*key),
        Key::Name(name) => {
            let entries = client.config_list()?;
            let mut matches = entries.iter().filter(|entry| entry.name == *name);
            let key = matches
                .next()
                .ok_or_else(|| format!("unknown config name: {name}"))?
                .key;
            if matches.next().is_some() {
                return Err(format!("ambiguous config name: {name}; use its numeric key").into());
            }
            Ok(key)
        }
    }
}

fn unlock<T: Transport>(
    client: &mut DebugClient<T>,
    serial: &str,
    yes: bool,
    action: &str,
) -> CliResult<()> {
    if !yes {
        eprint!("{action} on {serial}? [y/N] ");
        io::stderr().flush()?;
        let mut answer = String::new();
        io::stdin().read_line(&mut answer)?;
        if !matches!(answer.trim().to_ascii_lowercase().as_str(), "y" | "yes") {
            return Err("cancelled; device unchanged".into());
        }
    }
    client.unlock(serial)?;
    Ok(())
}

fn monitor<T: Transport>(client: &mut DebugClient<T>, options: &MonitorOptions) -> CliResult<()> {
    let mut csv = options
        .csv
        .as_ref()
        .map(File::create)
        .transpose()?
        .map(BufWriter::new);
    if let Some(csv) = csv.as_mut() {
        writeln!(
            csv,
            "host_elapsed_ms,event,sequence,field,value,level,uptime_ms,text"
        )?;
        csv.flush()?;
    }
    let stdout = io::stdout();
    let mut stdout = stdout.lock();
    let result = (|| -> CliResult<()> {
        client.stream_set(options.period_ms, if options.log { 3 } else { 1 })?;
        let started = Instant::now();
        loop {
            let elapsed = started.elapsed();
            let timeout = match options.duration {
                Some(duration) if elapsed >= duration => break,
                Some(duration) => EVENT_POLL.min(duration - elapsed),
                None => EVENT_POLL,
            };
            if let Some(event) = client.poll_event(timeout)? {
                print_event(&mut stdout, &event)?;
                if let Some(csv) = csv.as_mut() {
                    write_csv_event(csv, started.elapsed(), &event)?;
                    csv.flush()?;
                }
                stdout.flush()?;
            }
        }
        Ok(())
    })();
    let stop = client.stream_set(0, 0).map_err(Into::into);
    let flush = csv.map_or(Ok(()), |mut csv| csv.flush().map_err(Into::into));
    result.and(stop).and(flush)
}

fn print_event(output: &mut impl Write, event: &Event) -> io::Result<()> {
    match event {
        Event::Telemetry { sequence, fields } => {
            write!(output, "telemetry sequence={sequence}")?;
            for field in fields {
                write!(output, " {field}")?;
            }
            writeln!(output)
        }
        Event::Log {
            sequence,
            level,
            uptime_ms,
            text,
        } => writeln!(
            output,
            "log sequence={sequence} level={level} uptime_ms={uptime_ms} {text}"
        ),
    }
}

fn write_csv_event(output: &mut impl Write, elapsed: Duration, event: &Event) -> io::Result<()> {
    let elapsed = elapsed.as_millis().to_string();
    match event {
        Event::Telemetry { sequence, fields } => {
            for field in fields {
                write_csv_row(
                    output,
                    &[
                        &elapsed,
                        "telemetry",
                        &sequence.to_string(),
                        &format!("0x{:02x}", field.tag),
                        &field.value.to_string(),
                        "",
                        "",
                        "",
                    ],
                )?;
            }
            Ok(())
        }
        Event::Log {
            sequence,
            level,
            uptime_ms,
            text,
        } => write_csv_row(
            output,
            &[
                &elapsed,
                "log",
                &sequence.to_string(),
                "",
                "",
                &level.to_string(),
                &uptime_ms.to_string(),
                text,
            ],
        ),
    }
}

fn write_csv_row(output: &mut impl Write, cells: &[&str]) -> io::Result<()> {
    for (index, cell) in cells.iter().enumerate() {
        if index != 0 {
            write!(output, ",")?;
        }
        if cell.contains([',', '"', '\r', '\n']) {
            write!(output, "\"{}\"", cell.replace('"', "\"\""))?;
        } else {
            write!(output, "{cell}")?;
        }
    }
    writeln!(output)
}

pub(crate) fn usage() {
    eprintln!("  hc32-updater debug --serial <USB-serial> info");
    eprintln!(
        "  hc32-updater debug --serial <USB-serial> monitor [--period <100..5000 ms>] [--log] [--csv <file>] [--duration <seconds>]"
    );
    eprintln!(
        "  hc32-updater debug --serial <USB-serial> config list|get <key>|set <key> <value>|reset [--yes]"
    );
    eprintln!(
        "  hc32-updater debug --serial <USB-serial> reg-read <device 0..3> <register> <length 1..32>"
    );
    eprintln!("  hc32-updater debug --serial <USB-serial> stats");
    eprintln!("  hc32-updater debug --serial <USB-serial> reboot [--yes]");
    eprintln!(
        "  Debug keys: CONFIG_LIST name, decimal, or 0x hexadecimal; values: signed decimal or 0x hexadecimal."
    );
    eprintln!("  --yes skips confirmation for debug config set/reset and reboot only.");
}

#[cfg(test)]
mod tests {
    use super::*;
    use hc32_updater::debug_link::{TelemetryValue, Tlv};

    fn arguments(args: &[&str]) -> Result<Arguments, ()> {
        parse(args.iter().copied().map(String::from))
    }

    #[test]
    fn parses_all_debug_commands_and_key_formats() {
        let cases = [
            (vec!["info"], Command::Info),
            (vec!["stats"], Command::Stats),
            (vec!["config", "list"], Command::ConfigList),
            (
                vec!["config", "get", "0x0002"],
                Command::ConfigGet(Key::Number(2)),
            ),
            (
                vec!["config", "get", "2"],
                Command::ConfigGet(Key::Number(2)),
            ),
            (
                vec!["config", "get", "charge_enable"],
                Command::ConfigGet(Key::Name("charge_enable".into())),
            ),
            (
                vec!["config", "get", "2nd.setting-name"],
                Command::ConfigGet(Key::Name("2nd.setting-name".into())),
            ),
            (
                vec!["config", "set", "charge_current_limit", "-23"],
                Command::ConfigSet(Key::Name("charge_current_limit".into()), -23),
            ),
            (
                vec!["config", "set", "0X2", "0x1f4"],
                Command::ConfigSet(Key::Number(2), 500),
            ),
            (vec!["config", "reset"], Command::ConfigReset),
            (
                vec!["reg-read", "0x3", "0xff", "32"],
                Command::RegRead {
                    device: 3,
                    reg: 255,
                    len: 32,
                },
            ),
            (vec!["reboot"], Command::Reboot),
        ];
        for (mut args, expected) in cases {
            args.extend(["--serial", "USB-SN"]);
            let parsed = arguments(&args).unwrap();
            assert_eq!(parsed.serial, "USB-SN");
            assert!(!parsed.yes);
            assert_eq!(parsed.command, expected);
        }
    }

    #[test]
    fn parses_monitor_defaults_and_options() {
        assert_eq!(
            arguments(&["--serial", "SN", "monitor"]).unwrap().command,
            Command::Monitor(MonitorOptions {
                period_ms: 1000,
                log: false,
                csv: None,
                duration: None,
            })
        );
        assert_eq!(
            arguments(&[
                "monitor",
                "--period",
                "100",
                "--log",
                "--csv",
                "a,b.csv",
                "--duration",
                "10",
                "--serial",
                "SN",
            ])
            .unwrap()
            .command,
            Command::Monitor(MonitorOptions {
                period_ms: 100,
                log: true,
                csv: Some(PathBuf::from("a,b.csv")),
                duration: Some(Duration::from_secs(10)),
            })
        );
        assert!(arguments(&["monitor", "--period", "5000", "--serial", "SN"]).is_ok());
    }

    #[test]
    fn accepts_confirmation_override_only_for_mutations() {
        for args in [
            vec!["--yes", "config", "set", "2", "500"],
            vec!["config", "reset", "--yes"],
            vec!["reboot", "--yes"],
        ] {
            let mut args = args;
            args.extend(["--serial", "SN"]);
            assert!(arguments(&args).unwrap().yes);
        }
        assert_eq!(parse_value("-0x80000000"), Ok(i32::MIN));
        assert_eq!(parse_value("2147483647"), Ok(i32::MAX));
    }

    #[test]
    fn rejects_invalid_debug_arguments() {
        let cases: &[&[&str]] = &[
            &[],
            &["info"],
            &["--serial", "SN"],
            &["info", "--serial"],
            &["info", "--serial", ""],
            &["info", "--serial", "--yes"],
            &["--serial", "SN", "--serial", "SN", "info"],
            &["--serial", "SN", "info", "extra"],
            &["--serial", "SN", "unknown"],
            &["--serial", "SN", "info", "--yes"],
            &["--serial", "SN", "stats", "--yes"],
            &["--serial", "SN", "config", "list", "--yes"],
            &["--serial", "SN", "reboot", "--yes", "--yes"],
            &["--serial", "SN", "config"],
            &["--serial", "SN", "config", "get"],
            &["--serial", "SN", "config", "get", "65536"],
            &["--serial", "SN", "config", "get", "0x10000"],
            &["--serial", "SN", "config", "get", "-1"],
            &["--serial", "SN", "config", "get", ""],
            &["--serial", "SN", "config", "get", "\u{19}\u{19}\u{19}"],
            &["--serial", "SN", "config", "get", "0xwrong"],
            &["--serial", "SN", "config", "set", "1"],
            &["--serial", "SN", "config", "set", "1", "true"],
            &["--serial", "SN", "config", "set", "1", "2147483648"],
            &["--serial", "SN", "config", "set", "1", "-2147483649"],
            &["--serial", "SN", "config", "set", "1", "0x80000000"],
            &["--serial", "SN", "config", "reset", "extra"],
            &["--serial", "SN", "reg-read", "4", "0", "1"],
            &["--serial", "SN", "reg-read", "0", "256", "1"],
            &["--serial", "SN", "reg-read", "0", "0", "0"],
            &["--serial", "SN", "reg-read", "0", "0", "33"],
            &["--serial", "SN", "reg-read", "0", "0"],
            &["--serial", "SN", "monitor", "--period", "99"],
            &["--serial", "SN", "monitor", "--period", "5001"],
            &["--serial", "SN", "monitor", "--period", "0"],
            &["--serial", "SN", "monitor", "--period"],
            &["--serial", "SN", "monitor", "--duration", "0"],
            &["--serial", "SN", "monitor", "--duration", "-1"],
            &["--serial", "SN", "monitor", "--duration", "1.5"],
            &["--serial", "SN", "monitor", "--duration"],
            &["--serial", "SN", "monitor", "--csv"],
            &["--serial", "SN", "monitor", "--csv", ""],
            &["--serial", "SN", "monitor", "--log", "--log"],
            &[
                "--serial", "SN", "monitor", "--period", "100", "--period", "200",
            ],
            &[
                "--serial",
                "SN",
                "monitor",
                "--duration",
                "1",
                "--duration",
                "2",
            ],
            &["--serial", "SN", "monitor", "--csv", "a", "--csv", "b"],
            &["--serial", "SN", "monitor", "--unknown"],
        ];
        for args in cases {
            assert!(
                arguments(args).is_err(),
                "accepted invalid arguments: {args:?}"
            );
        }
    }

    #[test]
    fn escapes_csv_fields_and_preserves_event_metadata() {
        let mut csv = Vec::new();
        write_csv_event(
            &mut csv,
            Duration::from_millis(12),
            &Event::Log {
                sequence: 7,
                level: 2,
                uptime_ms: 34,
                text: "line,\"quoted\"\r\nnext".into(),
            },
        )
        .unwrap();
        assert_eq!(
            String::from_utf8(csv).unwrap(),
            "12,log,7,,,2,34,\"line,\"\"quoted\"\"\r\nnext\"\n"
        );
        let mut csv = Vec::new();
        write_csv_event(
            &mut csv,
            Duration::ZERO,
            &Event::Telemetry {
                sequence: 8,
                fields: vec![Tlv {
                    tag: 1,
                    value: TelemetryValue::U32(99),
                }],
            },
        )
        .unwrap();
        assert_eq!(
            String::from_utf8(csv).unwrap(),
            "0,telemetry,8,0x01,99,,,\n"
        );
    }
}
