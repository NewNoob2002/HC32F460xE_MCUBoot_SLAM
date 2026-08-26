# Firmware Update Protocol V1

Status: Accepted for Phase 3 implementation

Version: 1.0

Date: 2026-08-26

This document is the accepted wire contract shared by device firmware, host
tests and the future PC updater. No USB, UART, CAN or MCU-specific behavior is
defined here.

## Design constraints

- Fixed-size, statically allocated parser and response buffers.
- Byte-stream safe: input may be split or coalesced at any byte boundary.
- One request in flight; no windowing or out-of-order DATA.
- No protocol-layer fragmentation. Future CAN segmentation belongs below this
  protocol at the Transport boundary.
- CRC detects transfer corruption only. MCUboot signature validation remains
  the image trust boundary.
- All offsets are logical image offsets. No frame contains a physical address,
  slot ID or Flash area ID.

## Byte order and frame envelope

All multi-byte integers use little-endian byte order.
Implementations encode/decode bytes explicitly and must not overlay packed C
structs on untrusted input.

| Offset | Size | Field | V1 rule |
| --- | ---: | --- | --- |
| 0 | 4 | magic | ASCII bytes `FWUP` |
| 4 | 1 | protocol_major | `1` |
| 5 | 1 | protocol_minor | `0` |
| 6 | 1 | command | Command ID below |
| 7 | 1 | flags | Request `0`; response flags below |
| 8 | 4 | sequence | Request/response sequence |
| 12 | 2 | payload_length | `0..512` |
| 14 | 2 | reserved | Must be zero in V1 |
| 16 | N | payload | Command-specific payload |
| 16 + N | 4 | frame_crc32 | CRC over header and payload, excluding this field |

Constants:

