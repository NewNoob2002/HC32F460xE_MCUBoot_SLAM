# Firmware Update Architecture

Status: G4 passed; Phase 5 Rust updater plan active

This document defines ownership and semantic boundaries. Phase 2 converted
Storage/Boot-Control semantics into tested C contracts, Phase 3 implemented the
accepted Protocol V1/Manager boundary, and Phase 4 verified the pinned
CherryUSB/HC32 Vendor Bulk backend without updater coupling.

## System ownership

```text
Boot firmware
└── MCUboot trust, validation, swap, rollback and handover

Application firmware
├── Product health and image confirmation policy
├── Firmware Update Manager
│   ├── lifecycle/state/error handling
│   └── coordinates Protocol, Storage, Boot Control and lifecycle actions
├── Protocol V1
│   ├── framing, commands, sequence, CRC and capabilities
│   └── consumes/produces transport-neutral byte chunks
├── Transport contract
│   ├── USB Vendor Bulk backend -> CherryUSB -> HC32 DCD
│   ├── UART backend
│   └── CAN/CAN FD backend
├── Storage contract -> MCUboot Secondary Slot backend -> Flash
├── Boot-Control contract -> MCUboot pending/confirmation APIs
└── Platform services -> clock/timer/reset and MCU-specific drivers
```

Product Application internals use the same downward-only rule:

```text
App/Core/main.c
  -> App/Services/power_devices
       -> App/Devices/{bq40z50, husb238, mp2762a}
            -> Platform/HC32F460/bsp_i2c2
  -> App/Diagnostics/app_diagnostics
       -> bounded startup/runtime snapshot and periodic Debug report
```

Device protocol/address ownership does not belong in `main.c` or the BSP. The
BSP exposes generic I2C transactions; the device and service layers select
addresses and interpret responses. Diagnostics records fixed-size state without
allocation, persistence or an RTOS.

Primary and Secondary are two slots used by one Application product. Application v1 and v2 each contain the updater version compiled into that image; there is one updater source tree, not `app1/` and `app2/`.

## Dependency rules

Allowed dependencies point downward only:

```text
Application orchestration
  -> FW Update Manager
       -> Protocol
       -> Storage contract
       -> Boot-Control contract
       -> explicit time and lifecycle-action semantics
  -> Protocol -> Transport contract
  -> Transport backend -> platform/backend driver
  -> Storage backend -> MCUboot flash-area port -> BSP Flash
  -> Boot-Control backend -> MCUboot public state APIs
```

Forbidden dependencies:

- Protocol or manager -> `hc32f460.h`, HC32 LL, CherryUSB or physical Flash address.
- Protocol -> concrete USB/UART/CAN backend.
- USB callback, UART ISR or CAN ISR -> Flash or MCUboot API.
- Transport -> MCUboot slot IDs/state.
- Storage -> USB/UART/CAN/Protocol.
- Host-supplied offset/address -> physical Flash address without Secondary-only translation and bounds checks.
- Full updater -> Boot firmware.

When portable source exists, CI must reject HC32/CMSIS peripheral includes from its directories and build it in the native HostTests configuration.

## V1 interface semantics

The names below describe required behavior, not final C identifiers. Phase 2/3 chooses the shortest API that satisfies the first real backend and fake.

### Transport

| Operation | V1 necessity and semantics |
| --- | --- |
| initialize/start | Configure one statically supplied backend context; no allocation or device discovery inside portable code |
| poll/process | Advance a bounded amount of backend work in Application context |
| receive | Return zero or more bytes/chunk; never promise that one call equals one protocol packet |
| send | Accept a bounded byte range and report accepted/progress/error explicitly |
| capabilities | Report maximum chunk/MTU hints and stream/framed/reliability traits without naming USB/UART/CAN |
| abort/stop | Cancel I/O, release endpoints/buffers and return to a known idle state |

