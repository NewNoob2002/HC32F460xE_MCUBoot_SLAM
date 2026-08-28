# Firmware Update Test Strategy

The strategy uses the lowest test level that can falsify a requirement, then reserves HIL for real Flash, reset, USB/CAN/UART, timing and MCUboot behavior. A fake proves the portable contract, not the hardware behind it.

## Test pyramid

### Level 1 — Pure host unit tests

Run natively with strict warnings, ASan and UBSan. Use only static/fixed-size data.

Planned coverage:

- CRC and protocol header encode/decode;
- incremental parser with every possible split point for representative frames;
- command/state transition table;
- sequence, duplicate and retry limits;
- offset/length overflow and Secondary capacity boundaries;
- arbitrary byte split/coalesce behavior; Protocol V1 has no protocol-layer fragmentation;
- compatibility decisions for protocol/hardware/board/image/slot versions;
- error mapping and reboot permissions.

Fakes:

- Transport-facing test driver: scripted byte chunks, partial TX consumption, TX-idle and disconnect;
- Storage: bounded byte array, configurable erase/write/read failure index and corruption;
- Boot Control: records pending/confirm calls and return values;
- Clock: explicit deterministic monotonic `now_ms` passed to portable code;
- Reset: an emitted lifecycle action recorded only after response drain and TX idle.

Every fake must enforce the same alignment, ownership and ordering contract as the real boundary.

### Level 2 — Component integration tests

Required combinations:

- Protocol codec/parser with a test-local byte-chunk driver;
- Manager with the real Protocol core plus fake Storage/Boot Control and explicit time/lifecycle events;
- Storage backend + mocked MCUboot Flash area/BSP Flash;
- Rust updater core + test-only stdio adapter around the real C Manager/fake
  Storage and Boot Control;
- CAN/UART/USB adapter + deterministic host-side emulator where representative.

These tests verify call ordering and error propagation across boundaries. They do not claim target timing, ISR, electrical or Flash behavior.

### Level 3 — Firmware build and artifact tests

The existing baseline remains mandatory:

- Debug and Release configuration/build;
- strict compiler warnings;
- signing-key policy;
- `imgtool verify`;
- Boot/App partition/link placement;
- padded image size and map/size regression;
- generated public key/signing key consistency;
- architecture dependency checks.

Each new portable target must also compile in HostTests without `hc32_project_options` or HC32 include paths.

### Level 4 — HIL

HC32 HIL proves:

- actual Boot validation/handover;
- internal Flash erase/program/read and range isolation;
- USB enumeration/endpoints/transfers;
- UART/CAN ISR/buffering and timing;
- complete update, swap, rollback and confirmation;
- reset and unplug/replug behavior.

Phase 5 orders these checks deliberately: the same Rust client core must first
pass fake E2E, then blocking nusb USB HIL, then confirmation plus another reset
for persistence. Only afterward may the Slint GUI and clean-host portable tests use
that core. The Python loopback remains a separate Phase 4 regression oracle.

Every state-changing HIL run requires a safety preflight containing target/probe identity, permitted Flash ranges, firmware hashes, voltage/current bounds if controlled, timeout, cleanup and recovery path. Passive logs start before stimulus. Failures are retained before retry/reset.

### Level 5 — Fault injection

Fault injection is deterministic and reports attempt counts. A retry never converts an unexplained failure into a pass.

Host/fake injection:

- malformed/truncated/oversized frames;
- CRC/sequence/duplicate faults;
- disconnect and timeout at each state;
- Nth erase/write/read failure;
- corrupted readback/digest;
- boot-control and reset failure;
- incompatible hardware/board/protocol/image size.

HIL injection focuses on power/reset and real buses.

## Power-interruption matrix

