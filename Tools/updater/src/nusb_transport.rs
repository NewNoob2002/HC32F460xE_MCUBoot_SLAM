use crate::product_identity::{USB_BOOT_PID, USB_VID};
use crate::{Transport, valid_application_pid};
use nusb::MaybeFuture;
use nusb::transfer::{Buffer, Bulk, In, Out, TransferError};
use nusb::{DeviceId, DeviceInfo, Endpoint};
use std::fmt;
use std::io;
use std::time::Duration;

const INTERFACE: u8 = 0;
const OUT_ENDPOINT: u8 = 0x02;
const IN_ENDPOINT: u8 = 0x81;
const HEADER_SIZE: usize = 16;
const MAX_RESPONSE_SIZE: usize = HEADER_SIZE + 512 + 4;
const INTERFACE_DESCRIPTION: &str = "Vendor Bulk Firmware Update";

#[derive(Clone)]
pub struct UpdaterDevice {
    info: DeviceInfo,
    mode: DeviceMode,
}

impl UpdaterDevice {
    pub fn id(&self) -> DeviceId {
        self.info.id()
    }

    pub fn mode(&self) -> DeviceMode {
        self.mode
    }

    pub fn serial_number(&self) -> &str {
        self.info
            .serial_number()
            .expect("updater enumeration requires a serial number")
    }

    pub fn product_id(&self) -> u16 {
        self.info.product_id()
    }

    pub fn label(&self) -> String {
        format!(
            "[{}] {} · {:04x}:{:04x} · Bus {}/Device {}",
            match self.mode {
                DeviceMode::BootRecovery => "Boot",
                DeviceMode::Application => "App ",
            },
            self.serial_number(),
            self.info.vendor_id(),
            self.info.product_id(),
            self.info.bus_id(),
            self.info.device_address()
        )
    }
}

pub struct NusbTransport {
    out_endpoint: Endpoint<Bulk, Out>,
    in_endpoint: Endpoint<Bulk, In>,
    out_max_packet: usize,
    in_max_packet: usize,
    serial_number: Option<String>,
    mode: DeviceMode,
    // Bytes of an IN frame whose transfer was cancelled by a receive timeout.
    pending: Vec<u8>,
    pending_length: Option<usize>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeviceMode {
    BootRecovery,
    Application,
}

impl fmt::Display for DeviceMode {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::BootRecovery => "boot-recovery",
            Self::Application => "application",
        })
    }
}

impl NusbTransport {
    pub fn enumerate() -> io::Result<Vec<UpdaterDevice>> {
        Ok(nusb::list_devices()
            .wait()?
            .filter_map(|info| {
                let mode = updater_mode(&info)?;
                Some(UpdaterDevice { info, mode })
            })
            .collect())
    }

    pub fn open(device: &UpdaterDevice) -> io::Result<Self> {
        Self::open_info(&device.info, device.mode)
    }