V1 is cooperative and non-blocking at the manager boundary. ISR code may only move bytes/events into bounded backend buffers. Portable code owns no ISR callback and uses no `malloc/free`. Async completion registries and multiple simultaneous transports are future requirements.

The accepted Phase 3 boundary freezes only the protocol-facing byte ingress/egress,
disconnect, TX-idle and explicit-time semantics. It does not create a production
Transport backend registry or lifecycle vtable before the first real backend.
See accepted ADR-003 and the Phase 3 detailed plan.

USB Bulk and UART may split/coalesce data arbitrarily. CAN/CAN FD may present each payload as a chunk; transport adaptation segments outgoing bytes as required. The Protocol incremental parser remains correct for every chunk boundary.

### Protocol

Protocol V1 frames contain, at minimum:

- magic;
- protocol major/minor;
- command;
- flags;
- sequence;
- payload length;
- payload;
- frame CRC.

V1 commands:

- `HELLO`;
- `DEVICE_INFO`;
- `BEGIN` with image metadata and logical size;
- `DATA` with logical image offset;
- `END`, which performs readback verification;
- `COMMIT` for a test upgrade;
- `ABORT`;
- a common response status that provides ACK/NACK/error semantics.

V1 supports a single in-flight command, bounded retry, duplicate recognition and timeouts. It does not support resume after reset, sliding windows, arbitrary memory read/write, transport addresses, encrypted sessions or general-purpose RPC. Exact byte order, field widths, maxima and golden vectors are frozen in Phase 3 before parser implementation.

Future extensions include resume metadata, windowing, optional authentication, advanced capability negotiation and product-specific commands. Protocol major changes are incompatible; minor changes are additive and capability-gated.

### Storage

| Operation | Why it exists |
| --- | --- |
| capabilities | Supplies writable image capacity, write/erase alignment and erased value without exposing an address; capacity excludes MCUboot trailer reserve |
| erase all | V1 backend erases the complete Secondary Slot, including stale trailer metadata, without accepting a Host-controlled address/range |
| write(offset, data, length) | Writes logical image bytes with overflow/alignment checks |
| read(offset, data, length) | Enables readback/hash verification and host tests |

`open/close`, general partitions, physical addresses and arbitrary area IDs are excluded. V1 writes only the image region of Secondary; the final MCUboot trailer sector is never writable through logical Host offsets. Marking an image pending is not a Storage operation.

Phase 3 Manager owns `begin`, received-range accounting, readback/digest verification, finalize and abort session semantics. Internal HC32 Flash has no separate flush operation, so Phase 2 does not add a no-op API for one backend. Runtime transfer uses the signed, unpadded `app-signed-<version>.bin`; slot-padded `app-update-<version>.bin` remains a direct-programming/HIL artifact because it contains trailer state outside the logical writable region.

### Boot Control and platform lifecycle

Boot Control contains only MCUboot image-state operations:

- request a test upgrade after successful finalize;
- query/report relevant slot/pending state when safely available;
- confirm the running image after bounded Application health checks.

Permanent-upgrade request and security-counter policy are future decisions. Reset is a platform lifecycle service invoked only after a successful pending request and protocol response flush. Storage cannot call Boot Control, and Boot Control cannot write image data.

### Error model

A shared error domain must distinguish at least invalid argument/state, bounds, alignment, busy, timeout, transport, protocol/CRC/sequence, storage erase/write/read/verify, incompatible image, boot-control and internal failures. Public APIs must not expose unexplained `-1/-2/-3` values. Backends may retain native error details for diagnostics while mapping to the shared domain.

## Firmware-update state machine

```text
IDLE
  -> NEGOTIATING -> PREPARING -> RECEIVING -> VERIFYING
  -> READY_TO_COMMIT -> COMMITTING -> COMPLETED

Any active state -> ABORTED -> IDLE
Any unrecoverable failure -> ERROR -> ABORTED/IDLE after explicit cleanup
```

