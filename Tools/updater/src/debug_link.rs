//! Debug Link V1 frame codec and serialized maintenance client.
use crate::{Transport, crc32};
use std::collections::VecDeque;
use std::fmt;
use std::io;
use std::time::{Duration, Instant};

pub const MAX_PAYLOAD: usize = 256;
const MAX_QUEUED_EVENTS: usize = 256;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Command {
    Hello = 0x01,
    TelemetryGet = 0x02,
    StreamSet = 0x03,
    ConfigList = 0x10,
    ConfigGet = 0x11,
    ConfigSet = 0x12,
    ConfigReset = 0x13,
    Unlock = 0x20,
    RegRead = 0x30,
    Reboot = 0x40,
    Stats = 0x41,
    EvtTelemetry = 0x80,
    EvtLog = 0x81,
}

impl TryFrom<u8> for Command {
    type Error = Error;
    fn try_from(value: u8) -> Result<Self, Error> {
        Ok(match value {
            1 => Self::Hello,
            2 => Self::TelemetryGet,
            3 => Self::StreamSet,
            0x10 => Self::ConfigList,
            0x11 => Self::ConfigGet,
            0x12 => Self::ConfigSet,
            0x13 => Self::ConfigReset,
            0x20 => Self::Unlock,
            0x30 => Self::RegRead,
            0x40 => Self::Reboot,
            0x41 => Self::Stats,
            0x80 => Self::EvtTelemetry,
            0x81 => Self::EvtLog,
            _ => return Err(Error::Invalid("unknown command")),
        })
    }
}
impl Command {
    fn is_event(self) -> bool {
        matches!(self, Self::EvtTelemetry | Self::EvtLog)
    }
    fn idempotent(self) -> bool {
        !matches!(self, Self::ConfigSet | Self::ConfigReset | Self::Reboot)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum Status {
    Ok = 0,
    BadFrame = 1,
    UnknownCommand = 2,
    Locked = 3,
    UnknownKey = 4,
    OutOfRange = 5,
    StorageError = 6,
    DeviceError = 7,
    Busy = 8,
    InternalError = 9,
}
impl TryFrom<u16> for Status {
    type Error = Error;
    fn try_from(value: u16) -> Result<Self, Error> {
        Ok(match value {
            0 => Self::Ok,
            1 => Self::BadFrame,
            2 => Self::UnknownCommand,
            3 => Self::Locked,
            4 => Self::UnknownKey,
            5 => Self::OutOfRange,
            6 => Self::StorageError,
            7 => Self::DeviceError,
            8 => Self::Busy,
            9 => Self::InternalError,
            _ => return Err(Error::Invalid("unknown status")),
        })
    }
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    Invalid(&'static str),
    Device(Status),
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(error) => error.fmt(f),
            Self::Invalid(message) => write!(f, "Debug Link: {message}"),
            Self::Device(status) => write!(f, "Debug Link device status: {status:?}"),
        }
    }
}
impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            _ => None,
        }
    }
}
impl From<io::Error> for Error {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Frame {
    pub command: Command,
    pub flags: u8,
    pub sequence: u32,
    pub payload: Vec<u8>,
}
impl Frame {
    pub fn encode(&self) -> Result<Vec<u8>, Error> {
        self.validate()?;
        let mut bytes = Vec::with_capacity(20 + self.payload.len());
        bytes.extend_from_slice(b"DBG1");
        bytes.extend_from_slice(&[1, 0, self.command as u8, self.flags]);
        bytes.extend_from_slice(&self.sequence.to_le_bytes());
        bytes.extend_from_slice(&(self.payload.len() as u16).to_le_bytes());
        bytes.extend_from_slice(&[0, 0]);
        bytes.extend_from_slice(&self.payload);
        bytes.extend_from_slice(&crc32(&bytes).to_le_bytes());
        Ok(bytes)
    }
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        if bytes.len() < 20 || &bytes[..4] != b"DBG1" {
            return Err(Error::Invalid("invalid frame length or magic"));
        }
        if bytes[4..6] != [1, 0] {
            return Err(Error::Invalid("unsupported version"));
        }
        if bytes[14..16] != [0, 0] {
            return Err(Error::Invalid("nonzero reserved field"));
        }
        let length = usize::from(u16_at(bytes, 12));
        if length > MAX_PAYLOAD || bytes.len() != length + 20 {
            return Err(Error::Invalid("invalid payload length"));
        }
        if crc32(&bytes[..16 + length]) != u32_at(bytes, 16 + length) {
            return Err(Error::Invalid("CRC mismatch"));
        }
        let frame = Self {
            command: bytes[6].try_into()?,
            flags: bytes[7],
            sequence: u32_at(bytes, 8),
            payload: bytes[16..16 + length].to_vec(),
        };
        frame.validate()?;
        Ok(frame)
    }
    fn validate(&self) -> Result<(), Error> {
        if self.payload.len() > MAX_PAYLOAD {
            return Err(Error::Invalid("payload too large"));
        }
        if !matches!(self.flags, 0 | 1 | 3 | 4) || self.command.is_event() != (self.flags == 4) {
            return Err(Error::Invalid("invalid flags for command"));
        }
        if self.flags == 0 {
            let bytes = &self.payload;
            match self.command {
                Command::Hello | Command::TelemetryGet | Command::ConfigReset | Command::Stats => {
                    exact(bytes, 0)?
                }
                Command::ConfigList => exact(bytes, 1)?,
                Command::ConfigGet => exact(bytes, 2)?,
                Command::ConfigSet => exact(bytes, 6)?,
                Command::StreamSet => {
                    exact(bytes, 6)?;
                    let period = u16_at(bytes, 0);
                    if (period != 0 && !(100..=5000).contains(&period))
                        || u32_at(bytes, 2) & !3 != 0
                    {
                        return Err(Error::Invalid("invalid stream period or mask"));
                    }
                }
                Command::Unlock => {
                    if bytes.is_empty()
                        || bytes[0] == 0
                        || bytes.len() != 1 + usize::from(bytes[0])
                        || !bytes[1..].is_ascii()
                    {
                        return Err(Error::Invalid("invalid unlock serial"));
                    }
                }
                Command::RegRead => {
                    exact(bytes, 3)?;
                    if bytes[0] > 3 || !(1..=32).contains(&bytes[2]) {
                        return Err(Error::Invalid("invalid register device or length"));
                    }
                }
                Command::Reboot => {
                    exact(bytes, 1)?;
                    if bytes[0] != 0 {
                        return Err(Error::Invalid("unknown reboot mode"));
                    }
                }
                Command::EvtTelemetry | Command::EvtLog => unreachable!(),
            }
        }
        if matches!(self.flags, 1 | 3) {
            if self.payload.len() < 2 {
                return Err(Error::Invalid("missing response status"));
            }
            let status = Status::try_from(u16_at(&self.payload, 0))?;
            if (status != Status::Ok) != (self.flags == 3)
                || (status != Status::Ok && self.payload.len() != 2)
            {
                return Err(Error::Invalid("invalid error response"));
            }
        }
        Ok(())
    }
}
fn u16_at(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap())
}
fn u32_at(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}
fn i32_at(bytes: &[u8], offset: usize) -> i32 {
    i32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}
