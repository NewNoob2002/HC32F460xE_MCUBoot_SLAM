# Debug Link Protocol V1

Status: Implemented in Application firmware; hardware validation pending

Version: 1.0

Date: 2026-10-28

This document is the wire contract shared by the Application firmware and the
host updater for the long-lived debug/maintenance connection. It is independent
of [Firmware Update Protocol V1](PROTOCOL_V1.md); the update interface, its
commands and its Boot Recovery behavior are unchanged.

## Scope and trust boundary

- Application only. Boot Recovery exposes no debug interface.
- Exposes bounded telemetry, logs, statistics, whitelisted read-only device
  registers and a typed, range-checked settings table.
- Never exposes arbitrary memory/Flash reads or writes, physical addresses or
  firmware-update operations. No command can bypass image validation.
- Every request field is untrusted and validated before use. Unknown commands,
  keys and devices are rejected.
- Mutating commands (`CONFIG_SET`, `CONFIG_RESET`, `REBOOT`) require
  maintenance mode (`UNLOCK`). Unlock is an accidental-change guard, not
  authentication.

## USB binding

| Item | Value |
| --- | --- |
| Interface | 1 (interface 0 remains the firmware-update interface) |
| Class/Subclass/Protocol | `0xFF/0x00/0x00` |
| Interface string | `Slam-ChargerManager Debug Monitor` (host also accepts the earlier `Vendor Bulk Debug`) |
| Bulk OUT | `0x04`, 64-byte max packet |
| Bulk IN | `0x83`, 64-byte max packet |

The Application device becomes a two-interface composite device. The Microsoft
OS 2.0 descriptor set gains one function subset per interface, both WinUSB.
Boot Recovery descriptors stay single-interface.

The device keeps OUT armed independently of IN. Responses and events share IN
through a small fixed frame queue. A response always has a reserved slot;
events are dropped (and counted) when the queue is full, so a host that stops
reading can never stall the main loop or the update interface. Bus reset or
SET_CONFIGURATION clears the queue, stops streaming and leaves maintenance mode.

An IN frame whose length is a multiple of 64 bytes is terminated by a
zero-length packet. Hosts size reads from the header and must discard a
zero-length transfer received before the next frame's first byte.

## Frame envelope

Identical to Protocol V1 except for the magic, flags and payload limit.

| Offset | Size | Field | Rule |
| --- | ---: | --- | --- |
| 0 | 4 | magic | ASCII `DBG1` |
| 4 | 1 | major | `1` |
| 5 | 1 | minor | `0` |
| 6 | 1 | command | Command ID below |
| 7 | 1 | flags | See below |
| 8 | 4 | sequence | Request sequence echoed in the response; event counter for events |
| 12 | 2 | payload_length | `0..256` |
| 14 | 2 | reserved | `0` |
| 16 | N | payload | Command-specific |
| 16+N | 4 | crc32 | CRC-32/ISO-HDLC over header and payload (same as Protocol V1) |

Flags: `0x00` request, `0x01` response, `0x03` error response, `0x04` event.
Any other value is a format error. Little-endian integers throughout; no packed
struct overlays.

Responses start with a `u16 status`; the body follows only when status is OK.
The host keeps one request in flight; events may arrive before its response. The device does not keep a duplicate cache: requests are idempotent
except `CONFIG_SET`/`CONFIG_RESET`/`REBOOT`, which a host must not resend blindly.

Status codes: `0 OK`, `1 BAD_FRAME`, `2 UNKNOWN_COMMAND`, `3 LOCKED`,
`4 UNKNOWN_KEY`, `5 OUT_OF_RANGE`, `6 STORAGE_ERROR`, `7 DEVICE_ERROR`,
`8 BUSY`, `9 INTERNAL_ERROR`.

## Commands

