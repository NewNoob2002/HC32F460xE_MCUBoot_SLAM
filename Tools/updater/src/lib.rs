mod protocol;

pub use protocol::{Command, Frame, ProtocolError, Status, crc32};

use std::fmt;
use std::io;
use std::str::FromStr;
use std::time::{Duration, Instant};

const IMAGE_MAGIC: u32 = 0x96f3_b83d;
const IMAGE_HEADER_SIZE: usize = 32;
const REQUIRED_CAPABILITIES: u32 = 0b111;
const DEFAULT_ATTEMPTS: usize = 3;
const MAX_STALE_RESPONSES: usize = 2;

pub trait Transport {
    fn send(&mut self, request: &[u8], timeout: Duration) -> io::Result<()>;
    fn receive(&mut self, timeout: Duration) -> io::Result<Vec<u8>>;
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Version {
    pub major: u8,
    pub minor: u8,
    pub revision: u16,
    pub build: u32,
}

impl Version {
    fn decode(input: &[u8]) -> Result<Self, Error> {
        if input.len() != 8 {
            return Err(Error::Protocol(ProtocolError::InvalidLength(input.len())));
        }
        Ok(Self {
            major: input[0],
            minor: input[1],
            revision: read_u16(&input[2..4]),
            build: read_u32(&input[4..8]),
        })
    }
}

impl fmt::Display for Version {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}.{}.{}", self.major, self.minor, self.revision)?;
        if self.build != 0 {
            write!(formatter, "+{}", self.build)?;
        }
        Ok(())
    }
}

impl FromStr for Version {
    type Err = Error;

    fn from_str(input: &str) -> Result<Self, Self::Err> {
        let (version, build) = input.split_once('+').unwrap_or((input, "0"));
        let mut fields = version.split('.');
        let major = parse_number(fields.next(), "major")?;
        let minor = parse_number(fields.next(), "minor")?;
        let revision = parse_number(fields.next(), "revision")?;
        if fields.next().is_some() {
            return Err(Error::Argument(
                "version must be MAJOR.MINOR.REVISION[+BUILD]",
            ));
        }
        Ok(Self {
            major,
            minor,
            revision,
            build: build
                .parse()
                .map_err(|_| Error::Argument("invalid build number"))?,
        })
    }
}

