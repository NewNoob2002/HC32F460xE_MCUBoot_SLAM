use std::fmt;

pub const HEADER_SIZE: usize = 16;
pub const MAX_PAYLOAD: usize = 512;
pub const CRC_SIZE: usize = 4;
pub const VERSION_MAJOR: u8 = 1;
pub const VERSION_MINOR: u8 = 0;

const MAGIC: &[u8; 4] = b"FWUP";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum Command {
    Hello = 0x01,
    DeviceInfo = 0x02,
    ProductConfigGet = 0x03,
    ProductConfigSet = 0x04,
    Begin = 0x10,
    Data = 0x11,
    End = 0x12,
    Commit = 0x13,
    Abort = 0x14,
}

impl TryFrom<u8> for Command {
    type Error = ProtocolError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0x01 => Ok(Self::Hello),
            0x02 => Ok(Self::DeviceInfo),
            0x03 => Ok(Self::ProductConfigGet),
            0x04 => Ok(Self::ProductConfigSet),
            0x10 => Ok(Self::Begin),
            0x11 => Ok(Self::Data),
            0x12 => Ok(Self::End),
            0x13 => Ok(Self::Commit),
            0x14 => Ok(Self::Abort),
            _ => Err(ProtocolError::UnknownCommand(value)),
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum Status {
    Ok = 0,
    BadFrame = 1,
    IncompatibleVersion = 2,
    UnsupportedCommand = 3,
    BadSequence = 4,
    InvalidState = 5,
    InvalidArgument = 6,
    ImageTooLarge = 7,
    OffsetMismatch = 8,
    StorageError = 9,
    VerifyError = 10,
    BootControlError = 11,
    Timeout = 12,
    InternalError = 13,
}

impl Status {
    pub fn advances_sequence(self) -> bool {
        !matches!(
            self,
            Self::BadFrame | Self::IncompatibleVersion | Self::BadSequence
        )
    }
}

impl TryFrom<u16> for Status {
    type Error = ProtocolError;

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(Self::Ok),
            1 => Ok(Self::BadFrame),
            2 => Ok(Self::IncompatibleVersion),
            3 => Ok(Self::UnsupportedCommand),
            4 => Ok(Self::BadSequence),
            5 => Ok(Self::InvalidState),
            6 => Ok(Self::InvalidArgument),
            7 => Ok(Self::ImageTooLarge),
            8 => Ok(Self::OffsetMismatch),
            9 => Ok(Self::StorageError),
            10 => Ok(Self::VerifyError),
            11 => Ok(Self::BootControlError),
            12 => Ok(Self::Timeout),
            13 => Ok(Self::InternalError),
            _ => Err(ProtocolError::UnknownStatus(value)),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Frame {
    pub major: u8,
    pub minor: u8,
    pub command: Command,
    pub flags: u8,
    pub sequence: u32,
    pub payload: Vec<u8>,
}

impl Frame {
    pub fn request(command: Command, sequence: u32, payload: Vec<u8>) -> Self {
        Self {
            major: VERSION_MAJOR,
            minor: VERSION_MINOR,
            command,
            flags: 0,
            sequence,
            payload,
        }
    }

    pub fn encode(&self) -> Result<Vec<u8>, ProtocolError> {
        if self.payload.len() > MAX_PAYLOAD {
            return Err(ProtocolError::PayloadTooLarge(self.payload.len()));
        }

        let mut output = Vec::with_capacity(HEADER_SIZE + self.payload.len() + CRC_SIZE);
        output.extend_from_slice(MAGIC);
        output.extend_from_slice(&[self.major, self.minor, self.command as u8, self.flags]);
        output.extend_from_slice(&self.sequence.to_le_bytes());
        output.extend_from_slice(&(self.payload.len() as u16).to_le_bytes());
        output.extend_from_slice(&[0, 0]);
        output.extend_from_slice(&self.payload);
        output.extend_from_slice(&crc32(&output).to_le_bytes());
        Ok(output)
    }