    /// Opens Application debug interface 1 on the device with this unique USB serial.
    pub fn open_debug(serial_number: &str) -> io::Result<Option<Self>> {
        let devices: Vec<_> = Self::enumerate()?
            .into_iter()
            .filter(|device| {
                device.mode == DeviceMode::Application && device.serial_number() == serial_number
            })
            .collect();
        let Some(device) = devices.first() else {
            return Ok(None);
        };
        if devices.len() != 1 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "multiple Application devices report the same serial number",
            ));
        }
        if !device.info.interfaces().any(|interface| {
            interface.interface_number() == 1
                && interface.class() == 0xff
                && interface.subclass() == 0
                && interface.protocol() == 0
                && interface.interface_string() == Some("Vendor Bulk Debug")
        }) {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                "Application does not expose Vendor Bulk Debug interface 1",
            ));
        }
        let opened = device.info.clone().open().wait()?;
        let interface = opened.claim_interface(1).wait()?;
        let out_endpoint = interface.endpoint::<Bulk, Out>(0x04)?;
        let in_endpoint = interface.endpoint::<Bulk, In>(0x83)?;
        let out_max_packet = out_endpoint.max_packet_size();
        let in_max_packet = in_endpoint.max_packet_size();
        if out_max_packet != 64 || in_max_packet != 64 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "Debug endpoints must have 64-byte max packets",
            ));
        }
        Ok(Some(Self {
            out_endpoint,
            in_endpoint,
            out_max_packet,
            in_max_packet,
            serial_number: Some(device.serial_number().to_owned()),
            mode: DeviceMode::Application,
            pending: Vec::new(),
            pending_length: None,
        }))
    }

    pub fn open_unique_updater() -> io::Result<Option<Self>> {
        let devices = Self::enumerate()?;
        match devices.as_slice() {
            [] => Ok(None),
            [device] => Self::open(device).map(Some),
            _ => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "multiple updater devices match; select one by serial number",
            )),
        }
    }

    pub fn open_application(serial_number: &str) -> io::Result<Option<Self>> {
        Self::open_by_serial(serial_number, DeviceMode::Application)
    }

    pub fn open_recovery(serial_number: &str) -> io::Result<Option<Self>> {
        Self::open_by_serial(serial_number, DeviceMode::BootRecovery)
    }

    pub fn open_unique_application() -> io::Result<Option<Self>> {
        let devices: Vec<_> = Self::enumerate()?
            .into_iter()
            .filter(|device| device.mode == DeviceMode::Application)
            .collect();
        match devices.as_slice() {
            [] => Ok(None),
            [device] => Self::open(device).map(Some),
            _ => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "multiple Application updater devices match; specify a serial number",
            )),
        }
    }

    pub fn open_by_serial(serial_number: &str, mode: DeviceMode) -> io::Result<Option<Self>> {
        let matches: Vec<_> = Self::enumerate()?
            .into_iter()
            .filter(|device| device.mode == mode && device.serial_number() == serial_number)
            .collect();
        let Some(device) = matches.first() else {
            return Ok(None);
        };
        if matches.len() != 1 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "multiple updater devices report the same serial number",
            ));
        }
        Self::open(device).map(Some)
    }

    fn open_info(device_info: &DeviceInfo, mode: DeviceMode) -> io::Result<Self> {
        let serial_number = device_info.serial_number().map(str::to_owned);
        let device = device_info.clone().open().wait()?;
        let interface = device.claim_interface(INTERFACE).wait()?;
        let out_endpoint = interface.endpoint::<Bulk, Out>(OUT_ENDPOINT)?;
        let in_endpoint = interface.endpoint::<Bulk, In>(IN_ENDPOINT)?;
        let out_max_packet = out_endpoint.max_packet_size();
        let in_max_packet = in_endpoint.max_packet_size();
        Ok(Self {
            out_endpoint,
            in_endpoint,
            out_max_packet,
            in_max_packet,
            serial_number,
            mode,
            pending: Vec::new(),
            pending_length: None,
        })
    }

    pub fn serial_number(&self) -> Option<&str> {
        self.serial_number.as_deref()
    }

    pub fn mode(&self) -> DeviceMode {
        self.mode
    }
}

fn mode_for_product_id(product_id: u16) -> Option<DeviceMode> {
    (product_id == USB_BOOT_PID)
        .then_some(DeviceMode::BootRecovery)
        .or_else(|| valid_application_pid(product_id).then_some(DeviceMode::Application))
}

fn updater_mode(info: &DeviceInfo) -> Option<DeviceMode> {
    if info.vendor_id() != USB_VID
        || info.serial_number().is_none()
        || !info.interfaces().any(|interface| {
            interface.interface_number() == INTERFACE
                && interface.class() == 0xff
                && interface.subclass() == 0
                && interface.protocol() == 0
                && interface.interface_string() == Some(INTERFACE_DESCRIPTION)
        })
    {
        return None;
    }
    mode_for_product_id(info.product_id())
}

impl Transport for NusbTransport {
    fn send(&mut self, request: &[u8], timeout: Duration) -> io::Result<()> {
        let completion = self
            .out_endpoint
            .transfer_blocking(request.to_vec().into(), timeout);
        completion.status.map_err(transfer_error)?;
        if completion.actual_len != request.len() {
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                format!(
                    "short USB write: {}/{} bytes",
                    completion.actual_len,
                    request.len()
                ),
            ));
        }
        if !request.is_empty() && request.len().is_multiple_of(self.out_max_packet) {
            let zlp = self
                .out_endpoint
                .transfer_blocking(Vec::<u8>::new().into(), timeout);
            zlp.status.map_err(transfer_error)?;
            if zlp.actual_len != 0 {
                return Err(io::Error::new(
                    io::ErrorKind::WriteZero,
                    "USB zero-length packet transferred bytes",
                ));
            }
        }
        Ok(())
    }

    fn receive(&mut self, timeout: Duration) -> io::Result<Vec<u8>> {
        loop {
            let remaining = self
                .pending_length
                .map(|length| length - self.pending.len())
                .unwrap_or(self.in_max_packet);
            let request_length = remaining.div_ceil(self.in_max_packet) * self.in_max_packet;
            let completion = self
                .in_endpoint
                .transfer_blocking(Buffer::new(request_length), timeout);
            // A transfer cancelled by the timeout may already hold the first packets of a frame.
            // Keep them so the next receive continues the same frame instead of starting mid-frame.
            let appended = append_chunk(
                &mut self.pending,
                &completion.buffer[..completion.actual_len],
                request_length,
                &mut self.pending_length,
                completion.status.is_ok(),
            );
            let complete = match appended {
                Ok(complete) => complete,
                Err(error) => {
                    self.discard_pending();
                    return Err(error);
                }
            };
            if complete {
                self.pending_length = None;
                return Ok(std::mem::take(&mut self.pending));
            }
            if let Err(error) = completion.status {
                if !matches!(error, TransferError::Cancelled) {
                    self.discard_pending();
                }
                return Err(transfer_error(error));
            }
        }
    }
}