fn parse_number<T: FromStr>(value: Option<&str>, name: &'static str) -> Result<T, Error> {
    value
        .ok_or(Error::Argument(
            "version must be MAJOR.MINOR.REVISION[+BUILD]",
        ))?
        .parse()
        .map_err(|_| Error::InvalidVersionField(name))
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct FirmwareImage {
    bytes: Vec<u8>,
    pub version: Version,
}

impl FirmwareImage {
    pub fn parse(bytes: Vec<u8>) -> Result<Self, Error> {
        if bytes.len() < IMAGE_HEADER_SIZE {
            return Err(Error::Image("image is smaller than the MCUboot header"));
        }
        if read_u32(&bytes[0..4]) != IMAGE_MAGIC {
            return Err(Error::Image("invalid MCUboot image magic"));
        }

        let header_size = usize::from(read_u16(&bytes[8..10]));
        let body_size = read_u32(&bytes[12..16]) as usize;
        if header_size < IMAGE_HEADER_SIZE
            || header_size > bytes.len()
            || body_size > bytes.len() - header_size
        {
            return Err(Error::Image("invalid MCUboot header/body size"));
        }

        Ok(Self {
            version: Version::decode(&bytes[20..28])?,
            bytes,
        })
    }

    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    pub fn crc32(&self) -> u32 {
        crc32(&self.bytes)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HelloInfo {
    pub max_payload: u16,
    pub capabilities: u32,
    pub session_timeout_ms: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeviceInfo {
    pub board_revision: u16,
    pub hardware_id: u32,
    pub board_id: u32,
    pub image_capacity: u32,
    pub write_alignment: u32,
    pub erase_alignment: u32,
    pub application_version: Version,
    pub bootloader_version: Version,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ProgressEvent {
    Device(DeviceInfo),
    Transferring { sent: usize, total: usize },
    Verifying,
    Committing,
    WaitingForReenumeration,
}

pub struct UpgradeWorkflow;

impl UpgradeWorkflow {
    pub fn install<T: Transport>(
        client: &mut ProtocolV1Client<T>,
        image: &FirmwareImage,
        progress: impl FnMut(ProgressEvent),
    ) -> Result<DeviceInfo, Error> {
        client.install(image, progress)
    }

    pub fn wait_for_version<T: Transport>(
        mut connect: impl FnMut() -> io::Result<Option<T>>,
        expected: Version,
        timeout: Duration,
        poll_interval: Duration,
        request_timeout: Duration,
        mut progress: impl FnMut(ProgressEvent),
    ) -> Result<DeviceInfo, Error> {
        if timeout.is_zero() || poll_interval.is_zero() || request_timeout.is_zero() {
            return Err(Error::Argument(
                "wait timeout, poll interval and request timeout must be positive",
            ));
        }
        let deadline = Instant::now()
            .checked_add(timeout)
            .ok_or(Error::Argument("wait timeout is too large"))?;
        progress(ProgressEvent::WaitingForReenumeration);
        loop {
            let transport = match connect() {
                Ok(transport) => transport,
                Err(error) if retryable_wait_error(&error) => None,
                Err(error) => return Err(Error::Transport(error)),
            };
            if let Some(transport) = transport {
                let mut client = ProtocolV1Client::new(transport, request_timeout);
                match client.info() {
                    Ok(info) => {
                        progress(ProgressEvent::Device(info.clone()));
                        if info.application_version == expected {
                            return Ok(info);
                        }
                    }
                    Err(Error::Transport(error)) if retryable_wait_error(&error) => {}
                    Err(error) => return Err(error),
                }
            }
            let now = Instant::now();
            if now >= deadline {
                return Err(Error::WaitTimeout(expected));
            }
            std::thread::sleep(poll_interval.min(deadline - now));
        }
    }
}

fn retryable_wait_error(error: &io::Error) -> bool {
    matches!(
        error.kind(),
        io::ErrorKind::NotFound
            | io::ErrorKind::NotConnected
            | io::ErrorKind::ConnectionAborted
            | io::ErrorKind::ConnectionReset
            | io::ErrorKind::Interrupted
            | io::ErrorKind::TimedOut
            | io::ErrorKind::WouldBlock
    )
}

pub struct ProtocolV1Client<T> {
    transport: T,
    timeout: Duration,
    attempts: usize,
    next_sequence: u32,
    hello: Option<HelloInfo>,
    last_response: Option<(Command, u32)>,
}

impl<T: Transport> ProtocolV1Client<T> {
    pub fn new(transport: T, timeout: Duration) -> Self {
        Self {
            transport,
            timeout,
            attempts: DEFAULT_ATTEMPTS,
            next_sequence: 0,
            hello: None,
            last_response: None,
        }
    }

    pub fn into_transport(self) -> T {
        self.transport
    }

    pub fn hello(&mut self) -> Result<HelloInfo, Error> {
        if let Some(info) = &self.hello {
            return Ok(info.clone());
        }
        let body = self.request(Command::Hello, Vec::new())?;
        if body.len() != 10 {
            return Err(Error::Protocol(ProtocolError::InvalidLength(body.len())));
        }
        let info = HelloInfo {
            max_payload: read_u16(&body[0..2]),
            capabilities: read_u32(&body[2..6]),
            session_timeout_ms: read_u32(&body[6..10]),
        };
        if usize::from(info.max_payload) > protocol::MAX_PAYLOAD || info.max_payload <= 4 {
            return Err(Error::Protocol(ProtocolError::PayloadTooLarge(
                usize::from(info.max_payload),
            )));
        }
        self.hello = Some(info.clone());
        Ok(info)
    }

    pub fn info(&mut self) -> Result<DeviceInfo, Error> {
        self.hello()?;
        let body = self.request(Command::DeviceInfo, Vec::new())?;
        if body.len() != 38 {
            return Err(Error::Protocol(ProtocolError::InvalidLength(body.len())));
        }
        Ok(DeviceInfo {
            board_revision: read_u16(&body[0..2]),
            hardware_id: read_u32(&body[2..6]),
            board_id: read_u32(&body[6..10]),
            image_capacity: read_u32(&body[10..14]),
            write_alignment: read_u32(&body[14..18]),
            erase_alignment: read_u32(&body[18..22]),
            application_version: Version::decode(&body[22..30])?,
            bootloader_version: Version::decode(&body[30..38])?,
        })
    }

    pub fn install(
        &mut self,
        image: &FirmwareImage,
        mut progress: impl FnMut(ProgressEvent),
    ) -> Result<DeviceInfo, Error> {
        let hello = self.hello()?;
        if hello.capabilities & REQUIRED_CAPABILITIES != REQUIRED_CAPABILITIES {
            return Err(Error::Capability(hello.capabilities));
        }
        let device = self.info()?;
        progress(ProgressEvent::Device(device.clone()));
        if image.bytes.len() > device.image_capacity as usize {
            return Err(Error::Image("image exceeds device capacity"));
        }

        let image_size =
            u32::try_from(image.bytes.len()).map_err(|_| Error::Image("image is too large"))?;
        let image_crc = image.crc32();
        let mut begin = Vec::with_capacity(28);
        begin.extend_from_slice(&image_size.to_le_bytes());
        begin.extend_from_slice(&image_crc.to_le_bytes());
        begin.extend_from_slice(&device.hardware_id.to_le_bytes());
        begin.extend_from_slice(&device.board_id.to_le_bytes());
        begin.extend_from_slice(&device.board_revision.to_le_bytes());
        begin.extend_from_slice(&0u16.to_le_bytes());
        append_version(&mut begin, image.version);
        self.request(Command::Begin, begin)?;

        let chunk_size = usize::from(hello.max_payload) - 4;
        let mut offset = 0usize;
        while offset < image.bytes.len() {
            let end = (offset + chunk_size).min(image.bytes.len());
            let mut payload = Vec::with_capacity(4 + end - offset);
            payload.extend_from_slice(&(offset as u32).to_le_bytes());
            payload.extend_from_slice(&image.bytes[offset..end]);
            let response = self.request(Command::Data, payload)?;
            if response.len() != 4 || read_u32(&response) as usize != end {
                return Err(Error::Protocol(ProtocolError::InvalidLength(
                    response.len(),
                )));
            }
            offset = end;
            progress(ProgressEvent::Transferring {
                sent: offset,
                total: image.bytes.len(),
            });
        }

        progress(ProgressEvent::Verifying);
        let end = self.request(Command::End, Vec::new())?;
        if end.len() != 8 || read_u32(&end[0..4]) != image_size || read_u32(&end[4..8]) != image_crc
        {
            return Err(Error::Image(
                "device readback result does not match the image",
            ));
        }

        progress(ProgressEvent::Committing);
        self.request(Command::Commit, Vec::new())?;
        progress(ProgressEvent::WaitingForReenumeration);
        Ok(device)
    }

    fn request(&mut self, command: Command, payload: Vec<u8>) -> Result<Vec<u8>, Error> {
        let sequence = self.next_sequence;
        let encoded = Frame::request(command, sequence, payload).encode()?;
        let mut last_error = None;

        for attempt in 0..self.attempts {
            match self.transport.send(&encoded, self.timeout) {
                Ok(()) => match self.receive_response(command, sequence) {
                    Ok((status, body)) => {
                        self.last_response = Some((command, sequence));
                        if status.advances_sequence() {
                            self.next_sequence = self.next_sequence.wrapping_add(1);
                        }
                        if status != Status::Ok {
                            return Err(Error::Device(status));
                        }
                        return Ok(body);
                    }
                    Err(Error::Protocol(error))
                        if error.retryable() && attempt + 1 < self.attempts =>
                    {
                        last_error = Some(Error::Protocol(error));
                    }
                    Err(Error::Transport(error))
                        if retryable_transport_error(&error) && attempt + 1 < self.attempts =>
                    {
                        last_error = Some(Error::Transport(error));
                    }
                    Err(error) => return Err(error),
                },
                Err(error) if retryable_transport_error(&error) && attempt + 1 < self.attempts => {
                    last_error = Some(Error::Transport(error));
                }
                Err(error) => return Err(Error::Transport(error)),
            }
        }

        Err(last_error.unwrap_or(Error::Argument("request attempts must be positive")))
    }

    fn receive_response(
        &mut self,
        command: Command,
        sequence: u32,
    ) -> Result<(Status, Vec<u8>), Error> {
        for _ in 0..=MAX_STALE_RESPONSES {
            let raw = self
                .transport
                .receive(self.timeout)
                .map_err(Error::Transport)?;
            let frame = Frame::decode(&raw)?;
            if frame.command == command && frame.sequence == sequence {
                return decode_response(frame);
            }
            if self.last_response == Some((frame.command, frame.sequence)) {
                continue;
            }
            return Err(Error::Protocol(ProtocolError::UnexpectedResponse {
                command: frame.command as u8,
                sequence: frame.sequence,
            }));
        }
        Err(Error::Protocol(ProtocolError::UnexpectedResponse {
            command: command as u8,
            sequence,
        }))
    }
}

fn retryable_transport_error(error: &io::Error) -> bool {
    matches!(
        error.kind(),
        io::ErrorKind::Interrupted | io::ErrorKind::TimedOut | io::ErrorKind::WouldBlock
    )
}

fn decode_response(frame: Frame) -> Result<(Status, Vec<u8>), Error> {
    if frame.major != protocol::VERSION_MAJOR
        || frame.minor != protocol::VERSION_MINOR
        || frame.flags & 0x01 == 0
        || frame.flags & !0x03 != 0
        || frame.payload.len() < 2
    {
        return Err(Error::Protocol(ProtocolError::InvalidLength(
            frame.payload.len(),
        )));
    }
    let status = Status::try_from(read_u16(&frame.payload[0..2]))?;
    if (status == Status::Ok && frame.flags != 0x01)
        || (status != Status::Ok && frame.flags != 0x03)
    {
        return Err(Error::Protocol(ProtocolError::ReservedField));
    }
    Ok((status, frame.payload[2..].to_vec()))
}

fn append_version(output: &mut Vec<u8>, version: Version) {
    output.extend_from_slice(&[version.major, version.minor]);
    output.extend_from_slice(&version.revision.to_le_bytes());
    output.extend_from_slice(&version.build.to_le_bytes());
}

fn read_u16(input: &[u8]) -> u16 {
    u16::from_le_bytes(input.try_into().expect("u16 slice has fixed length"))
}

fn read_u32(input: &[u8]) -> u32 {
    u32::from_le_bytes(input.try_into().expect("u32 slice has fixed length"))
}

#[derive(Debug)]
pub enum Error {
    Argument(&'static str),
    InvalidVersionField(&'static str),
    Image(&'static str),
    Capability(u32),
    Device(Status),
    Protocol(ProtocolError),
    Transport(io::Error),
    WaitTimeout(Version),
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Argument(message) | Self::Image(message) => formatter.write_str(message),
            Self::InvalidVersionField(field) => write!(formatter, "invalid version {field}"),
            Self::Capability(bits) => write!(
                formatter,
                "device lacks required capabilities: 0x{bits:08x}"
            ),
            Self::Device(status) => write!(formatter, "device returned {status:?}"),
            Self::Protocol(error) => write!(formatter, "protocol error: {error}"),
            Self::Transport(error) => write!(formatter, "transport error: {error}"),
            Self::WaitTimeout(version) => {
                write!(formatter, "timed out waiting for version {version}")
            }
        }
    }
}

impl std::error::Error for Error {}

impl From<ProtocolError> for Error {
    fn from(error: ProtocolError) -> Self {
        Self::Protocol(error)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::VecDeque;

    #[test]
    fn parses_mcuboot_image_and_version() {
        let mut bytes = vec![0u8; 520];
        bytes[0..4].copy_from_slice(&IMAGE_MAGIC.to_le_bytes());
        bytes[8..10].copy_from_slice(&512u16.to_le_bytes());
        bytes[12..16].copy_from_slice(&8u32.to_le_bytes());
        bytes[20..28].copy_from_slice(&[2, 1, 3, 0, 4, 0, 0, 0]);
        let image = FirmwareImage::parse(bytes).unwrap();
        assert_eq!(image.version.to_string(), "2.1.3+4");
    }

    #[test]
    fn parses_cli_version() {
        assert_eq!(
            "2.0.3+4".parse::<Version>().unwrap(),
            Version {
                major: 2,
                minor: 0,
                revision: 3,
                build: 4,
            }
        );
        assert!("2.0".parse::<Version>().is_err());
    }

    #[test]
    fn retries_the_exact_request_after_timeout() {
        let hello = response(Command::Hello, 0, Status::Ok, &hello_body());
        let transport = ScriptTransport::new([
            Err(io::Error::new(io::ErrorKind::TimedOut, "injected timeout")),
            Ok(hello.clone()),
            Ok(hello),
            Ok(response(
                Command::DeviceInfo,
                1,
                Status::Ok,
                &device_info_body(2048),
            )),
        ]);
        let mut client = ProtocolV1Client::new(transport, Duration::from_millis(10));
        assert_eq!(client.hello().unwrap().max_payload, 512);
        assert_eq!(client.info().unwrap().image_capacity, 2048);
        let transport = client.into_transport();
        assert_eq!(transport.requests.len(), 3);
        assert_eq!(transport.requests[0], transport.requests[1]);
    }

    #[test]
    fn disconnect_is_not_retried_without_resume() {
        let transport = ScriptTransport::new([Err(io::Error::new(
            io::ErrorKind::BrokenPipe,
            "injected disconnect",
        ))]);
        let mut client = ProtocolV1Client::new(transport, Duration::from_millis(10));
        assert!(matches!(client.hello(), Err(Error::Transport(_))));
        assert_eq!(client.into_transport().requests.len(), 1);
    }

    #[test]
    fn rejects_an_image_before_begin_when_capacity_is_too_small() {
        let transport = ScriptTransport::new([
            Ok(response(Command::Hello, 0, Status::Ok, &hello_body())),
            Ok(response(
                Command::DeviceInfo,
                1,
                Status::Ok,
                &device_info_body(32),
            )),
        ]);
        let mut client = ProtocolV1Client::new(transport, Duration::from_millis(10));
        let image = FirmwareImage::parse(test_image(40)).unwrap();
        assert!(matches!(
            client.install(&image, |_| {}),
            Err(Error::Image("image exceeds device capacity"))
        ));
        assert_eq!(client.into_transport().requests.len(), 2);
    }

    #[test]
    fn wait_workflow_returns_the_requested_version() {
        let expected = Version {
            major: 1,
            minor: 0,
            revision: 0,
            build: 0,
        };
        let mut transport = Some(ScriptTransport::new([
            Ok(response(Command::Hello, 0, Status::Ok, &hello_body())),
            Ok(response(
                Command::DeviceInfo,
                1,
                Status::Ok,
                &device_info_body(2048),
            )),
        ]));
        let info = UpgradeWorkflow::wait_for_version(
            || Ok(transport.take()),
            expected,
            Duration::from_secs(1),
            Duration::from_millis(1),
            Duration::from_millis(10),
            |_| {},
        )
        .unwrap();
        assert_eq!(info.application_version, expected);
    }

    struct ScriptTransport {
        responses: VecDeque<io::Result<Vec<u8>>>,
        requests: Vec<Vec<u8>>,
    }

    impl ScriptTransport {
        fn new(responses: impl IntoIterator<Item = io::Result<Vec<u8>>>) -> Self {
            Self {
                responses: responses.into_iter().collect(),
                requests: Vec::new(),
            }
        }
    }

    impl Transport for ScriptTransport {
        fn send(&mut self, request: &[u8], _timeout: Duration) -> io::Result<()> {
            self.requests.push(request.to_vec());
            Ok(())
        }

        fn receive(&mut self, _timeout: Duration) -> io::Result<Vec<u8>> {
            self.responses.pop_front().expect("scripted response")
        }
    }

    fn response(command: Command, sequence: u32, status: Status, body: &[u8]) -> Vec<u8> {
        let mut payload = Vec::with_capacity(2 + body.len());
        payload.extend_from_slice(&(status as u16).to_le_bytes());
        payload.extend_from_slice(body);
        Frame {
            major: protocol::VERSION_MAJOR,
            minor: protocol::VERSION_MINOR,
            command,
            flags: if status == Status::Ok { 0x01 } else { 0x03 },
            sequence,
            payload,
        }
        .encode()
        .unwrap()
    }

    fn hello_body() -> Vec<u8> {
        let mut body = Vec::new();
        body.extend_from_slice(&512u16.to_le_bytes());
        body.extend_from_slice(&REQUIRED_CAPABILITIES.to_le_bytes());
        body.extend_from_slice(&5000u32.to_le_bytes());
        body
    }

    fn device_info_body(capacity: u32) -> Vec<u8> {
        let mut body = Vec::new();
        body.extend_from_slice(&2u16.to_le_bytes());
        body.extend_from_slice(&0x0000_4600u32.to_le_bytes());
        body.extend_from_slice(&1u32.to_le_bytes());
        body.extend_from_slice(&capacity.to_le_bytes());
        body.extend_from_slice(&4u32.to_le_bytes());
        body.extend_from_slice(&256u32.to_le_bytes());
        append_version(
            &mut body,
            Version {
                major: 1,
                minor: 0,
                revision: 0,
                build: 0,
            },
        );
        append_version(
            &mut body,
            Version {
                major: 1,
                minor: 0,
                revision: 0,
                build: 0,
            },
        );
        body
    }

    fn test_image(length: usize) -> Vec<u8> {
        let mut bytes = vec![0u8; length];
        bytes[0..4].copy_from_slice(&IMAGE_MAGIC.to_le_bytes());
        bytes[8..10].copy_from_slice(&(IMAGE_HEADER_SIZE as u16).to_le_bytes());
        bytes[12..16].copy_from_slice(&((length - IMAGE_HEADER_SIZE) as u32).to_le_bytes());
        bytes[20] = 2;
        bytes
    }
}