fn exact(bytes: &[u8], length: usize) -> Result<(), Error> {
    if bytes.len() == length {
        Ok(())
    } else {
        Err(Error::Invalid("unexpected response body length"))
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HelloInfo {
    pub major: u8,
    pub minor: u8,
    pub max_payload: u16,
    pub capabilities: u32,
    pub telemetry_version: u16,
}
impl HelloInfo {
    pub fn decode(bytes: &[u8]) -> Result<Self, Error> {
        exact(bytes, 10)?;
        if bytes[..2] != [1, 0] || u16_at(bytes, 2) != MAX_PAYLOAD as u16 {
            return Err(Error::Invalid("unsupported HELLO version or payload limit"));
        }
        Ok(Self {
            major: bytes[0],
            minor: bytes[1],
            max_payload: u16_at(bytes, 2),
            capabilities: u32_at(bytes, 4),
            telemetry_version: u16_at(bytes, 8),
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BqIdentity {
    pub device_type: u16,
    pub device_number: u16,
    pub firmware_version: u16,
    pub firmware_build: u16,
    pub firmware_type: u8,
    pub impedance_track_version: u16,
    pub reserved: u8,
    pub execution_context: u8,
    pub hardware_version: u16,
    pub chemistry_id: u16,
}

/// BQ40Z50 Smart Battery Data words carried by telemetry tags 0x50/0x51.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BatteryStatus {
    pub battery_mode: u16,
    pub voltage_mv: u16,
    /// Positive while charging.
    pub current_ma: i16,
    pub temperature_dk: u16,
    pub relative_soc_percent: u16,
    pub remaining_capacity: u16,
    pub full_charge_capacity: u16,
    pub charging_voltage_mv: u16,
    pub charging_current_ma: u16,
    pub battery_status: u16,
    pub cycle_count: u16,
}
impl BatteryStatus {
    /// BatteryStatus alarm bits from the SBS 1.1 specification.
    const ALARMS: [(u16, &'static str); 6] = [
        (0x8000, "over-charged"),
        (0x4000, "terminate charge"),
        (0x1000, "over-temperature"),
        (0x0800, "terminate discharge"),
        (0x0200, "remaining capacity"),
        (0x0100, "remaining time"),
    ];

    /// BatteryMode CAPM switches capacities from mAh to 10 mWh units.
    pub fn capacity_in_mwh(&self) -> bool {
        self.battery_mode & 0x8000 != 0
    }
    /// Returns (remaining, full, unit) with the CAPM scaling applied.
    pub fn capacity(&self) -> (u32, u32, &'static str) {
        if self.capacity_in_mwh() {
            (
                u32::from(self.remaining_capacity) * 10,
                u32::from(self.full_charge_capacity) * 10,
                "mWh",
            )
        } else {
            (
                u32::from(self.remaining_capacity),
                u32::from(self.full_charge_capacity),
                "mAh",
            )
        }
    }
    /// Terminal power in milliwatts; positive while charging.
    pub fn power_mw(&self) -> i32 {
        i32::from(self.voltage_mv) * i32::from(self.current_ma) / 1000
    }
    pub fn temperature_c(&self) -> f32 {
        f32::from(self.temperature_dk) / 10.0 - 273.15
    }
    pub fn state(&self) -> &'static str {
        if self.battery_status & 0x0020 != 0 {
            "Full"
        } else if self.current_ma > 0 {
            "Charging"
        } else if self.current_ma < 0 || self.battery_status & 0x0040 != 0 {
            "Discharging"
        } else {
            "Idle"
        }
    }
    pub fn alarms(&self) -> Vec<&'static str> {
        Self::ALARMS
            .iter()
            .filter(|(bit, _)| self.battery_status & bit != 0)
            .map(|(_, name)| *name)
            .collect()
    }
}

/// Raw MP2762A registers carried by telemetry tag 0x12.
///
/// ADC words hold a 10-bit value in bits 15:6. The LSB weights come from the earlier `Libraries/Mp2762a` driver and
/// still have to be confirmed against the datasheet and cross-checked with the BQ40Z50 on hardware.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ChargerStatus {
    pub status: u8,
    pub fault: u8,
    pub battery_voltage: u16,
    pub system_voltage: u16,
    pub charge_current: u16,
    pub input_voltage: u16,
    pub input_current: u16,
}
impl ChargerStatus {
    fn adc(word: u16, lsb: f32) -> f32 {
        f32::from(word >> 6) * lsb
    }
    pub fn battery_voltage_mv(&self) -> f32 {
        Self::adc(self.battery_voltage, 12.5)
    }
    pub fn system_voltage_mv(&self) -> f32 {
        Self::adc(self.system_voltage, 12.5)
    }
    pub fn charge_current_ma(&self) -> f32 {
        Self::adc(self.charge_current, 12.5)
    }
    pub fn input_voltage_mv(&self) -> f32 {
        Self::adc(self.input_voltage, 25.0)
    }
    pub fn input_current_ma(&self) -> f32 {
        Self::adc(self.input_current, 6.25)
    }
    pub fn input_power_mw(&self) -> f32 {
        self.input_voltage_mv() * self.input_current_ma() / 1000.0
    }
    /// Charge state field, status register bits 3:2.
    pub fn charge_state(&self) -> &'static str {
        match (self.status >> 2) & 0x03 {
            0 => "Not charging",
            1 => "Pre-charge",
            2 => "Fast charge",
            _ => "Charge done",
        }
    }
}

/// Names the RMU_RSTF0 flags in bit order. HC32F460 keeps them latched until software clears them, so more than one
/// can be set; bit 14 marks that several causes were recorded.
pub fn reset_cause_names(flags: u32) -> Vec<&'static str> {
    const NAMES: [&str; 15] = [
        "Power-on",
        "Reset pin",
        "Brown-out",
        "PVD1",
        "PVD2",
        "Watchdog",
        "Special watchdog",
        "Power-down",
        "Software",
        "MPU error",
        "RAM parity error",
        "RAM ECC error",
        "Clock frequency error",
        "XTAL failure",
        "Multiple causes",
    ];
    NAMES
        .iter()
        .enumerate()
        .filter(|(bit, _)| flags & (1 << bit) != 0)
        .map(|(_, name)| *name)
        .collect()
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TelemetryValue {
    U8(u8),
    U32(u32),
    Identity(BqIdentity),
    Battery(BatteryStatus),
    Charger(ChargerStatus),
    Bytes(Vec<u8>),
}
impl fmt::Display for TelemetryValue {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::U8(value) => value.fmt(f),
            Self::U32(value) => value.fmt(f),
            Self::Identity(value) => write!(
                f,
                "device_type=0x{:04x} device_number=0x{:04x} firmware_version=0x{:04x} firmware_build={} firmware_type={} impedance_track_version=0x{:04x} reserved={} execution_context={} hardware_version=0x{:04x} chemistry_id=0x{:04x}",
                value.device_type,
                value.device_number,
                value.firmware_version,
                value.firmware_build,
                value.firmware_type,
                value.impedance_track_version,
                value.reserved,
                value.execution_context,
                value.hardware_version,
                value.chemistry_id
            ),
            Self::Battery(value) => {
                let (remaining, full, unit) = value.capacity();
                write!(
                    f,
                    "state={} soc={}% capacity={remaining}/{full}{unit} voltage={}mV current={}mA power={}mW temperature={:.1}C charge_target={}mV/{}mA cycles={} status=0x{:04x}",
                    value.state(),
                    value.relative_soc_percent,
                    value.voltage_mv,
                    value.current_ma,
                    value.power_mw(),
                    value.temperature_c(),
                    value.charging_voltage_mv,
                    value.charging_current_ma,
                    value.cycle_count,
                    value.battery_status
                )?;
                for alarm in value.alarms() {
                    write!(f, " alarm={alarm}")?;
                }
                Ok(())
            }
            Self::Charger(value) => write!(
                f,
                "state={} input={:.0}mV/{:.0}mA power={:.0}mW battery={:.0}mV system={:.0}mV charge_current={:.0}mA status=0x{:02x} fault=0x{:02x}",
                value.charge_state(),
                value.input_voltage_mv(),
                value.input_current_ma(),
                value.input_power_mw(),
                value.battery_voltage_mv(),
                value.system_voltage_mv(),
                value.charge_current_ma(),
                value.status,
                value.fault
            ),
            Self::Bytes(bytes) => {
                for byte in bytes {
                    write!(f, "{byte:02x}")?;
                }
                Ok(())
            }
        }
    }
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Tlv {
    pub tag: u8,
    pub value: TelemetryValue,
}
impl Tlv {
    pub fn name(&self) -> String {
        match self.tag {
            1 => "uptime_ms",
            2 => "loop_count",
            0x10 => "power_probe_mask",
            0x11 => "bq_identity",
            0x12 => "charger",
            0x50 => "battery_0x0b",
            0x51 => "battery_0x0c",
            0x20 => "charger_pg",
            0x21 => "charger_int",
            0x22 => "charger_change_count",
            0x30 => "update_manager_state",
            0x31 => "usb_update_errors",
            0x40 => "debug_events_dropped",
            0x41 => "last_reset_cause",
            _ => return format!("tag_0x{:02x}", self.tag),
        }
        .to_owned()
    }
}
impl fmt::Display for Tlv {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}={}", self.name(), self.value)
    }
}
pub fn decode_tlvs(mut bytes: &[u8]) -> Result<Vec<Tlv>, Error> {
    let mut fields = Vec::new();
    while !bytes.is_empty() {
        if bytes.len() < 2 || bytes.len() < 2 + usize::from(bytes[1]) {
            return Err(Error::Invalid("truncated TLV"));
        }
        let tag = bytes[0];
        let length = usize::from(bytes[1]);
        let value = &bytes[2..2 + length];
        let value = match tag {
            1 | 2 | 0x22 | 0x31 | 0x40 | 0x41 => {
                exact(value, 4)?;
                TelemetryValue::U32(u32_at(value, 0))
            }
            0x10 | 0x20 | 0x21 | 0x30 => {
                exact(value, 1)?;
                TelemetryValue::U8(value[0])
            }
            0x11 => {
                exact(value, 17)?;
                TelemetryValue::Identity(BqIdentity {
                    device_type: u16_at(value, 0),
                    device_number: u16_at(value, 2),
                    firmware_version: u16_at(value, 4),
                    firmware_build: u16_at(value, 6),
                    firmware_type: value[8],
                    impedance_track_version: u16_at(value, 9),
                    reserved: value[11],
                    execution_context: value[12],
                    hardware_version: u16_at(value, 13),
                    chemistry_id: u16_at(value, 15),
                })
            }
            0x12 => {
                exact(value, 12)?;
                TelemetryValue::Charger(ChargerStatus {
                    status: value[0],
                    fault: value[1],
                    battery_voltage: u16_at(value, 2),
                    system_voltage: u16_at(value, 4),
                    charge_current: u16_at(value, 6),
                    input_voltage: u16_at(value, 8),
                    input_current: u16_at(value, 10),
                })
            }
            0x50 | 0x51 => {
                exact(value, 22)?;
                let word = |index: usize| u16_at(value, index * 2);
                TelemetryValue::Battery(BatteryStatus {
                    battery_mode: word(0),
                    voltage_mv: word(1),
                    current_ma: word(2) as i16,
                    temperature_dk: word(3),
                    relative_soc_percent: word(4),
                    remaining_capacity: word(5),
                    full_charge_capacity: word(6),
                    charging_voltage_mv: word(7),
                    charging_current_ma: word(8),
                    battery_status: word(9),
                    cycle_count: word(10),
                })
            }
            _ => TelemetryValue::Bytes(value.to_vec()),
        };
        fields.push(Tlv { tag, value });
        bytes = &bytes[2 + length..];
    }
    Ok(fields)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ValueType {
    I32,
    Bool,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Unit {
    None,
    Milliamp,
    Millivolt,
    Millisecond,
    DeciCelsius,
}
impl fmt::Display for Unit {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::None => "",
            Self::Milliamp => "mA",
            Self::Millivolt => "mV",
            Self::Millisecond => "ms",
            Self::DeciCelsius => "0.1 degC",
        })
    }
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConfigEntry {
    pub key: u16,
    pub value_type: ValueType,
    pub flags: u8,
    pub min: i32,
    pub max: i32,
    pub default: i32,
    pub current: i32,
    pub unit: Unit,
    pub name: String,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConfigPage {
    pub total: u8,
    pub entries: Vec<ConfigEntry>,
}
impl ConfigPage {
    pub fn decode(bytes: &[u8], start: u8) -> Result<Self, Error> {
        if bytes.len() < 2 {
            return Err(Error::Invalid("truncated config page"));
        }
        let total = bytes[0];
        let count = bytes[1];
        if u16::from(start) + u16::from(count) > u16::from(total)
            || (start < total && count == 0)
            || start > total
        {
            return Err(Error::Invalid("invalid config page progress"));
        }
        let mut remaining = &bytes[2..];
        let mut entries = Vec::with_capacity(usize::from(count));
        for _ in 0..count {
            if remaining.len() < 22 {
                return Err(Error::Invalid("truncated config entry"));
            }
            let length = usize::from(remaining[21]);
            if remaining.len() < 22 + length || !remaining[22..22 + length].is_ascii() {
                return Err(Error::Invalid("invalid config name"));
            }
            let value_type = match remaining[2] {
                0 => ValueType::I32,
                1 => ValueType::Bool,
                _ => return Err(Error::Invalid("unknown config type")),
            };
            if remaining[3] & !7 != 0 {
                return Err(Error::Invalid("reserved config flags"));
            }
            let unit = match remaining[20] {
                0 => Unit::None,
                1 => Unit::Milliamp,
                2 => Unit::Millivolt,
                3 => Unit::Millisecond,
                4 => Unit::DeciCelsius,
                _ => return Err(Error::Invalid("unknown config unit")),
            };
            let entry = ConfigEntry {
                key: u16_at(remaining, 0),
                value_type,
                flags: remaining[3],
                min: i32_at(remaining, 4),
                max: i32_at(remaining, 8),
                default: i32_at(remaining, 12),
                current: i32_at(remaining, 16),
                unit,
                name: String::from_utf8(remaining[22..22 + length].to_vec()).unwrap(),
            };
            if entry.min > entry.max
                || !(entry.min..=entry.max).contains(&entry.default)
                || !(entry.min..=entry.max).contains(&entry.current)
                || (value_type == ValueType::Bool && (entry.min < 0 || entry.max > 1))
            {
                return Err(Error::Invalid("invalid config range"));
            }
            entries.push(entry);
            remaining = &remaining[22 + length..];
        }
        exact(remaining, 0)?;
        Ok(Self { total, entries })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum Event {
    Telemetry {
        sequence: u32,
        fields: Vec<Tlv>,
    },
    Log {
        sequence: u32,
        level: u8,
        uptime_ms: u32,
        text: String,
    },
}
impl Event {
    pub fn decode(frame: Frame) -> Result<Self, Error> {
        if frame.flags != 4 {
            return Err(Error::Invalid("expected event"));
        }
        match frame.command {
            Command::EvtTelemetry => Ok(Self::Telemetry {
                sequence: frame.sequence,
                fields: decode_tlvs(&frame.payload)?,
            }),
            Command::EvtLog => {
                if frame.payload.len() < 6 {
                    return Err(Error::Invalid("truncated log event"));
                }
                exact(&frame.payload, 6 + usize::from(frame.payload[5]))?;
                Ok(Self::Log {
                    sequence: frame.sequence,
                    level: frame.payload[0],
                    uptime_ms: u32_at(&frame.payload, 1),
                    text: String::from_utf8_lossy(&frame.payload[6..]).into_owned(),
                })
            }
            _ => Err(Error::Invalid("expected event command")),
        }
    }
}

pub struct DebugClient<T: Transport> {
    transport: T,
    timeout: Duration,
    sequence: u32,
    events: VecDeque<Event>,
    pub events_dropped: u64,
}
impl<T: Transport> DebugClient<T> {
    pub fn new(transport: T, timeout: Duration) -> Self {
        Self {
            transport,
            timeout,
            sequence: 0,
            events: VecDeque::new(),
            events_dropped: 0,
        }
    }
    fn queue(&mut self, event: Event) {
        if self.events.len() == MAX_QUEUED_EVENTS {
            self.events.pop_front();
            self.events_dropped += 1;
        }
        self.events.push_back(event);
    }
    fn request(&mut self, command: Command, payload: Vec<u8>) -> Result<Vec<u8>, Error> {
        self.sequence = self
            .sequence
            .checked_add(1)
            .ok_or(Error::Invalid("sequence exhausted; reconnect"))?;
        let sequence = self.sequence;
        let bytes = Frame {
            command,
            flags: 0,
            sequence,
            payload,
        }
        .encode()?;
        let attempts = if command.idempotent() { 3 } else { 1 };
        for attempt in 0..attempts {
            let started = Instant::now();
            let result = self
                .transport
                .send(&bytes, self.timeout)
                .map_err(Error::from)
                .and_then(|()| {
                    loop {
                        let remaining = self.timeout.saturating_sub(started.elapsed());
                        if remaining.is_zero() {
                            return Err(Error::Io(io::Error::new(
                                io::ErrorKind::TimedOut,
                                "debug response timeout",
                            )));
                        }
                        let frame = Frame::decode(&self.transport.receive(remaining)?)?;
                        if frame.flags == 4 {
                            self.queue(Event::decode(frame)?);
                            continue;
                        }
                        if !matches!(frame.flags, 1 | 3) {
                            return Err(Error::Invalid("expected response"));
                        }
                        if frame.sequence < sequence {
                            continue;
                        }
                        if frame.sequence != sequence || frame.command != command {
                            return Err(Error::Invalid("response command or sequence mismatch"));
                        }
                        let status = Status::try_from(u16_at(&frame.payload, 0))?;
                        if status != Status::Ok {
                            return Err(Error::Device(status));
                        }
                        return Ok(frame.payload[2..].to_vec());
                    }
                });
            if matches!(&result, Err(Error::Io(error)) if matches!(error.kind(), io::ErrorKind::TimedOut | io::ErrorKind::Interrupted))
                && attempt + 1 < attempts
            {
                continue;
            }
            return result;
        }
        unreachable!()
    }
    pub fn hello(&mut self) -> Result<HelloInfo, Error> {
        HelloInfo::decode(&self.request(Command::Hello, vec![])?)
    }
    pub fn telemetry(&mut self) -> Result<Vec<Tlv>, Error> {
        decode_tlvs(&self.request(Command::TelemetryGet, vec![])?)
    }
    pub fn stats(&mut self) -> Result<Vec<Tlv>, Error> {
        decode_tlvs(&self.request(Command::Stats, vec![])?)
    }
    pub fn stream_set(&mut self, period_ms: u16, mask: u32) -> Result<(), Error> {
        if (period_ms != 0 && !(100..=5000).contains(&period_ms)) || mask & !3 != 0 {
            return Err(Error::Invalid("invalid stream period or mask"));
        }
        let mut payload = period_ms.to_le_bytes().to_vec();
        payload.extend_from_slice(&mask.to_le_bytes());
        exact(&self.request(Command::StreamSet, payload)?, 0)
    }
    pub fn config_list(&mut self) -> Result<Vec<ConfigEntry>, Error> {
        let mut entries: Vec<ConfigEntry> = Vec::new();
        let mut total = None;
        loop {
            let start = u8::try_from(entries.len())
                .map_err(|_| Error::Invalid("config table too large"))?;
            let page = ConfigPage::decode(&self.request(Command::ConfigList, vec![start])?, start)?;
            if total.is_some_and(|total| total != page.total) {
                return Err(Error::Invalid("config table changed during paging"));
            }
            total = Some(page.total);
            for entry in page.entries {
                if entries.iter().any(|old| old.key == entry.key) {
                    return Err(Error::Invalid("duplicate config key"));
                }
                entries.push(entry);
            }
            if entries.len() == usize::from(page.total) {
                return Ok(entries);
            }
        }
    }
    pub fn config_get(&mut self, key: u16) -> Result<i32, Error> {
        let bytes = self.request(Command::ConfigGet, key.to_le_bytes().to_vec())?;
        exact(&bytes, 4)?;
        Ok(i32_at(&bytes, 0))
    }
    pub fn config_set(&mut self, key: u16, value: i32) -> Result<i32, Error> {
        let mut payload = key.to_le_bytes().to_vec();
        payload.extend_from_slice(&value.to_le_bytes());
        let bytes = self.request(Command::ConfigSet, payload)?;
        exact(&bytes, 4)?;
        Ok(i32_at(&bytes, 0))
    }
    pub fn config_reset(&mut self) -> Result<(), Error> {
        exact(&self.request(Command::ConfigReset, vec![])?, 0)
    }
    pub fn unlock(&mut self, serial: &str) -> Result<u16, Error> {
        if serial.is_empty() || serial.len() > MAX_PAYLOAD - 1 || !serial.is_ascii() {
            return Err(Error::Invalid("invalid unlock serial"));
        }
        let mut payload = vec![serial.len() as u8];
        payload.extend_from_slice(serial.as_bytes());
        let bytes = self.request(Command::Unlock, payload)?;
        exact(&bytes, 2)?;
        Ok(u16_at(&bytes, 0))
    }
    pub fn reg_read(&mut self, device: u8, reg: u8, len: u8) -> Result<Vec<u8>, Error> {
        if device > 3 || !(1..=32).contains(&len) {
            return Err(Error::Invalid("invalid register device or length"));
        }
        let bytes = self.request(Command::RegRead, vec![device, reg, len])?;
        exact(&bytes, usize::from(len))?;
        Ok(bytes)
    }
    pub fn reboot(&mut self) -> Result<(), Error> {
        exact(&self.request(Command::Reboot, vec![0])?, 0)
    }
    pub fn poll_event(&mut self, timeout: Duration) -> Result<Option<Event>, Error> {
        if let Some(event) = self.events.pop_front() {
            return Ok(Some(event));
        }
        let started = Instant::now();
        loop {
            let remaining = timeout.saturating_sub(started.elapsed());
            if remaining.is_zero() {
                return Ok(None);
            }
            match self.transport.receive(remaining) {
                Ok(bytes) => {
                    let frame = Frame::decode(&bytes)?;
                    if frame.flags == 4 {
                        return Event::decode(frame).map(Some);
                    }
                    if matches!(frame.flags, 1 | 3) && frame.sequence <= self.sequence {
                        continue;
                    }
                    return Err(Error::Invalid("unexpected frame while polling events"));
                }
                // nusb reports an expired blocking transfer as Cancelled, mapped to Interrupted.
                Err(error)
                    if matches!(
                        error.kind(),
                        io::ErrorKind::TimedOut | io::ErrorKind::Interrupted
                    ) =>
                {
                    return Ok(None);
                }
                Err(error) => return Err(error.into()),
            }
        }
    }
}
