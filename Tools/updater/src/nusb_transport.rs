use hc32_updater::Transport;
use hc32_updater::product_identity::{USB_APPLICATION_PID, USB_BOOT_PID, USB_VID};
use nusb::Endpoint;
use nusb::MaybeFuture;
use nusb::transfer::{Buffer, Bulk, In, Out, TransferError};
use std::fmt;
use std::io;
use std::time::Duration;

const INTERFACE: u8 = 0;
const OUT_ENDPOINT: u8 = 0x02;
const IN_ENDPOINT: u8 = 0x81;
const HEADER_SIZE: usize = 16;
const MAX_RESPONSE_SIZE: usize = HEADER_SIZE + 512 + 4;

pub struct NusbTransport {
    out_endpoint: Endpoint<Bulk, Out>,
    in_endpoint: Endpoint<Bulk, In>,
    out_max_packet: usize,
    in_max_packet: usize,
    serial_number: Option<String>,
    mode: DeviceMode,
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
    pub fn open_unique_updater() -> io::Result<Option<Self>> {
        Self::open_unique(&[USB_BOOT_PID, USB_APPLICATION_PID])
    }

    pub fn open_application() -> io::Result<Option<Self>> {
        Self::open_unique(&[USB_APPLICATION_PID])
    }

    fn open_unique(product_ids: &[u16]) -> io::Result<Option<Self>> {
        let mut devices = nusb::list_devices().wait()?.filter(|device| {
            device.vendor_id() == USB_VID && product_ids.contains(&device.product_id())
        });
        let Some(device_info) = devices.next() else {
            return Ok(None);
        };
        if devices.next().is_some() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "multiple Boot/Application updater devices match",
            ));
        }

        let mode = mode_for_product_id(device_info.product_id())
            .expect("device was filtered by product ID");
        let serial_number = device_info.serial_number().map(str::to_owned);
        let device = device_info.open().wait()?;
        let interface = device.claim_interface(INTERFACE).wait()?;
        let out_endpoint = interface.endpoint::<Bulk, Out>(OUT_ENDPOINT)?;
        let in_endpoint = interface.endpoint::<Bulk, In>(IN_ENDPOINT)?;
        let out_max_packet = out_endpoint.max_packet_size();
        let in_max_packet = in_endpoint.max_packet_size();
        Ok(Some(Self {
            out_endpoint,
            in_endpoint,
            out_max_packet,
            in_max_packet,
            serial_number,
            mode,
        }))
    }

    pub fn serial_number(&self) -> Option<&str> {
        self.serial_number.as_deref()
    }

    pub fn mode(&self) -> DeviceMode {
        self.mode
    }
}

fn mode_for_product_id(product_id: u16) -> Option<DeviceMode> {
    match product_id {
        USB_BOOT_PID => Some(DeviceMode::BootRecovery),
        USB_APPLICATION_PID => Some(DeviceMode::Application),
        _ => None,
    }
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
        let mut response = Vec::with_capacity(MAX_RESPONSE_SIZE);
        let mut expected_length = None;
        loop {
            let remaining = expected_length
                .map(|length| length - response.len())
                .unwrap_or(self.in_max_packet);
            let request_length = remaining.div_ceil(self.in_max_packet) * self.in_max_packet;
            let completion = self
                .in_endpoint
                .transfer_blocking(Buffer::new(request_length), timeout);
            completion.status.map_err(transfer_error)?;
            if append_chunk(
                &mut response,
                &completion.buffer[..completion.actual_len],
                request_length,
                &mut expected_length,
            )? {
                return Ok(response);
            }
        }
    }
}

fn append_chunk(
    response: &mut Vec<u8>,
    chunk: &[u8],
    requested: usize,
    expected_length: &mut Option<usize>,
) -> io::Result<bool> {
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
    if let Some(length) = *expected_length {
        if response.len() == length {
            return Ok(true);
        }
        if response.len() > length || chunk.len() < requested {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                format!("USB response ended at {}/{} bytes", response.len(), length),
            ));
        }
    } else if chunk.len() < requested {
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
        assert!(!append_chunk(&mut response, &frame[..64], 64, &mut expected).unwrap());
        assert!(append_chunk(&mut response, &frame[64..], 64, &mut expected).unwrap());
        assert_eq!(response, frame);
    }

    #[test]
    fn rejects_a_short_transfer_before_the_header() {
        let mut response = Vec::new();
        let mut expected = None;
        assert_eq!(
            append_chunk(&mut response, &[0; 8], 64, &mut expected)
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
            mode_for_product_id(USB_APPLICATION_PID),
            Some(DeviceMode::Application)
        );
        assert_eq!(mode_for_product_id(0), None);
    }
}