    pub fn decode(input: &[u8]) -> Result<Self, ProtocolError> {
        if input.len() < HEADER_SIZE + CRC_SIZE {
            return Err(ProtocolError::InvalidLength(input.len()));
        }
        if &input[..4] != MAGIC {
            return Err(ProtocolError::BadMagic);
        }
        if input[14] != 0 || input[15] != 0 {
            return Err(ProtocolError::ReservedField);
        }

        let payload_length = u16::from_le_bytes([input[12], input[13]]) as usize;
        if payload_length > MAX_PAYLOAD {
            return Err(ProtocolError::PayloadTooLarge(payload_length));
        }
        let expected_length = HEADER_SIZE + payload_length + CRC_SIZE;
        if input.len() != expected_length {
            return Err(ProtocolError::InvalidLength(input.len()));
        }

        let expected_crc = u32::from_le_bytes(
            input[expected_length - CRC_SIZE..]
                .try_into()
                .expect("CRC slice has fixed length"),
        );
        if crc32(&input[..expected_length - CRC_SIZE]) != expected_crc {
            return Err(ProtocolError::Crc);
        }

        Ok(Self {
            major: input[4],
            minor: input[5],
            command: input[6].try_into()?,
            flags: input[7],
            sequence: u32::from_le_bytes(
                input[8..12]
                    .try_into()
                    .expect("sequence slice has fixed length"),
            ),
            payload: input[HEADER_SIZE..HEADER_SIZE + payload_length].to_vec(),
        })
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ProtocolError {
    InvalidLength(usize),
    PayloadTooLarge(usize),
    BadMagic,
    ReservedField,
    Crc,
    UnknownCommand(u8),
    UnknownStatus(u16),
    UnexpectedResponse { command: u8, sequence: u32 },
}

impl ProtocolError {
    pub fn retryable(&self) -> bool {
        matches!(self, Self::InvalidLength(_) | Self::BadMagic | Self::Crc)
    }
}

impl fmt::Display for ProtocolError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidLength(length) => write!(formatter, "invalid frame length {length}"),
            Self::PayloadTooLarge(length) => write!(formatter, "payload too large: {length}"),
            Self::BadMagic => formatter.write_str("invalid frame magic"),
            Self::ReservedField => formatter.write_str("reserved header field is not zero"),
            Self::Crc => formatter.write_str("frame CRC mismatch"),
            Self::UnknownCommand(command) => write!(formatter, "unknown command 0x{command:02x}"),
            Self::UnknownStatus(status) => write!(formatter, "unknown status {status}"),
            Self::UnexpectedResponse { command, sequence } => {
                write!(
                    formatter,
                    "unexpected response command 0x{command:02x} sequence {sequence}"
                )
            }
        }
    }
}

impl std::error::Error for ProtocolError {}

pub fn crc32(input: &[u8]) -> u32 {
    let mut crc = u32::MAX;
    for byte in input {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(crc & 1);
            crc = (crc >> 1) ^ (0xedb8_8320 & mask);
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn protocol_golden_vectors_round_trip() {
        let csv = include_str!("../../../Tests/Protocol/golden_vectors.csv");
        for line in csv.lines().skip(1).filter(|line| !line.is_empty()) {
            let fields: Vec<_> = line.split(',').collect();
            assert_eq!(fields.len(), 8, "bad vector: {line}");
            let expected = decode_hex(fields[7]);
            let frame =
                Frame::decode(&expected).unwrap_or_else(|error| panic!("{}: {error}", fields[0]));
            assert_eq!(frame.major, fields[1].parse::<u8>().unwrap());
            assert_eq!(frame.minor, fields[2].parse::<u8>().unwrap());
            assert_eq!(frame.command as u8, parse_u8(fields[3]));
            assert_eq!(frame.flags, parse_u8(fields[4]));
            assert_eq!(frame.sequence, fields[5].parse::<u32>().unwrap());
            assert_eq!(frame.payload, decode_hex(fields[6]));
            assert_eq!(frame.encode().unwrap(), expected);
        }
    }

    fn parse_u8(value: &str) -> u8 {
        u8::from_str_radix(value.trim_start_matches("0x"), 16).unwrap()
    }

    fn decode_hex(value: &str) -> Vec<u8> {
        let (pairs, remainder) = value.as_bytes().as_chunks::<2>();
        assert!(remainder.is_empty());
        pairs
            .iter()
            .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
            .collect()
    }
}