impl NusbTransport {
    fn discard_pending(&mut self) {
        self.pending.clear();
        self.pending_length = None;
    }
}

/// Appends one IN transfer to the frame being assembled and returns whether the frame is complete.
/// `finished` is false for a transfer cancelled on timeout: its short length is not an end of frame.
fn append_chunk(
    response: &mut Vec<u8>,
    chunk: &[u8],
    requested: usize,
    expected_length: &mut Option<usize>,
    finished: bool,
) -> io::Result<bool> {
    // A ZLP that terminated the previous max-packet-multiple frame arrives before the next frame.
    if chunk.is_empty() && (response.is_empty() || !finished) {
        return Ok(false);
    }
    if chunk.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "empty USB response",
        ));
    }
    response.extend_from_slice(chunk);
    if expected_length.is_none() && response.len() >= HEADER_SIZE {
        let payload_length = u16::from_le_bytes([response[12], response[13]]) as usize;
        let length = HEADER_SIZE + payload_length + 4;
        if length > MAX_RESPONSE_SIZE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("USB response frame is too large: {length}"),
            ));
        }
        *expected_length = Some(length);
    }
    let ended = finished && chunk.len() < requested;
    if let Some(length) = *expected_length {
        if response.len() == length {
            return Ok(true);
        }
        if response.len() > length || ended {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                format!("USB response ended at {}/{} bytes", response.len(), length),
            ));
        }
    } else if ended {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "USB response ended before the protocol header",
        ));
    }
    Ok(false)
}

fn transfer_error(error: TransferError) -> io::Error {
    let kind = match error {
        TransferError::Cancelled => io::ErrorKind::Interrupted,
        TransferError::Disconnected => io::ErrorKind::NotConnected,
        TransferError::Stall => io::ErrorKind::ConnectionReset,
        TransferError::Fault | TransferError::Unknown(_) => io::ErrorKind::Other,
        TransferError::InvalidArgument => io::ErrorKind::InvalidInput,
    };
    io::Error::new(kind, error)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn assembles_a_response_across_bulk_reads() {
        let mut frame = vec![0u8; 84];
        frame[12..14].copy_from_slice(&64u16.to_le_bytes());
        let mut response = Vec::new();
        let mut expected = None;
        assert!(!append_chunk(&mut response, &frame[..64], 64, &mut expected, true).unwrap());
        assert!(append_chunk(&mut response, &frame[64..], 64, &mut expected, true).unwrap());
        assert_eq!(response, frame);
    }

    #[test]
    fn skips_a_terminating_zlp_before_the_next_frame() {
        let mut response = Vec::new();
        let mut expected = None;
        assert!(!append_chunk(&mut response, &[], 64, &mut expected, true).unwrap());
        assert!(response.is_empty() && expected.is_none());
    }

    #[test]
    fn keeps_a_frame_prefix_from_a_cancelled_transfer() {
        let mut frame = vec![0u8; 84];
        frame[12..14].copy_from_slice(&64u16.to_le_bytes());
        let mut response = Vec::new();
        let mut expected = None;
        // Timeout after the first packet of a two-packet frame: not an error, not complete.
        assert!(!append_chunk(&mut response, &frame[..64], 128, &mut expected, false).unwrap());
        assert!(!append_chunk(&mut response, &[], 64, &mut expected, false).unwrap());
        assert!(append_chunk(&mut response, &frame[64..], 64, &mut expected, true).unwrap());
        assert_eq!(response, frame);
    }

    #[test]
    fn rejects_a_short_transfer_before_the_header() {
        let mut response = Vec::new();
        let mut expected = None;
        assert_eq!(
            append_chunk(&mut response, &[0; 8], 64, &mut expected, true)
                .unwrap_err()
                .kind(),
            io::ErrorKind::UnexpectedEof
        );
    }

    #[test]
    fn maps_boot_and_application_product_ids() {
        assert_eq!(
            mode_for_product_id(USB_BOOT_PID),
            Some(DeviceMode::BootRecovery)
        );
        assert_eq!(
            mode_for_product_id(crate::product_identity::USB_APPLICATION_PID),
            Some(DeviceMode::Application)
        );
        assert_eq!(
            mode_for_product_id(crate::product_identity::USB_APPLICATION_PID_MAX),
            Some(DeviceMode::Application)
        );
        assert_eq!(mode_for_product_id(0), None);
    }
}