| State | Allowed behavior | Reboot / resume policy |
| --- | --- | --- |
| IDLE | Accept HELLO only; malformed input has no side effect | Reboot safe; no session |
| NEGOTIATING | Accept DEVICE_INFO/BEGIN and validate versions, board/hardware and capabilities | Disconnect returns to IDLE; no resume |
| PREPARING | Validate size and erase Secondary | Reboot leaves no pending image; restart from BEGIN |
| RECEIVING | Accept expected DATA/duplicate retry only | Reboot/disconnect aborts V1; restart from BEGIN |
| VERIFYING | Readback/digest and metadata checks | Reboot leaves candidate unmarked and ignored |
| READY_TO_COMMIT | Candidate complete, not pending | Reboot safe; V1 requires a new session to commit |
| COMMITTING | Request MCUboot test-pending state | Reset allowed only after success and response flush |
| COMPLETED | Report success and optionally reset | Reset expected |
| ABORTED | Candidate is not pending; cleanup diagnostics retained | Return to IDLE explicitly |
| ERROR | No further writes; expose bounded diagnostic | Reset only by policy; never mark pending |

Specific failure behavior:

- Secondary erase/write/readback failure: enter ERROR, do not mark pending.
- Frame CRC failure is silently discarded because command/sequence integrity is
  unknown; invalid sequence receives an error response. Host retry and device
  error counts are bounded.
- Image-size/hardware incompatibility: reject before erase when possible.
- Final digest mismatch or invalid metadata: abort candidate; MCUboot signature validation remains authoritative at Boot.
- Power loss during download: Primary remains selected because pending state was never written.
- Power loss during MCUboot swap: upstream scratch-swap status resumes/reverts according to MCUboot.
- First test boot without health confirmation: next reset reverts.

V1 resume after Application reset is explicitly unsupported. It may be added only with persistent, integrity-protected session metadata and fault-injection evidence.

## Application confirmation policy

The current immediate confirmation is a baseline test mechanism, not the product policy. Production confirmation must occur only after bounded essential checks, for example:

- clocks and required memory initialized;
- scheduler/main loop reached if used;
- critical configuration is readable;
- watchdog service is active;
- required product-specific self-test has a clear timeout.

Failure or timeout leaves the image unconfirmed and allows MCUboot revert.

## Host tool

Phase 5 uses one minimal Rust updater because Windows/Linux packaging and the
later Slint GUI now justify a compiled cross-platform host. The existing Python
libusb loopback remains a Phase 4 USB regression tool only. Protocol V1 and its
Golden Vectors remain the specification; Rust types are not the specification.

The initial CLI exposes only `info`, `install` and `wait`. CLI and GUI are sibling
frontends over one Rust library containing `FirmwareImage`, `ProtocolV1Client`
and `UpgradeWorkflow`. It owns framing, sequence/retry, compatibility, image
transfer and structured progress events. Fake E2E uses a
test-only stdio link to the existing C Manager/fake backends; real HIL replaces
only that link with blocking nusb Bulk transfers. There is no Tokio runtime,
plugin API, transport registry or transport-selection option.

`FirmwareImage` means one MCUboot signed-image file, not a plugin/package
container. Host-side CRC and metadata checks provide early rejection and
transfer integrity; MCUboot remains the signature trust root. nusb owns only
discovery/open/claim, Bulk I/O and USB error mapping. The workflow owns the
bounded wait/re-enumeration policy.

After CLI v1 -> v2 -> confirmation -> persistence HIL passes, one Slint window
calls the same core. Blocking USB work runs on one standard-library worker
thread so the UI event loop remains responsive. Windows/Linux packaging is the
final Phase 5 step, not a reason to add a service or framework.

## Version and compatibility model