| ID | Name | Request | Response body |
| --- | --- | --- | --- |
| `0x01` | HELLO | empty | `u8 major, u8 minor, u16 max_payload, u32 capabilities, u16 telemetry_version` |
| `0x02` | TELEMETRY_GET | empty | telemetry TLV record |
| `0x03` | STREAM_SET | `u16 period_ms, u32 mask` | empty |
| `0x10` | CONFIG_LIST | `u8 start_index` | `u8 total, u8 count`, then `count` entries |
| `0x11` | CONFIG_GET | `u16 key` | `i32 value` |
| `0x12` | CONFIG_SET | `u16 key, i32 value` | `i32 stored_value` |
| `0x13` | CONFIG_RESET | empty | empty |
| `0x20` | UNLOCK | `u8 len, len bytes serial` | `u16 timeout_s` |
| `0x30` | REG_READ | `u8 device, u8 reg, u8 len` | `len` bytes |
| `0x40` | REBOOT | `u8 mode` (`0` = normal only) | empty; reset after the response is sent, or after 1 s if the host does not read it |
| `0x41` | STATS | empty | stats TLV record |
| `0x80` | EVT_TELEMETRY | event | telemetry TLV record |
| `0x81` | EVT_LOG | event | `u8 level, u32 uptime_ms, u8 len, len bytes text` |

- HELLO stops streaming and leaves maintenance mode. Capability bits: `0`
  telemetry, `1` logs, `2` config, `3` register read, `4` reboot, `5` stats.
- STREAM_SET: `period_ms` is `0` (off) or `100..5000`; `mask` bit0 telemetry,
  bit1 logs; other bits must be zero. The period paces telemetry; logs are sent
  as soon as they are queued while streaming is on.
- EVT_LOG `level` follows EasyLogger: `0` assert, `1` error, `2` warn, `3` info,
  `4` debug, `5` verbose.
- CONFIG_LIST entry: `u16 key, u8 type (0=i32, 1=bool), u8 flags (bit0 persisted,
  bit1 requires unlock, bit2 applied-to-hardware), i32 min, i32 max, i32 default,
  i32 current, u8 unit (0 none, 1 mA, 2 mV, 3 ms, 4 0.1 degC), u8 name_len,
  name ASCII`. As many whole entries as fit; the host continues from
  `start_index + count`.
- UNLOCK: the serial must equal the provisioned device serial (or the USB
  serial string of an unprovisioned device). Maintenance mode ends on HELLO,
  bus reset, or 60 s without a request.
- REG_READ devices: `0` MP2762A `0x5C`, `1` BQ40Z50 `0x0B`, `2` BQ40Z50 `0x0C`,
  `3` HUSB238 `0x08`. `len` is `1..32`. Reads only; there is no register write.
- Telemetry and stats are TLV sequences `u8 tag, u8 len, value`, so new fields
  are additive. Hosts ignore unknown tags. Initial tags: `0x01 uptime_ms u32`,
  `0x02 loop_count u32`, `0x10 power probe mask u8` (bit per device in REG_READ
  order), `0x11 BQ identity block` (as retained by the Power node), `0x12
  MP2762A registers`, `0x20
  charger PG u8`, `0x21 charger INT# u8`, `0x22 charger change count u32`,
  `0x50`/`0x51 BQ SBS status` (packs `0x0B`/`0x0C`), `0x30 update manager
  state u8`, `0x31 usb update errors u32`, `0x40 debug
  events dropped u32`, `0x41 last reset cause u32`. `0x42 i2c bus recoveries u32`. Charger control tags
  (2026-11-04): `0x13` MP2762A read-back (7 bytes: `u8 REG00H, u8 REG0FH, u8
  REG02H, u8 REG04H, u8 REG08H, u16 REG24..25H`; junction code in bits 15:6, T(C) = (903 -
  code) / 2.578), `0x14` charge decision (12 bytes: `u8 mode` (0 active, 1
  suspended without a pack, 2 wake probe), `u8 allowed, u16 target_mV, u16
  target_mA, u16 input_limit_mA, u16 source_limit_mA, i16 control_result`) and
  `0x15` HUSB238 `PD_STATUS0, PD_STATUS1`. `0x13` and `0x15` are omitted when
  the device did not answer.