| Interruption point | Required post-reset behavior | Evidence |
| --- | --- | --- |
| During Secondary erase | Existing confirmed Primary boots; candidate is not pending; new transfer starts with erase | reset/boot log and slot/trailer sample |
| First data block | Primary boots; partial Secondary is ignored | host failure plus slot/trailer state |
| Middle data block | Same as first block; no resume in V1 | transferred offset and post-reset state |
| Final data block before verify | Candidate remains uncommitted | host/target logs and pending state |
| Verification/readback | Candidate remains uncommitted; retry requires new BEGIN | digest/error result and pending state |
| Before mark pending | Complete bytes may remain but Primary boots normally | finalized metadata and trailer state |
| During mark pending | Result must be one valid MCUboot trailer state; Boot either uses valid test candidate or ignores it | raw trailer and boot result |
| During MCUboot scratch swap | Upstream status resumes swap/revert without leaving no bootable Primary | probe/power timing, boot logs and final slots |
| First test boot | If reset before confirmation, next boot reverts | version sequence and trailer flags |
| During confirmation | Either confirmation is valid and persists, or image remains test and reverts; no ambiguous unsafe state | confirmation return, trailer and next boot |

The matrix must be repeated at deterministic offsets/timing windows sufficient to cover each operation boundary. Exact repetition counts are fixed in the Phase 6 HIL manifest after timing measurement.

## Requirement-to-test mapping

Every test result references at least one of:

- architecture invariant;
- protocol requirement/vector;
- security boundary;
- phase acceptance criterion;
- reproduced defect.

A test without an oracle or linked requirement is not release evidence. Coverage measures execution only; it does not replace assertions.

## CI evolution

### Current CI retained

- HIL evidence checksum verification;
- `-Wall -Wextra -Wconversion -Wshadow -Werror`;
- ASan/UBSan HostTests;
- Debug firmware and signed-image verification;
- Release missing-key/wrong-curve rejection;
- Release firmware and signed-image verification.

### Added by later phases

- Storage/Boot-Control unit and fake tests;
- Protocol parser/state/CRC/golden-vector tests;
- deterministic malformed corpus;
- test-local byte driver plus fake Storage/Boot Control and explicit time/lifecycle integration;
- size-regression budgets;
- portable dependency checker rejecting `hc32f460.h`, HC32 LL/HAL and CherryUSB includes;
- build isolation proving portable targets need no HC32 definitions/include paths.
- Rust format/clippy/locked tests and shared Protocol Golden Vectors.
- Fake E2E through the production C Manager and fake backends.
- Windows EXE signature/portable-ZIP checks and Linux direct-run/udev-rule checks.

Normal CI does not claim HIL. Hardware CI may be added only with exclusive fixture locking, bounded timeouts and immutable evidence upload.

## Evidence policy

`build/` is disposable and ignored. Gate evidence must live under a Git-tracked immutable directory such as:

```text
evidence/<level>/<YYYY-MM-DD>-<source-revision>/
```

Required metadata:

- source revision and dirty state;
- exact command and tool versions;
- build preset/compiler/linker script;
- target/board/probe/transport identity;
- setup, stimulus, timeout, repetitions and cleanup;
- raw output, pass/fail/skip counts and classified failures;
- firmware/input/output hashes;
- an index and `SHA256SUMS`.

Sensitive full-device backups and production private keys are excluded. Their safe metadata/hash may be recorded when useful. A fresh clone must contain all evidence required to justify a completed gate.

## Failure classification

- Product failure: firmware violates a requirement.
- Test failure: oracle, fixture or test code is wrong.
- Infrastructure failure: tool/runner/permission/sanitizer environment prevents execution.
- Flaky failure: nondeterministic result not yet classified; the gate fails until resolved.
- Hardware unavailable: HIL is skipped and the gate remains incomplete, not passed.

## Phase Definition of Done

A phase needs all applicable code, unit/integration tests, builds, compiler/static/dependency checks, documents, immutable evidence, architecture review and hard gate. HIL-only claims cannot be replaced by host fakes, and a single successful bench run without retained evidence does not pass.