- Header size: 16 bytes.
- Maximum payload: 512 bytes.
- Maximum frame: 532 bytes.
- CRC: CRC-32/ISO-HDLC, reflected polynomial `0xEDB88320`, initial value
  `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. The check value for ASCII
  `123456789` is `0xCBF43926`.

The normative Phase 3A examples are `Tests/Protocol/golden_vectors.csv`.
`Tests/Protocol/verify_golden_vectors.py` reconstructs every frame from its
fields and must pass before codec implementation or vector changes are reviewed.

Response flags:

- `0x01 RESPONSE`;
- `0x02 ERROR`;
- all other bits are zero in V1.

A successful response uses `RESPONSE`. An unsuccessful response uses
`RESPONSE | ERROR`. A request with any nonzero flag is rejected.

## Commands

| ID | Command | Request payload | Successful response body after status |
| ---: | --- | --- | --- |
| `0x01` | HELLO | Empty | max payload, capability bits and timeout |
| `0x02` | DEVICE_INFO | Empty | bounded numeric device/update information |
| `0x10` | BEGIN | image metadata | Empty |
| `0x11` | DATA | logical offset and bytes | next expected logical offset |
| `0x12` | END | Empty | verified logical size and image CRC |
| `0x13` | COMMIT | Empty | Empty |
| `0x14` | ABORT | Empty | Empty |

Every response payload starts with a two-byte wire status. Separate ACK/NACK
commands are not needed: status zero is ACK and nonzero status is NACK.
The response uses the request command ID and sequence. An unsuccessful response
payload contains exactly `status:u16`; command-specific response fields are
present only when status is `OK`.
Response headers use the device's V1 version `1.0`, including an
`INCOMPATIBLE_VERSION` response to a request using another version.

### HELLO

Request payload: empty.

Response payload:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | status |
| 2 | 2 | maximum payload, currently `512` |
| 4 | 4 | capability bits |
| 8 | 4 | active-session inactivity timeout in milliseconds |

V1 capability bits:

- bit 0: MCUboot test-upgrade request supported;
- bit 1: full-image readback CRC supported;
- bit 2: DATA is strict and sequential.

HELLO must use sequence zero and is accepted only in IDLE. A successful HELLO
starts a protocol session and sets the next expected sequence to one.
The V1 default timeout is 5000 ms; the response reports the actual
configured value.

### DEVICE_INFO

Request payload: empty.

Response payload:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 2 | status |
| 2 | 2 | board revision |
| 4 | 4 | hardware ID |
| 8 | 4 | board ID |
| 12 | 4 | logical Secondary image capacity |
| 16 | 4 | Storage write alignment |
| 20 | 4 | Storage erase alignment |
| 24 | 8 | Application version |
| 32 | 8 | Bootloader version |

An eight-byte version is `major:u8, minor:u8, revision:u16, build:u32`.
DEVICE_INFO is informational and does not mutate update state.

### BEGIN

Request payload:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | exact unpadded signed-image size |
| 4 | 4 | CRC-32 of those logical image bytes |
| 8 | 4 | target hardware ID |
| 12 | 4 | target board ID |
| 16 | 2 | target board revision |
| 18 | 2 | reserved, must be zero |
| 20 | 8 | image version |

The device validates protocol state, nonzero size, aligned-up size against
logical Storage capacity and configured hardware/board IDs before erase. It
then performs the backend-controlled full Secondary erase.

Hardware/board values supplied by the Host are an early compatibility check,
not signed proof. A signed image-compatibility metadata policy must be approved
before G5; MCUboot signature validation still rejects any modified image.

### DATA

Request payload:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | logical image offset |
| 4 | 1..508 | image bytes |

Rules:

- Offset equals the next expected logical offset; V1 does not allow gaps,
  overlap or out-of-order DATA.
- Offset plus data length is checked without integer wrap and does not exceed
  the BEGIN image size.
- DATA length is independent of Storage write alignment. The Manager coalesces
  bytes in a bounded staging buffer and submits only aligned prefixes to
  Storage.
- When the logical image ends, the Manager pads only the final physical write
  with the Storage erased value; padding is not included in image size or CRC.
- The successful response contains `status:u16` and `next_offset:u32`.

### END

Request payload: empty.

END is accepted only after exactly the BEGIN image size has been received. The
Manager reads back exactly the logical image bytes in bounded blocks, computes
CRC-32 and compares it with BEGIN metadata. A successful response contains
`status:u16, verified_size:u32, image_crc32:u32` and enters READY_TO_COMMIT.

END verifies transfer/storage integrity. It does not claim MCUboot signature
trust and does not mark the image pending.

### COMMIT

Request payload: empty.

COMMIT is accepted only in READY_TO_COMMIT. It calls the Boot-Control test
upgrade operation once. The device queues a successful response, waits until
all response bytes have been consumed and the Transport reports TX idle, then
emits a reset action to Application orchestration. Protocol code does not call
a platform reset API directly.

### ABORT

Request payload: empty.

ABORT is accepted before a successful COMMIT in NEGOTIATING, RECEIVING,
READY_TO_COMMIT and ERROR. It clears volatile session/parser state and returns
to IDLE. It does not perform another Flash erase and never calls Boot Control.
Partial candidate bytes may remain but are not pending because BEGIN erased
stale trailer state. ABORT is rejected after Boot Control has successfully
marked the image pending.

## Wire status values

| Value | Name | Meaning |
| ---: | --- | --- |
| 0 | OK | Request completed |
| 1 | BAD_FRAME | Header, flags, reserved field or payload shape invalid |
| 2 | INCOMPATIBLE_VERSION | Protocol major/minor cannot be used |
| 3 | UNSUPPORTED_COMMAND | Command ID is not supported |
| 4 | BAD_SEQUENCE | Unexpected sequence or conflicting duplicate |
| 5 | INVALID_STATE | Command is not allowed in current state |
| 6 | INVALID_ARGUMENT | Metadata or command argument invalid |
| 7 | IMAGE_TOO_LARGE | Logical or aligned image cannot fit |
| 8 | OFFSET_MISMATCH | DATA is not the next contiguous range |
| 9 | STORAGE_ERROR | Erase/write/read failed |
| 10 | VERIFY_ERROR | Received length or readback CRC failed |
| 11 | BOOT_CONTROL_ERROR | Pending request failed |
| 12 | TIMEOUT | Session inactivity limit reached |
| 13 | INTERNAL_ERROR | Bounded internal invariant failed |

Wire statuses are stable protocol values. They are mapped from, but are not the
numeric values of, internal `enum fw_update_result`.

## Sequence, duplicate and retry rules

- One request is active at a time.
- Every well-formed request with the expected sequence produces one response.
- After that response is created, the expected sequence advances even when the
  command returns a semantic error.
- Bad CRC, incompatible HELLO version and bad sequence do not advance it.
- A byte-identical repeat of the immediately previous accepted request replays
  the byte-identical cached response without repeating Storage, verification,
  Boot-Control or reset side effects.
- Reuse of the previous sequence with different command/payload is BAD_SEQUENCE.
- Three consecutive CRC/sequence/frame errors abort the volatile session. This
  limit is compile-time bounded and resets after a valid expected request.
- Host retry count is bounded by the host implementation; the device does not
  implement a retransmission window.

Disconnect/timeout before successful COMMIT returns the volatile session to
IDLE and leaves no pending request. After successful COMMIT, disconnect or
timeout cannot undo MCUboot trailer state; V1 emits no automatic RESET unless
the successful response was fully consumed and TX idle was reported.

## Incremental parser behavior

- No allocation and no pointer retained from caller-owned RX bytes.
- Search for the four magic bytes with bounded linear work.
- Reject payload lengths above 512 before copying payload bytes.
- Accept every split point and multiple coalesced frames.
- Hold at most one completed request while its response is pending. The caller
  retains any unconsumed coalesced bytes and retries after TX progress.
- CRC failure never invokes command/session/storage logic and produces no
  response because command/sequence integrity is not established; the Host
  retries after its bounded timeout.
- Garbage before magic is discarded; no response is generated when a reliable
  command/sequence cannot be recovered.

## Version compatibility

- Major must match exactly.
- V1 device minor is zero. Requests with a higher minor fail closed.
- A future minor may add commands, response fields or capability bits only when
  old field offsets and existing semantics remain unchanged.
- Unknown flags and nonzero reserved fields are rejected in V1.

## Explicit V1 exclusions

- Resume after reset or disconnect.
- Sliding windows or multiple in-flight requests.
- Protocol-layer fragmentation/reassembly.
- Compression, encryption or transport authentication.
- Arbitrary memory/Flash read or write.
- Permanent-upgrade request.
- General RPC or product-specific commands.
- Device-side MCUboot signature validation before COMMIT.