## Settings table (initial)

Keys `0x0001`-`0x0005` persist in the FlashDB `app_kv` partition and change
only through CONFIG_SET/CONFIG_RESET; firmware updates never rewrite them. At
startup the Application loads them into RAM: a value outside the current range
loads as its default in RAM while Flash keeps the stored value until the host
writes a new one. CONFIG_SET validates the range, saves to Flash and publishes
the new value in the same request.

Keys `0x0001`-`0x0004` (flags `0x07`) are applied to the MP2762A once per
second while the J2 pack (BQ40Z50 `0x0B`) answers: charge voltage =
min(pack ChargingVoltage, `charge_voltage_limit`, 8400 mV), charge current =
min(pack ChargingCurrent, `charge_current_limit`). The input current limit is
min(`input_current_limit`, 90% of the source limit), written to both
IIN_LIM1 (REG00H) and IIN_LIM2 (REG0FH) so the two-level limit cannot lift
the average above it, and is also applied at
startup and whenever the DC/DC runs without a pack. The source limit is the
HUSB238 PD contract current, else its Type-C 5 V advertisement, else 500 mA
(USB 2.0 default, also used when the HUSB238 does not answer). With a responding
pack, charging is enabled only when `charge_enable` is 1, the capped current
request is at least 180 mA, the voltage is supported and BatteryStatus has none
of OCA, TCA or OTA. REG02H rounds fast-charge current down in 50 mA steps;
REG03H pre-charge current is explicitly held at 180 mA while preserving the
termination setting. The MP2762A cannot pre-charge below 180 mA (datasheet
Rev 1.1, pages 24 and 33), so a lower cap or pack request disables charging
across all phases, even if the charger currently reports fast charge.
The advertised 100..2000 mA settings range is retained; 100..179 mA means no
charging. Without a responding pack, debounce and wake windows also honor
`charge_enable` and this current floor, and apply the configured current cap
before enabling charging. Explicit stop requests run before input-limit writes;
any failed limit/control write triggers a stop attempt and reports the error.
A bus failure that also prevents the stop write cannot guarantee physical
shutdown. The output-only pack at `0x0C` is monitored but never charged.

`led_override` (`0x0006`, flags `0x06`) is runtime-only: it never reaches Flash,
returns to `0` on every reset and on CONFIG_RESET, and is applied to the status
LEDs immediately. `0` keeps the normal battery/update pattern; `1` off, `2` red,
`3` blue and `4` red+blue hold the LEDs steady for power measurement. The panic
pattern (solid red) still overrides it.

| Key | Name | Unit | Min | Max | Default |
| --- | --- | --- | ---: | ---: | ---: |
| `0x0001` | charge_enable | bool | 0 | 1 | 1 |
| `0x0002` | charge_current_limit | mA | 100 | 2000 | 2000 |
| `0x0003` | charge_voltage_limit | mV | 7425 | 8400 | 8400 |
| `0x0004` | input_current_limit | mA | 100 | 3000 | 1500 |
| `0x0005` | telemetry_period | ms | 100 | 5000 | 1000 |
| `0x0006` | led_override | none | 0 | 4 | 0 |

The voltage range is 2S (MP2762A REG04H starts at 7425 mV). Current and voltage
are caps on the pack's own request, so the defaults let the pack decide.

## Firmware implementation details

- The fixed IN queue has three event slots and one response slot (276 bytes
  each). A pending response is sent before queued events once any active IN
  transfer completes. OUT is independently armed in 64-byte transfers. Requests
  received while a previous response occupies its slot are discarded without
  executing commands; the host must obey the one-request-in-flight rule.