| Identity | V1 rule |
| --- | --- |
| Protocol major | Must match exactly |
| Protocol minor | Host may use only capabilities advertised by the device |
| Host-tool version | Informational/support field; not used as a substitute for protocol compatibility |
| Application version | Reported by device and used in upgrade/downgrade policy |
| Bootloader version | Reported for diagnostics and required-capability checks |
| Hardware ID | BEGIN declaration must match for early rejection; signed image metadata must match before G5 COMMIT policy is accepted |
| Board ID/revision | BEGIN declaration uses exact V1 match; signed exact/compatibility-set policy remains a pre-G5 decision |
| Image size | Nonzero and no larger than logical Secondary capacity/trailer policy |
| Slot size | Device capability; host must reject an image that cannot fit |

V1 Protocol fails closed on incompatible major, declared hardware/board
mismatch, impossible slot size and malformed/overflowing metadata. Host
declarations are not image trust; a signed compatibility metadata policy must
fail closed before G5. Downgrade policy and MCUboot security counters require a
separate ADR before production release.

## Security boundaries

- MCUboot signature validation is the root of image trust. Transport success is not image trust.
- Unsigned/invalid-signature images may be received into Secondary but must never replace a valid Primary; Boot validation rejects them.
- Host inputs use logical offsets and bounded lengths; integer addition is checked before translation.
- Firmware-image commands can erase/write only Secondary. Product-config
  commands expose only the fixed write-once identity schema; their FlashDB
  backend alone owns Reserved. Boot, Primary, Scratch, arbitrary KV keys and
  physical addresses are not selectable by the Host.
- Frame lengths, firmware sizes and offsets have fixed compile-time maxima.
- Release private keys never enter the repository, firmware or host tool.
- Malformed traffic has bounded CPU/time/retry cost to limit denial of service.
- Host-declared hardware/board compatibility is checked before destructive
  erase as an early error filter, not as image trust. A signed compatibility
  metadata policy remains required before G5.
- Anti-rollback is not provided by signatures alone and remains an explicit product-security gap.

## CherryUSB boundary

CherryUSB is the verified USB device-stack backend, not the transport contract
or protocol. Phase 4 pinned the unchanged required subset of release `v1.6.1`
at commit `c9625ffa773ad10b8824d1b5361bca2ccc1f3d1e`, licensed Apache-2.0,
under `components/cherryusb/`. The HC32F460 DCD remains project-owned code
outside CherryUSB core. G4 HIL verified enumeration, Bulk transfer, stall
recovery, re-enumeration and a 30-minute run. See
`docs/adr/ADR-002-cherryusb-strategy.md` and `components/cherryusb/UPSTREAM.md`.

## Architecture self-review

| Question | Result |
| --- | --- |
| 1. What is actually complete? | Signed Boot/App baseline, Secondary Storage/Boot-Control, Protocol/Manager through COMMIT/RESET, and CherryUSB/HC32 Vendor Bulk G4 loopback |
| 2. What should happen next? | Execute the accepted Phase 5 Rust CLI/fake E2E plan, then blocking nusb HIL |
| 3. What should not happen now? | Slint before core HIL, plugins/transport registry, Tokio, UART/CAN, resume or Boot recovery |
| 4. Top three risks? | Parser bounds/desync, duplicate side effects, unsigned compatibility metadata assumptions |
| 5. Boot/App/Slot distinguished? | Yes |
| 6. Full manager in Application? | Yes |
| 7. Boot recovery separate? | Yes |
| 8. Protocol unaware of USB/CAN/UART? | Yes by contract |
| 9. Transport unaware of MCUboot slot? | Yes by contract |
| 10. Storage unaware of transport? | Yes by contract |
| 11. Portable core unaware of HC32? | Yes for current fw_update core by contract, HostTests and CI dependency rule |
| 12. CherryUSB only a backend? | Yes |
| 13. Second MCU portability proof? | Phase 9 requires unchanged portable core |
| 14. Each phase has PASS/FAIL? | Yes, in `ACCEPTANCE_GATES.md` |
| 15. CI vs HIL split clear? | Yes, in `TEST_STRATEGY.md` |

All answers are affirmative for the target architecture. Items not yet implemented remain requirements, not claims about current code.
