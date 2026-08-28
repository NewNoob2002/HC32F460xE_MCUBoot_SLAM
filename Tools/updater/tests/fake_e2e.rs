#![cfg(feature = "fake-e2e")]

use hc32_updater::{FirmwareImage, ProgressEvent, ProtocolV1Client, Transport, Version};
use std::fs;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::process::{Child, ChildStdin, ChildStdout, Command, Stdio};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

struct ProcessTransport {
    child: Option<Child>,
    input: Option<ChildStdin>,
    output: ChildStdout,
}

impl ProcessTransport {
    fn spawn(expected_image: &PathBuf) -> Self {
        let fake_device = std::env::var("HC32_FAKE_DEVICE").expect("HC32_FAKE_DEVICE from CTest");
        let mut child = Command::new(fake_device)
            .arg(expected_image)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .expect("start C fake device");
        let input = child.stdin.take().expect("fake stdin");
        let output = child.stdout.take().expect("fake stdout");
        Self {
            child: Some(child),
            input: Some(input),
            output,
        }
    }

    fn finish(mut self) {
        self.input.take();
        let status = self
            .child
            .take()
            .unwrap()
            .wait()
            .expect("wait for C fake device");
        assert!(status.success(), "C fake device failed: {status}");
    }
}

impl Drop for ProcessTransport {
    fn drop(&mut self) {
        self.input.take();
        if let Some(mut child) = self.child.take() {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

impl Transport for ProcessTransport {
    fn send(&mut self, request: &[u8], _timeout: Duration) -> io::Result<()> {
        let input = self.input.as_mut().expect("fake input remains open");
        input.write_all(request)?;
        input.flush()
    }

    fn receive(&mut self, _timeout: Duration) -> io::Result<Vec<u8>> {
        let mut header = [0u8; 16];
        self.output.read_exact(&mut header)?;
        let payload_length = u16::from_le_bytes([header[12], header[13]]) as usize;
        let mut response = header.to_vec();
        response.resize(16 + payload_length + 4, 0);
        self.output.read_exact(&mut response[16..])?;
        Ok(response)
    }
}

#[test]
fn rust_core_completes_real_c_manager_fake_e2e() {
    let path = temporary_image_path();
    let bytes = make_image(777);
    fs::write(&path, &bytes).expect("write fake image");
    let image = FirmwareImage::parse(bytes).expect("parse fake MCUboot image");

    let transport = ProcessTransport::spawn(&path);
    let mut client = ProtocolV1Client::new(transport, Duration::from_secs(1));
    let info = client.info().expect("read fake device info");
    assert_eq!(info.application_version, version(1, 0, 0, 0));
    assert_eq!(info.hardware_id, 0x0000_4600);
    assert_eq!(info.board_id, 1);
    assert_eq!(info.board_revision, 2);

    let mut events = Vec::new();
    client
        .install(&image, |event| events.push(event))
        .expect("complete fake install");
    assert!(events.contains(&ProgressEvent::Verifying));
    assert!(events.contains(&ProgressEvent::Committing));
    assert!(events.contains(&ProgressEvent::WaitingForReenumeration));
    assert!(events.contains(&ProgressEvent::Transferring {
        sent: image.bytes().len(),
        total: image.bytes().len(),
    }));

    client.into_transport().finish();
    fs::remove_file(path).expect("remove fake image");
}

fn make_image(length: usize) -> Vec<u8> {
    assert!(length >= 52);
    let mut bytes: Vec<u8> = (0..length)
        .map(|index| (index as u8).wrapping_mul(37).wrapping_add(11))
        .collect();
    bytes[0..4].copy_from_slice(&0x96f3_b83du32.to_le_bytes());
    bytes[8..10].copy_from_slice(&32u16.to_le_bytes());
    bytes[10..12].copy_from_slice(&20u16.to_le_bytes());
    bytes[12..16].copy_from_slice(&((length - 52) as u32).to_le_bytes());
    bytes[20..28].copy_from_slice(&[2, 0, 0, 0, 0, 0, 0, 0]);
    let tlv = length - 20;
    bytes[tlv..tlv + 2].copy_from_slice(&0x6908u16.to_le_bytes());
    bytes[tlv + 2..tlv + 4].copy_from_slice(&20u16.to_le_bytes());
    bytes[tlv + 4..tlv + 6].copy_from_slice(&0x00a0u16.to_le_bytes());
    bytes[tlv + 6..tlv + 8].copy_from_slice(&12u16.to_le_bytes());
    bytes[tlv + 8..tlv + 20].copy_from_slice(&[1, 0, 2, 0, 0x00, 0x46, 0, 0, 1, 0, 0, 0]);
    bytes
}

fn temporary_image_path() -> PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock")
        .as_nanos();
    std::env::temp_dir().join(format!("hc32-updater-{}-{nonce}.bin", std::process::id()))
}

fn version(major: u8, minor: u8, revision: u16, build: u32) -> Version {
    Version {
        major,
        minor,
        revision,
        build,
    }
}