- Initial settings fit in one CONFIG_LIST page. `start_index == total` returns
  an empty page; larger indices return OUT_OF_RANGE. Wrong request sizes return
  BAD_FRAME; invalid values/devices/periods return OUT_OF_RANGE. A serial mismatch
  returns LOCKED. REG_READ whitelists device addresses, with all byte register
  indices available; no write command is exposed.
- Telemetry tag `0x11` is emitted only after a successful alternate-pack identity
  read. It is 17 bytes in this order: `u16 device_type`, `u16 device_number`,
  `u16 firmware_version`, `u16 firmware_build`, `u8 firmware_type`,
  `u16 impedance_track_version`, `u8 reserved`, `u8 execution_context`,
  `u16 hardware_version`, `u16 chemistry_id`. All words are little-endian;
  compiler structure padding is never transmitted. INT# telemetry is `1` when
  asserted. Tags `0x50 + slot` (slot 0 = `0x0B`, 1 = `0x0C`) carry 22 bytes,
  eleven little-endian SBS words read once per second: `BatteryMode 0x03`,
  `Voltage 0x09 mV`, `Current 0x0A mA` (signed, positive while charging),
  `Temperature 0x08 0.1 K`, `RelativeStateOfCharge 0x0D %`,
  `RemainingCapacity 0x0F`, `FullChargeCapacity 0x10`, `ChargingVoltage 0x15
  mV`, `ChargingCurrent 0x14 mA`, `BatteryStatus 0x16`, `CycleCount 0x17`.
  Capacities are mAh, or 10 mWh when BatteryMode bit 15 (CAPM) is set. A tag
  is emitted only when every word of that pack's last refresh was read; a
  missing tag means the pack is absent or unreadable. Tag `0x12` carries 12 raw
  MP2762A bytes read once per second (read-only): `u8 status 0x13`, `u8 fault
  0x14`, then little-endian words `0x16` battery voltage, `0x18` system voltage,
  `0x1A` charge current, `0x1C` input voltage, `0x1E` input current. Each word
  holds a 10-bit ADC value in bits 15:6; the host scales it by 12.5 mV, 12.5 mV,
  12.5 mA, 25 mV and 6.25 mA and reads the charge state from status bits 3:2
  (0 not charging, 1 pre-charge, 2 fast charge, 3 done). These weights come from
  the earlier `Libraries/Mp2762a` driver and must still be confirmed against the
  datasheet and the BQ40Z50 readings on V1.1. `0x41` is the raw `RMU_RSTF0`
  value; the flags stay latched until cleared, so several may be set. STATS currently returns the same complete TLV snapshot as telemetry.
- Logs use twenty fixed records (200 text bytes maximum per record; longer lines
  are truncated). Sources are the explicit `Service::log` hook and, in Debug
  builds, every EasyLogger line (`CORE_DEBUG_PRINTF`/`elog_*`) after its UART
  and RTT output, with ANSI color codes and line endings removed and the level
  taken from the `X/` prefix. Lines logged from an interrupt are not forwarded.
  Release builds have no EasyLogger, so only `Service::log` records exist.
- While log streaming is on, queued records are sent whenever an IN event slot is
  free; one slot stays reserved for telemetry when both streams are enabled.
  A full ring drops new records. Records survive USB reset/HELLO, while
  streaming and maintenance mode stop. Tag `0x40` includes both IN event drops
  and log-ring overflows.
- The telemetry_period setting is persisted and listed; STREAM_SET explicitly
  controls the active period and does not persist it or implicitly enable a stream.
- No DCD change is needed: HC32 configures six endpoints in each direction;
  FIFO3 already has 32 words for EP3 IN, and EP4 OUT uses the 128-word shared RX
  FIFO. The existing 320-word total FIFO budget is unchanged.
- The shared CherryUSB EP0 request buffer is 512 bytes to carry the 338-byte
  Application MS OS 2.0 descriptor set. Boot's descriptor bytes are unchanged.
