use hc32_updater::debug_link::{
    Command, ConfigPage, DebugClient, Error, Event, Frame, HelloInfo, Status, TelemetryValue,
    decode_tlvs,
};
use hc32_updater::{Transport, crc32};
use std::collections::VecDeque;
use std::io;
use std::time::Duration;

#[derive(Default)]
struct Script {
    reads: VecDeque<io::Result<Vec<u8>>>,
    sent: Vec<Frame>,
}
impl Script {
    fn frames(frames: impl IntoIterator<Item = Frame>) -> Self {
        Self {
            reads: frames
                .into_iter()
                .map(|frame| Ok(frame.encode().unwrap()))
                .collect(),
            sent: vec![],
        }
    }
}
impl Transport for Script {
    fn send(&mut self, bytes: &[u8], _: Duration) -> io::Result<()> {
        self.sent.push(Frame::decode(bytes).unwrap());
        Ok(())
    }
    fn receive(&mut self, _: Duration) -> io::Result<Vec<u8>> {
        self.reads
            .pop_front()
            .unwrap_or_else(|| Err(io::Error::from(io::ErrorKind::TimedOut)))
    }
}
impl Transport for &mut Script {
    fn send(&mut self, bytes: &[u8], timeout: Duration) -> io::Result<()> {
        (**self).send(bytes, timeout)
    }
    fn receive(&mut self, timeout: Duration) -> io::Result<Vec<u8>> {
        (**self).receive(timeout)
    }
}
fn response(command: Command, sequence: u32, body: &[u8]) -> Frame {
    let mut payload = vec![0, 0];
    payload.extend_from_slice(body);
    Frame {
        command,
        flags: 1,
        sequence,
        payload,
    }
}
fn telemetry(sequence: u32) -> Frame {
    Frame {
        command: Command::EvtTelemetry,
        flags: 4,
        sequence,
        payload: vec![1, 4, 42, 0, 0, 0],
    }
}
fn log(sequence: u32) -> Frame {
    Frame {
        command: Command::EvtLog,
        flags: 4,
        sequence,
        payload: vec![2, 42, 0, 0, 0, 2, b'o', b'k'],
    }
}
fn entry(key: u16, name: &str, value: i32) -> Vec<u8> {
    let mut bytes = key.to_le_bytes().to_vec();
    bytes.extend_from_slice(&[0, 3]);
    for number in [-100i32, 100, 0, value] {
        bytes.extend_from_slice(&number.to_le_bytes());
    }
    bytes.extend_from_slice(&[0, name.len() as u8]);
    bytes.extend_from_slice(name.as_bytes());
    bytes
}
fn page(total: u8, entries: &[Vec<u8>]) -> Vec<u8> {
    let mut bytes = vec![total, entries.len() as u8];
    for entry in entries {
        bytes.extend_from_slice(entry);
    }
    bytes
}
fn repair_crc(bytes: &mut [u8]) {
    let length = bytes.len();
    let crc = crc32(&bytes[..length - 4]);
    bytes[length - 4..].copy_from_slice(&crc.to_le_bytes());
}

#[test]
fn golden_hello_and_response_round_trips() {
    let golden = [
        0x44, 0x42, 0x47, 0x31, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0x90, 0x75, 0xdf, 0x32,
    ];
    let hello = Frame {
        command: Command::Hello,
        flags: 0,
        sequence: 1,
        payload: vec![],
    };
    assert_eq!(hello.encode().unwrap(), golden);
    assert_eq!(Frame::decode(&golden).unwrap(), hello);
    for frame in [
        response(Command::ConfigGet, 0x12345678, &(-42i32).to_le_bytes()),
        telemetry(8),
        log(9),
        Frame {
            command: Command::ConfigSet,
            flags: 3,
            sequence: 4,
            payload: vec![3, 0],
        },
    ] {
        assert_eq!(Frame::decode(&frame.encode().unwrap()).unwrap(), frame);
    }
    let info = HelloInfo::decode(&[1, 0, 0, 1, 63, 0, 0, 0, 1, 0]).unwrap();
    assert_eq!(
        (info.max_payload, info.capabilities, info.telemetry_version),
        (256, 63, 1)
    );
    assert!(HelloInfo::decode(&[1, 0]).is_err());
}

