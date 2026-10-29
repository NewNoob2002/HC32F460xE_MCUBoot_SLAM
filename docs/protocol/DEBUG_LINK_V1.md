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
| Interface string | `Vendor Bulk Debug` |
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
| `0x40` | REBOOT | `u8 mode` (`0` = normal only) | empty; reset after the response is sent |
| `0x41` | STATS | empty | stats TLV record |
| `0x80` | EVT_TELEMETRY | event | telemetry TLV record |
| `0x81` | EVT_LOG | event | `u8 level, u32 uptime_ms, u8 len, len bytes text` |

- HELLO stops streaming and leaves maintenance mode. Capability bits: `0`
  telemetry, `1` logs, `2` config, `3` register read, `4` reboot, `5` stats.
- STREAM_SET: `period_ms` is `0` (off) or `100..5000`; `mask` bit0 telemetry,
  bit1 logs; other bits must be zero.
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
  order), `0x11 BQ identity block` (as retained by the Power node), `0x20
  charger PG u8`, `0x21 charger INT# u8`, `0x22 charger change count u32`,
  `0x30 update manager state u8`, `0x31 usb update errors u32`, `0x40 debug
  events dropped u32`, `0x41 last reset cause u32`.

## Settings table (initial)

Settings persist in the FlashDB `app_kv` partition. Values outside the range
are rejected; on load an invalid or missing value falls back to its default.
Charger values are stored and published to the Charger node now; the
`applied-to-hardware` flag stays clear until the MP2762A driver applies them.

| Key | Name | Unit | Min | Max | Default |
| --- | --- | --- | ---: | ---: | ---: |
| `0x0001` | charge_enable | bool | 0 | 1 | 0 |
| `0x0002` | charge_current_limit | mA | 100 | 2000 | 500 |
| `0x0003` | charge_voltage_limit | mV | 12000 | 16800 | 16800 |
| `0x0004` | input_current_limit | mA | 100 | 3000 | 1500 |
| `0x0005` | telemetry_period | ms | 100 | 5000 | 1000 |

The charger ranges are conservative placeholders. They must be confirmed against
the battery pack and MP2762A specifications before the charger driver applies
them to hardware.

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
  asserted. STATS currently returns the same complete TLV snapshot as telemetry.
- Logs use twelve fixed records (72 text bytes maximum per record), supplied
  through the explicit `Service::log` application hook. At most one log event
  is emitted each stream period; CORE_DEBUG_PRINTF forwarding is not enabled.
  Records survive USB reset/HELLO, while streaming and maintenance mode stop.
  Tag `0x40` includes both IN event drops and log-ring overflows.
- The telemetry_period setting is persisted and listed; STREAM_SET explicitly
  controls the active period and does not persist it or implicitly enable a stream.
- No DCD change is needed: HC32 configures six endpoints in each direction;
  FIFO3 already has 32 words for EP3 IN, and EP4 OUT uses the 128-word shared RX
  FIFO. The existing 320-word total FIFO budget is unchanged.
- The shared CherryUSB EP0 request buffer is 512 bytes to carry the 338-byte
  Application MS OS 2.0 descriptor set. Boot's descriptor bytes are unchanged.
