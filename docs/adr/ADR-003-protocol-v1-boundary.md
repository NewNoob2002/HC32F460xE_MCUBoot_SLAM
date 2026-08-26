# ADR-003: Protocol V1 Boundary and Stop-and-Wait Model

Status: Accepted

Date: 2026-08-26

## Context

G2 provides a portable logical Secondary Storage contract and separate MCUboot
Boot-Control contract. The repository has no Protocol, update Manager or
Transport implementation. Phase 3 must prove that arbitrary byte chunks can be
parsed and applied without importing USB/UART/CAN, MCUboot or HC32 dependencies.

Freezing a complete transport backend vtable against only a fake would create
speculative lifecycle and callback semantics before the first real USB backend.
Conversely, assuming one read equals one frame would make USB Bulk/UART/CAN
integration unsafe and force protocol rewrites.

The unpadded signed application image can end on a non-Flash-aligned byte. Phase
3 must preserve its exact logical size/CRC while using the aligned Phase 2
Storage write contract.

## Decision

- Protocol V1 uses the fixed envelope, command IDs and limits in
  `docs/protocol/PROTOCOL_V1.md`.
- V1 is stop-and-wait with one request in flight, strict contiguous DATA and
  byte-identical duplicate response replay.
- Protocol framing is a byte-stream parser. Transport packet, USB transfer,
  UART read and CAN frame boundaries have no protocol meaning.
- Phase 3 exposes the smallest protocol-facing ingress/egress contract:
  feed borrowed RX bytes, inspect/consume manager-owned TX bytes, notify
  disconnect/TX-idle and poll with an explicit monotonic time value.
- Phase 3 does not add a production transport backend registry, factory or
  lifecycle vtable. A test-only fake byte driver proves split/coalesced input,
  short TX consumption, disconnect and TX-drain ordering. The first real
  transport in Phase 4/5 freezes only the lifecycle operations it actually
  needs.
- Protocol code owns frame encode/decode, CRC and incremental parsing only.
  Manager code owns command/state/sequence/duplicate/timeout behavior and calls
  Storage and Boot Control.
- The Manager emits a reset action only after a successful COMMIT response is
  fully consumed and TX idle is reported. Portable code does not call a
  platform reset API.
- The exact unpadded image size and image CRC belong to BEGIN. The Manager pads
  only the last physical Storage write with the erased value and verifies CRC
  by reading back only logical image bytes.
- CRC is transfer integrity, not authentication. MCUboot signature validation
  remains the trust boundary.

## Alternatives

### Freeze a complete Transport vtable in Phase 3

Deferred because only a fake would implement it. USB endpoint lifecycle,
short-write ownership and close/abort behavior should be frozen with the first
real backend rather than guessed.

### One transport packet equals one protocol frame

Rejected because USB Bulk and UART split/coalesce bytes and CAN requires a
separate segmentation adapter.

### MCUmgr/SMP in Phase 3

Deferred. It adds CBOR, management groups and ecosystem constraints before the
project has proven its Storage/Manager boundary. It can be evaluated later as
another Protocol implementation without changing Storage or Transport.

### XMODEM/YMODEM as the common protocol

Rejected as the framework protocol because update metadata, device capability,
logical offsets, structured errors and Boot-Control lifecycle would remain
outside the transfer protocol.

### Windowed or resumable transfer in V1

Rejected until the stop-and-wait implementation has measured throughput and
Phase 6 has defined persistent, integrity-protected resume metadata.

### Manager calls reset immediately after COMMIT

Rejected because the successful response may still be buffered in a USB/UART/
CAN backend and the Host could observe an unexplained disconnect.

## Consequences

Positive:

- Parser and Manager are fully host-testable without hardware or an I/O stack.
- No fake-only transport abstraction is promoted to production API.
- Duplicate COMMIT/DATA handling can prove side effects occur once.
- The same byte protocol can later run over USB, UART and CAN adapters.
- Unaligned final signed-image bytes are handled without changing Storage.

Costs and limits:

- Stop-and-wait throughput may be lower than a windowed protocol.
- Concrete Transport start/stop/capability operations remain a Phase 4/5 task.
- Host-declared hardware/board metadata is only an early check; signed image
  compatibility metadata needs a separate decision before G5.
- Application orchestration must bridge the Manager byte contract to the real
  Transport and execute emitted reset actions.

## Acceptance condition

Accepted on 2026-08-26 after review of the wire format, Manager/Protocol
ownership, duplicate-side-effect rules, final-write padding and host-only G3
scope. Production Phase 3 headers remain deferred until Phase 3B.