#[test]
fn rejects_bad_crc_flags_reserved_version_and_lengths() {
    let bytes = response(Command::Stats, 1, &[]).encode().unwrap();
    let mut damaged = bytes.clone();
    damaged[8] ^= 1;
    assert!(matches!(
        Frame::decode(&damaged),
        Err(Error::Invalid("CRC mismatch"))
    ));
    for (offset, value) in [
        (7, 2),
        (7, 4),
        (7, 0x81),
        (14, 1),
        (15, 1),
        (4, 2),
        (5, 1),
        (12, 255),
        (13, 2),
        (6, 0xff),
    ] {
        let mut damaged = bytes.clone();
        damaged[offset] = value;
        repair_crc(&mut damaged);
        assert!(
            Frame::decode(&damaged).is_err(),
            "offset {offset} value {value}"
        );
    }
    for end in 0..bytes.len() {
        assert!(Frame::decode(&bytes[..end]).is_err());
    }
    let mut extra = bytes.clone();
    extra.push(0);
    assert!(Frame::decode(&extra).is_err());
    assert!(
        Frame {
            command: Command::Stats,
            flags: 1,
            sequence: 1,
            payload: vec![0]
        }
        .encode()
        .is_err()
    );
    assert!(
        Frame {
            command: Command::Stats,
            flags: 3,
            sequence: 1,
            payload: vec![0, 0]
        }
        .encode()
        .is_err()
    );
    assert!(
        Frame {
            command: Command::Stats,
            flags: 1,
            sequence: 1,
            payload: vec![3, 0]
        }
        .encode()
        .is_err()
    );
    assert!(
        Frame {
            command: Command::Stats,
            flags: 3,
            sequence: 1,
            payload: vec![3, 0, 0]
        }
        .encode()
        .is_err()
    );
    assert!(
        Frame {
            command: Command::Stats,
            flags: 3,
            sequence: 1,
            payload: vec![10, 0]
        }
        .encode()
        .is_err()
    );
    assert!(
        Frame {
            command: Command::Stats,
            flags: 1,
            sequence: 1,
            payload: vec![0; 257]
        }
        .encode()
        .is_err()
    );
}

#[test]
fn typed_tlvs_preserve_unknown_and_decode_identity() {
    let fields = decode_tlvs(&[
        1, 4, 0x78, 0x56, 0x34, 0x12, 0x20, 1, 1, 0xee, 3, 1, 2, 3, 0x11, 17, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    ])
    .unwrap();
    assert_eq!(fields[0].value, TelemetryValue::U32(0x12345678));
    assert_eq!(fields[1].value, TelemetryValue::U8(1));
    assert_eq!(fields[2].tag, 0xee);
    assert_eq!(fields[2].value, TelemetryValue::Bytes(vec![1, 2, 3]));
    assert_eq!(
        fields[3].value,
        TelemetryValue::Identity(hc32_updater::debug_link::BqIdentity {
            device_type: 0x0201,
            device_number: 0x0403,
            firmware_version: 0x0605,
            firmware_build: 0x0807,
            firmware_type: 9,
            impedance_track_version: 0x0b0a,
            reserved: 12,
            execution_context: 13,
            hardware_version: 0x0f0e,
            chemistry_id: 0x1110,
        })
    );
    assert!(decode_tlvs(&[0x11, 2, 9, 8]).is_err());
    for bytes in [&[1][..], &[1, 4, 0], &[1, 1, 0], &[0x20, 2, 0, 0]] {
        assert!(decode_tlvs(bytes).is_err());
    }
}

#[test]
fn battery_tlv_decodes_signed_current_and_derived_values() {
    let words: [u16; 11] = [
        0x6001,
        7920,
        (-1200i16) as u16,
        3015,
        76,
        2340,
        3100,
        8400,
        2000,
        0x40c0,
        12,
    ];
    let mut bytes = vec![0x51, 22];
    for word in words {
        bytes.extend_from_slice(&word.to_le_bytes());
    }
    let fields = decode_tlvs(&bytes).unwrap();
    assert_eq!(fields[0].name(), "battery_0x0c");
    let TelemetryValue::Battery(battery) = fields[0].value else {
        panic!("expected battery value");
    };
    assert_eq!(battery.current_ma, -1200);
    assert_eq!(battery.capacity(), (2340, 3100, "mAh"));
    assert_eq!(battery.power_mw(), -9504);
    assert!((battery.temperature_c() - 28.35).abs() < 0.01);
    assert_eq!(battery.state(), "Discharging");
    assert_eq!(battery.alarms(), ["terminate charge"]);
    let mwh = hc32_updater::debug_link::BatteryStatus {
        battery_mode: 0x8000,
        current_ma: 500,
        ..battery
    };
    assert_eq!(mwh.capacity(), (23400, 31000, "mWh"));
    assert_eq!(mwh.state(), "Charging");
    assert!(decode_tlvs(&[0x50, 2, 0, 0]).is_err());
}

#[test]
fn charger_tlv_and_reset_cause_decode() {
    // 10-bit ADC values in bits 15:6: battery 672 * 12.5 mV, input 200 * 25 mV, input current 160 * 6.25 mA.
    let word = |value: u16| (value << 6).to_le_bytes();
    let mut bytes = vec![0x12, 12, 0x08, 0x00];
    for value in [672, 670, 80, 200, 160] {
        bytes.extend_from_slice(&word(value));
    }
    let fields = decode_tlvs(&bytes).unwrap();
    let TelemetryValue::Charger(charger) = fields[0].value else {
        panic!("expected charger value");
    };
    assert_eq!(charger.charge_state(), "Fast charge");
    assert_eq!(charger.battery_voltage_mv(), 8400.0);
    assert_eq!(charger.charge_current_ma(), 1000.0);
    assert_eq!(charger.input_voltage_mv(), 5000.0);
    assert_eq!(charger.input_current_ma(), 1000.0);
    assert_eq!(charger.input_power_mw(), 5000.0);
    assert!(decode_tlvs(&[0x12, 2, 0, 0]).is_err());
    assert_eq!(
        hc32_updater::debug_link::reset_cause_names(0x4102),
        ["Reset pin", "Software", "Multiple causes"]
    );
    assert!(hc32_updater::debug_link::reset_cause_names(0).is_empty());
}

#[test]
fn config_list_pages_using_entry_count_and_rejects_nonprogress() {
    let mut script = Script::frames([
        response(Command::ConfigList, 1, &page(3, &[entry(1, "one", 1)])),
        response(
            Command::ConfigList,
            2,
            &page(3, &[entry(2, "two", 2), entry(3, "three", 3)]),
        ),
    ]);
    let entries = DebugClient::new(&mut script, Duration::from_secs(1))
        .config_list()
        .unwrap();
    assert_eq!(
        entries
            .iter()
            .map(|entry| (entry.key, entry.current))
            .collect::<Vec<_>>(),
        [(1, 1), (2, 2), (3, 3)]
    );
    assert_eq!(
        script
            .sent
            .iter()
            .map(|frame| frame.payload.clone())
            .collect::<Vec<_>>(),
        [vec![0], vec![1]]
    );
    assert!(ConfigPage::decode(&[1, 0], 0).is_err());
    assert!(ConfigPage::decode(&[1, 1], 0).is_err());
    assert!(ConfigPage::decode(&[1, 0], 2).is_err());
    assert!(ConfigPage::decode(&[0, 0, 1], 0).is_err());
    let empty = ConfigPage::decode(&[0, 0], 0).unwrap();
    assert_eq!(empty.total, 0);
    for offset in [2, 3, 20, 21] {
        let mut bad = entry(1, "name", 0);
        bad[offset] = 0xff;
        assert!(ConfigPage::decode(&page(1, &[bad]), 0).is_err());
    }
}

#[test]
fn config_list_rejects_changed_total_and_duplicate_keys() {
    for second in [
        page(3, &[entry(2, "two", 0)]),
        page(2, &[entry(1, "two", 0)]),
    ] {
        let script = Script::frames([
            response(Command::ConfigList, 1, &page(2, &[entry(1, "one", 0)])),
            response(Command::ConfigList, 2, &second),
        ]);
        assert!(
            DebugClient::new(script, Duration::from_secs(1))
                .config_list()
                .is_err()
        );
    }
}

#[test]
fn queues_interleaved_events_in_order_including_before_hello() {
    let script = Script::frames([
        telemetry(5),
        log(6),
        response(Command::Hello, 1, &[1, 0, 0, 1, 63, 0, 0, 0, 1, 0]),
        telemetry(7),
    ]);
    let mut client = DebugClient::new(script, Duration::from_secs(1));
    client.hello().unwrap();
    assert!(matches!(
        client.poll_event(Duration::ZERO).unwrap(),
        Some(Event::Telemetry { sequence: 5, .. })
    ));
    assert!(
        matches!(client.poll_event(Duration::ZERO).unwrap(),Some(Event::Log {sequence:6,level:2,uptime_ms:42,text}) if text == "ok")
    );
    assert!(matches!(
        client.poll_event(Duration::from_secs(1)).unwrap(),
        Some(Event::Telemetry { sequence: 7, .. })
    ));
    assert_eq!(client.poll_event(Duration::from_secs(1)).unwrap(), None);
}

#[test]
fn mutations_are_never_resent_after_receive_or_send_timeout() {
    for command in [Command::ConfigSet, Command::ConfigReset, Command::Reboot] {
        let mut script = Script::default();
        let mut client = DebugClient::new(&mut script, Duration::from_secs(1));
        let result = match command {
            Command::ConfigSet => client.config_set(1, 1).map(|_| ()),
            Command::ConfigReset => client.config_reset(),
            _ => client.reboot(),
        };
        assert!(matches!(result,Err(Error::Io(error)) if error.kind() == io::ErrorKind::TimedOut));
        assert_eq!(script.sent.len(), 1);
        assert_eq!(script.sent[0].command, command);
    }
    struct SendTimeout(std::rc::Rc<std::cell::Cell<usize>>);
    impl Transport for SendTimeout {
        fn send(&mut self, _: &[u8], _: Duration) -> io::Result<()> {
            self.0.set(self.0.get() + 1);
            Err(io::ErrorKind::TimedOut.into())
        }
        fn receive(&mut self, _: Duration) -> io::Result<Vec<u8>> {
            panic!("receive after send timeout")
        }
    }
    let sends = std::rc::Rc::new(std::cell::Cell::new(0));
    let mut client = DebugClient::new(SendTimeout(sends.clone()), Duration::from_secs(1));
    assert!(client.config_set(1, 1).is_err());
    assert_eq!(sends.get(), 1);
}

#[test]
fn idempotent_timeout_retry_and_late_response_drain() {
    let mut script = Script::frames([
        response(Command::Stats, 1, &[]),
        response(Command::Stats, 1, &[]),
        response(Command::ConfigGet, 2, &7i32.to_le_bytes()),
    ]);
    script.reads.push_front(Err(io::ErrorKind::TimedOut.into()));
    let mut client = DebugClient::new(&mut script, Duration::from_secs(1));
    assert!(client.stats().unwrap().is_empty());
    assert_eq!(client.config_get(1).unwrap(), 7);
    assert_eq!(script.sent.len(), 3);
    assert_eq!(script.sent[0], script.sent[1]);
}

#[test]
fn validates_register_stream_and_typed_response_boundaries() {
    let mut script = Script::frames([
        response(Command::RegRead, 1, &[1, 2]),
        response(Command::ConfigGet, 2, &[0]),
    ]);
    let mut client = DebugClient::new(&mut script, Duration::from_secs(1));
    assert!(client.stream_set(99, 1).is_err());
    assert!(client.stream_set(5001, 1).is_err());
    assert!(client.stream_set(100, 4).is_err());
    assert!(client.reg_read(4, 0, 1).is_err());
    assert!(client.reg_read(0, 0, 0).is_err());
    assert!(client.reg_read(0, 0, 33).is_err());
    assert!(client.reg_read(0, 0, 3).is_err());
    assert!(client.config_get(1).is_err());
    assert_eq!(script.sent.len(), 2);
    let mut script = Script::frames([Frame {
        command: Command::ConfigReset,
        flags: 3,
        sequence: 1,
        payload: vec![Status::Locked as u8, 0],
    }]);
    assert!(matches!(
        DebugClient::new(&mut script, Duration::from_secs(1)).config_reset(),
        Err(Error::Device(Status::Locked))
    ));
    assert_eq!(script.sent.len(), 1);
}

#[test]
fn malformed_event_and_unexpected_response_are_errors() {
    let mut bad = log(1);
    bad.payload[5] = 3;
    assert!(Event::decode(bad).is_err());
    let script = Script::frames([response(Command::Stats, 2, &[])]);
    assert!(
        DebugClient::new(script, Duration::from_secs(1))
            .stats()
            .is_err()
    );
}

#[test]
fn rejects_invalid_request_shapes_and_bounds_event_queue() {
    for (command, payload) in [
        (Command::Hello, vec![0]),
        (Command::ConfigList, vec![]),
        (Command::ConfigGet, vec![0]),
        (Command::ConfigSet, vec![0; 5]),
        (Command::ConfigReset, vec![0]),
        (Command::StreamSet, vec![99, 0, 1, 0, 0, 0]),
        (Command::StreamSet, vec![100, 0, 4, 0, 0, 0]),
        (Command::Unlock, vec![2, b'A']),
        (Command::RegRead, vec![4, 0, 1]),
        (Command::RegRead, vec![0, 0, 33]),
        (Command::Reboot, vec![1]),
    ] {
        assert!(
            Frame {
                command,
                flags: 0,
                sequence: 1,
                payload
            }
            .encode()
            .is_err()
        );
    }
    let mut frames: Vec<_> = (0..257).map(telemetry).collect();
    frames.push(response(Command::Stats, 1, &[]));
    let mut client = DebugClient::new(Script::frames(frames), Duration::from_secs(1));
    client.stats().unwrap();
    assert_eq!(client.events_dropped, 1);
    assert!(matches!(
        client.poll_event(Duration::ZERO).unwrap(),
        Some(Event::Telemetry { sequence: 1, .. })
    ));
}

#[test]
fn poll_event_treats_a_cancelled_transfer_as_timeout() {
    // nusb maps an expired blocking transfer to Cancelled, surfaced as Interrupted.
    let mut script = Script::default();
    script
        .reads
        .push_back(Err(io::ErrorKind::Interrupted.into()));
    let mut client = DebugClient::new(script, Duration::from_secs(1));
    assert_eq!(client.poll_event(Duration::from_secs(1)).unwrap(), None);
}
