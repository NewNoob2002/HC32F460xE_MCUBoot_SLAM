# Firmware Update Implementation Plan

Status: Active roadmap

Baseline revision: `138d37ff3afe0a8655c98cade11bd7421c219918`

This plan evolves the verified HC32F460 MCUboot baseline into a portable firmware-update framework. Work advances one phase at a time. A phase is complete only after its hard gate in `ACCEPTANCE_GATES.md` passes and its evidence is retained outside ignored build directories.

## Planning decisions

- Boot remains limited to trust, image selection, swap, rollback and handover. A recovery profile is a later, independent decision.
- The full updater belongs to every Application image. Primary and Secondary are slots, not separate applications.
- Protocol, transport, storage and boot control are separate responsibilities.
- Phase 1 freezes semantics in documentation only. Empty C interfaces are deferred until Phase 2 gives Storage and Boot Control their first real implementation and fake.
- V1 uses static allocation, bounded buffers and cooperative polling. Async callback frameworks, registries and dynamic plugin loading are out of scope.
- Every host-supplied offset is a logical image offset. Only the storage backend may translate it to a physical Flash address.

## Phase 0 — Verified MCUboot Baseline

### Objective

Preserve the known-good signed boot, swap, rollback and confirmation baseline. Phase 0A additionally makes HIL evidence survive a fresh clone.

### Preconditions

- HC32F460 target, J-Link and a reproducible Arm toolchain are available.
- Primary, Secondary and Scratch layout is frozen.

### Scope

- HostTests, Debug and Release builds.
- ECDSA-P256 signing and image verification.
- Primary boot, test upgrade, revert and confirmation persistence.
- Immutable HIL evidence and checksum verification in CI.

### Out of Scope

- Firmware transfer, USB, UART, CAN and anti-rollback counters.

### Architecture Impact

Establishes the protected Boot/App/MCUboot/platform baseline.

### Files / Components

Existing `Boot/`, `App/`, `Platform/HC32F460/`, `components/mcuboot_port/`, build scripts, HostTests and `evidence/hil/`.

### API Impact

None beyond the existing MCUboot port and App confirmation wrapper.

### Implementation Steps

1. Build and test HostTests, Debug and Release.
2. Verify signing policy and image layout.
3. Execute bounded rollback/confirmation HIL.
4. Freeze raw logs, exact firmware and manifests outside `build/`.

### Automated Tests

`python3 Tests/HIL/verify_evidence.py`, strict HostTests, Debug/Release builds and `verify_app_image`.

### HIL Tests

Confirmed v1, unconfirmed v2 test boot, revert to v1, confirmed v2 and persistence after reset.

### Fault Injection

Unconfirmed test image reset.

### Acceptance Criteria

- CI passes all baseline checks.
- Exact programmed images and raw logs are available after a fresh clone.
- Physical rollback and confirmation persistence are evidenced.

### Evidence

`evidence/hil/2026-08-25-339f32c/` and CI run `32922559686`.

### Exit Gate

G0 Baseline Gate.

### Rollback Point

Git revision `138d37f`.

### Risks

- Historical HIL does not replace HIL for future firmware revisions.

## Phase 1 — Architecture Audit and Contract Freeze

### Objective

Record the real repository state, freeze module ownership and define the smallest V1 interface semantics before implementation.

### Preconditions

- G0 has passed.
- Baseline source and evidence are clean and available.

### Scope

- Current architecture, Flash layout, Boot flow, capability and test audit.
- Target architecture and dependency rules.
- Transport, Protocol, Storage and Boot-Control semantic contracts.
- State machine, version compatibility, security review and host-tool direction.
- Phase gates, test strategy, status tracking and initial ADRs.

### Out of Scope

- New production C APIs or empty interface libraries.
- Storage backend implementation.
- USB, CherryUSB vendoring, HC32 DCD, UART, CAN and protocol parser code.
- Flash layout or MCUboot upstream changes.

### Architecture Impact

Freezes allowed dependency directions and separates storage operations from MCUboot boot-state control.

### Files / Components

- `docs/roadmap/CURRENT_STATE.md`
- `docs/roadmap/ARCHITECTURE.md`
- `docs/roadmap/IMPLEMENTATION_PLAN.md`
- `docs/roadmap/ACCEPTANCE_GATES.md`
- `docs/roadmap/TEST_STRATEGY.md`
- `docs/roadmap/STATUS.md`
- `docs/adr/ADR-001-firmware-update-layering.md`
- `docs/adr/ADR-002-cherryusb-strategy.md`

### API Impact

No source API is added. The documents define V1 behavior that Phase 2 must turn into the smallest concrete C contracts.

### Implementation Steps

1. Audit actual CMake targets, includes, MCUboot configuration, linkers and tests.
2. Extract addresses and boot flow from source, not historical prose.
3. Classify implemented, partial and missing capability.
4. Define module ownership, dependency rules and V1/Future interface semantics.
5. Define phases, hard gates, evidence requirements and project DoD.
6. Review the result against the fifteen architecture questions in `ARCHITECTURE.md`.
7. Re-run the protected baseline checks and retain the exact commands/results in `STATUS.md`.

### Automated Tests

- Retained evidence verification.
- Strict HostTests.
- Debug and Release firmware/image verification.
- Documentation link/path and forbidden-dependency checks where applicable.

### HIL Tests

None. Phase 1 must not change target behavior.

### Fault Injection

None beyond existing host boundary tests. Fault cases are planned, not executed.

### Acceptance Criteria

- Every current-state claim cites a repository source path.
- Boot/App/slot ownership is unambiguous.
- Protocol has no transport or MCU dependency in the target design.
- Storage and Boot Control are separate contracts.
- CherryUSB is recorded only as a proposed USB backend with an independent HC32 DCD phase.
- Every later phase has PASS/FAIL criteria, evidence and rollback points.
- No production source or hardware state changes.

### Evidence

Roadmap/ADR files, clean diff review, baseline command output and CI result.

### Exit Gate

G1 Architecture Gate.

### Rollback Point

Phase 0A revision `138d37f`.

### Risks

- Over-design before real backends exist. Mitigation: documentation-only contracts and explicit V1 exclusions.
- Documentation drift. Mitigation: source paths, status ownership and review at every gate.

## Phase 2 — MCUboot Secondary Storage and Boot Control

### Objective

Implement the first portable contracts with an HC32/MCUboot Secondary Slot backend and a separate boot-control adapter.

### Preconditions

G1 has passed and API names/semantics are approved.

### Scope

- Logical-offset Secondary image capacity excluding the trailer sector, full-slot prepare/erase, aligned write and readback.
- Bounds/overflow checks before physical address translation.
- Separate request-test-upgrade and confirm-running-image adapter.
- Fake storage/boot-control implementations for host tests.

### Out of Scope

Protocol, transport, USB, reboot policy, resume and permanent upgrades.

### Architecture Impact

Creates the first portable-to-platform boundary.

### Files / Components

`components/fw_update/`, an MCUboot storage/backend directory, App integration points and HostTests. Exact names are chosen during implementation to match existing CMake conventions.

### API Impact

First public Storage and Boot-Control C contracts. No Transport or Protocol API yet.

### Implementation Steps

1. Write host contract/bounds tests.
2. Add minimal public types and error codes.
3. Implement fake backends.
4. Implement Secondary Slot adapter using area-relative offsets.
5. Implement boot-control adapter using MCUboot public APIs.
6. Isolate HC32 headers from portable targets.
7. Build/test/review each step independently.

### Automated Tests

Offset overflow, image/trailer boundary, alignment, area-open/driver failure, readback and pending-call ordering. Zero/oversize session metadata is tested in Phase 3 where the manager first owns image size.

### HIL Tests

Erase/write/read Secondary without marking pending; verify Primary and Reserved remain unchanged. A separate test requests a known valid test image.

### Fault Injection

Erase failure, Nth write failure, readback mismatch and boot-control failure.

### Acceptance Criteria

- No input can address outside Secondary.
- Host tests cover every boundary and failure return.
- Storage cannot confirm or request boot; Boot Control cannot write image bytes.
- Portable code compiles without HC32 headers.

### Evidence

CTest output, HIL logs, before/after region hashes, map/size and source revision.

### Exit Gate

G2 Storage Gate.

### Rollback Point

G1-passed architecture revision.

### Risks

- Flash wear or unintended range access. Mitigation: fake-first bounds tests and HIL region hashes.

## Phase 3 — Protocol Core with Test Byte Driver

Detailed execution plan: `docs/roadmap/PHASE3_PROTOCOL_CORE_PLAN.md`.
Wire-format draft: `docs/protocol/PROTOCOL_V1.md`.
Boundary decision: `docs/adr/ADR-003-protocol-v1-boundary.md` (Accepted).

### Objective

Implement a bounded Protocol V1 parser and update session without real I/O hardware.

### Preconditions

G2 has passed.

### Scope

- HELLO, DEVICE_INFO, BEGIN, DATA, END, COMMIT, ABORT and common status responses.
- Fixed 16-byte header, 512-byte maximum payload, sequence and CRC-32.
- Incremental parsing across arbitrary input chunks.
- Strict contiguous DATA, alignment staging/tail padding, duplicate response replay and timeout/error budget.

### Out of Scope

Resume, sliding windows, encryption, authentication beyond signed images and transport-specific framing.

### Architecture Impact

Adds a pure Protocol codec/parser and a portable Manager above Storage and Boot Control. Reset is emitted as an action after response drain; no platform reset call enters portable code.

### Files / Components

Portable protocol/update sources, Protocol V1 specification, test-local byte driver/fakes, malformed corpus and HostTests.

### API Impact

Adds Protocol and Manager contracts with borrowed RX feed, Manager-owned TX drain, explicit monotonic time, disconnect/TX-idle notifications and lifecycle actions. It does not freeze a production Transport backend vtable before the first real backend.

### Implementation Steps

1. Review/accept ADR-003 and freeze wire-format/golden vectors; run local RED without pushing a failing revision.
2. Implement CRC, bounded codec and incremental parser.
3. Implement Manager receive/readback lifecycle with fake Storage/Boot Control.
4. Add duplicate/sequence/timeout/disconnect and COMMIT response-drain ordering.
5. Add the fixed-seed malformed corpus, dependency enforcement and immutable host evidence.

### Automated Tests

Every-byte split/coalesced frames, fixed golden vectors, invalid lengths/CRC/version/flags, state/sequence/duplicate behavior, logical overflow, final tail padding, Storage/Boot-Control faults, response-drain/reset ordering and at least 10,000 deterministic malformed cases.

### HIL Tests

None.

### Fault Injection

Byte split/coalesce/short TX/disconnect, Nth Storage failure, readback corruption, Boot-Control failure, timeout boundaries and duplicate delivery.

### Acceptance Criteria

- Parser never reads/writes outside fixed buffers; maximum payload/frame are 512/532 bytes.
- Identical protocol behavior passes at every representative split point and under coalescing/backpressure.
- Arbitrary DATA sizes are staged into aligned writes; final tails preserve exact logical size/CRC.
- Duplicate COMMIT/DATA causes no repeated side effect; RESET is emitted only after successful response drain and TX idle.
- Manager static state is at most 2048 bytes, uses no dynamic allocation and includes no transport-, MCUboot- or HC32-specific header.

### Evidence

Accepted Protocol/ADR, golden vectors, sanitizer CTest output, corpus seed/count/result, Manager size, dependency report and tracked `evidence/host/` bundle.

### Exit Gate

G3 Protocol Gate.

### Rollback Point

G2-passed storage revision.

### Risks

- Parser complexity. Mitigation: fixed header/maxima, stop-and-wait and no resume/windowing/protocol fragmentation in V1.
- Fake-only Transport abstraction. Mitigation: freeze only the byte ingress/egress contract until the first real backend.
- Host metadata mistaken for image trust. Mitigation: CRC is integrity only; signed compatibility metadata requires a separate decision before G5.

## Phase 4 — CherryUSB and HC32 USB DCD Loopback

### Objective

Prove USB Vendor Bulk independently of firmware-update logic.

### Preconditions

G3 has passed; selected CherryUSB revision/license is recorded; USB pins/clock/IRQ are confirmed.

### Scope

- Vendor CherryUSB at an immutable tag/commit.
- HC32F460 DCD outside CherryUSB core.
- Device descriptors, Bulk IN/OUT and bounded loopback/echo host test.

### Out of Scope

Flash writes, protocol commands, MCUboot APIs and Boot recovery.

### Architecture Impact

Adds `USB transport backend -> CherryUSB -> HC32 DCD`; core updater remains unchanged.

### Files / Components

Vendored dependency metadata, USB backend, HC32 DCD/platform code, descriptor specification and host loopback tool.

### API Impact

Implements the existing Transport contract; no Protocol API change.

### Implementation Steps

1. Freeze upstream revision/license and import policy.
2. Port DCD init, endpoint, transfer and ISR paths.
3. Enumerate with fixed descriptors.
4. Add Vendor Bulk echo.
5. Stress unplug/replug and repeated transfer.

### Automated Tests

Descriptor parsing, host loopback logic and build/dependency checks.

### HIL Tests

Enumeration on Linux, descriptor dump, 10,000 deterministic echo transfers at multiple lengths, unplug/replug and 30-minute run.

### Fault Injection

Short packets, zero-length packet, endpoint stall recovery and disconnect during transfer.

### Acceptance Criteria

- VID/PID/interface/endpoints match the specification.
- All echo payloads are byte-identical with no unhandled stall.
- Replug re-enumerates without target power cycle.
- USB code has no storage/MCUboot calls.

### Evidence

Upstream revision/license, descriptor dump, host logs, HIL manifest and artifact hashes.

### Exit Gate

G4 USB Stack Gate.

### Rollback Point

G3-passed protocol revision.

### Risks

- No known upstream HC32F460 DCD. Mitigation: independent DCD phase and no CherryUSB core modifications.

## Phase 5 — USB Firmware Upgrade End to End

Detailed execution plan: `docs/roadmap/PHASE5_USB_UPDATER_PLAN.md`.

### Objective

Deliver the first complete host-to-confirmed-application upgrade path.

### Preconditions

G4 has passed and a release-qualified HIL fixture is available.

### Scope

Minimal Rust `info`/`install`/`wait` CLI, one shared Rust protocol/client core,
blocking nusb USB link, Application USB-to-Manager glue, Secondary storage,
test-pending request, reboot, bounded post-boot health confirmation and
persistence verification. After that core path passes, add one Slint window and
signed Windows `.msi` and Linux package; the Linux package carries the
udev rule.

### Out of Scope

Python updater work, Tokio, plugins, a transport registry, Boot recovery,
download resume, UART and CAN. The Python USB loopback tool remains a Phase 4
regression tool only.

### Architecture Impact

Integrates existing layers without bypasses. Fake E2E and real USB HIL use the
same Rust client core; the Slint GUI later calls that same core.

### Files / Components

Application integration, `Tools/updater/`, existing protocol spec/vectors, a
test-only C Manager fake-device adapter, HIL scripts and delayed GUI/package
assets.

### API Impact

Only corrections required by integration evidence; breaking changes require ADR update.

### Implementation Steps

1. Freeze release preconditions: signed compatibility metadata source,
   production VID/PID, WinUSB binding and external signing credentials.
2. Implement the Rust client core and `info`/`install`/`wait`; prove it against
   the existing C Manager with fake Storage/Boot Control.
3. Add the minimum Application USB glue and blocking nusb link.
4. Transfer a valid signed image, finalize, request test upgrade and reboot.
5. Verify v2 health confirmation and persistence after another reset.
6. Only then add the Slint single-window GUI over the same core.
7. Build/verify the signed Windows `.msi` and Linux package with udev rule.
8. Retain immutable fake, HIL, GUI and package evidence.

### Automated Tests

Rust format/clippy/tests, shared Golden Vectors, fake E2E session, package
content/signature checks and existing firmware/dependency regressions.

### HIL Tests

CLI v1 to v2 over USB, swap, new version report, bounded confirmation and
persistence after another reset; then one GUI install using the same core and
clean Windows/Linux package install/access/uninstall checks.

### Fault Injection

Wrong hardware ID, oversize image, bad CRC, invalid signature, USB disconnect,
bounded timeout and duplicate response. Download resume is not added.

### Acceptance Criteria

- A clean host installs a signed compatible image without debugger Flash writes.
- Invalid/oversize images never become bootable.
- After confirmation, v2 persists and reports expected versions/hardware identity.
- Fake E2E, CLI USB and GUI paths use one Rust protocol/client core.
- Windows ships a verified signed installer without manual driver binding;
  Linux ships an installable package with the correct udev rule.
- The Phase 4 Python loopback regression remains unchanged and green.

### Evidence

Fake/host transcript, USB trace/descriptor, firmware hashes, HIL logs,
confirmation/persistence slot state, package hashes/signature verification and
clean-VM install results.

### Exit Gate

G5 USB Upgrade E2E Gate.

### Rollback Point

G4 USB loopback revision.

### Risks

- Cross-layer shortcuts. Mitigation: dependency checks and review of every call path.
- Current `fffe:ffff` test identity and missing release-signing secret cannot
  qualify public packages. Mitigation: make both explicit Phase 5A/5F gates.

## Phase 6 — Failure and Recovery Qualification

### Objective

Prove deterministic safe behavior under transfer, Flash, reset and swap failures.

### Preconditions

G5 has passed.

### Scope

Interrupted transfer, disconnect, malformed input, invalid image/signature, write failure and reset at each critical lifecycle point.

### Out of Scope

New transport and automatic Boot recovery implementation.

### Architecture Impact

May refine state/error handling without changing layer ownership.

### Files / Components

Fault-injection fakes, HIL power/reset controller scripts, evidence and recovery ADR decision.

### API Impact

Only bounded error/state additions supported by failing tests.

### Implementation Steps

1. Build deterministic failure matrix.
2. Automate host/fake failures.
3. Execute bounded reset/power-interruption HIL.
4. Classify outcomes and close defects.
5. Decide whether a minimal Boot recovery profile is justified.

### Automated Tests

Every state/error branch, retry limits and reboot permissions.

### HIL Tests

Reset/power loss during Secondary erase, first/middle/final block, verify, mark pending, swap, first test boot and confirmation.

### Fault Injection

The phase is the fault-injection qualification.

### Acceptance Criteria

- Primary remains bootable or MCUboot safely rejects the candidate.
- Partial images are never marked pending.
- An unconfirmed test image reverts.
- Every failure has a bounded, diagnosable host/device result.

### Evidence

Failure matrix, raw power/reset logs, target identity, exact artifacts and recovery outcome.

### Exit Gate

G6 Reliability Gate.

### Rollback Point

G5 E2E revision.

### Risks

- Unsafe power interruption. Mitigation: workflow safety preflight, bounded Flash range and recoverable baseline image.

## Phase 7 — UART Transport Portability Proof

### Objective

Prove transport independence by adding UART without changing Protocol or update-manager behavior.

### Preconditions

G6 has passed.

### Scope

UART byte-stream backend, a later Rust serial link and the existing Protocol V1.

### Out of Scope

YMODEM/XMODEM, RS485 and protocol redesign.

### Architecture Impact

Adds one Transport implementation only.

### Files / Components

UART platform/backend, Rust serial link and HIL tests.

### API Impact

None expected. Any Protocol/manager change fails the portability goal unless correcting a proven abstraction defect.

### Implementation Steps

1. Implement bounded RX/TX buffering and ISR handoff.
2. Connect existing transport contract.
3. Run existing protocol suite unchanged.
4. Execute UART E2E upgrade.

### Automated Tests

Byte-by-byte and arbitrarily chunked transport tests, overflow and timeout.

### HIL Tests

UART E2E upgrade, disconnect/reconnect and configured baud stress.

### Fault Injection

Dropped/corrupted bytes and RX overflow.

### Acceptance Criteria

- Protocol/update-manager source is unchanged or changes are justified as abstraction fixes.
- USB and UART use the same protocol vectors and host command semantics.

### Evidence

Diff audit, serial logs, HIL result and artifact hashes.

### Exit Gate

G7 UART Portability Gate.

### Rollback Point

G6 reliability revision.

### Risks

- ISR coupling. Mitigation: ISR only moves bytes/events; session logic runs in application context.

## Phase 8 — CAN / CAN FD Transport Portability Proof

### Objective

Add Classic CAN and CAN FD while preserving the same update manager and Protocol V1 semantics.

### Preconditions

G7 has passed.

### Scope

CAN addressing, 8/64-byte payload adaptation, bounded segmentation, flow control, sequence and timeout behavior.

### Out of Scope

UDS compatibility unless separately approved.

### Architecture Impact

Adds CAN transport/backends; fragmentation does not enter the update manager.

### Files / Components

CAN platform/backend, host SocketCAN adapter, bus tests and HIL evidence.

### API Impact

None expected; capability values may be extended without breaking V1.

### Implementation Steps

1. Freeze arbitration IDs and bus settings.
2. Implement Classic CAN adaptation.
3. Add CAN FD capability.
4. Measure bus load and timeout behavior.
5. Execute E2E upgrades.

### Automated Tests

Segmentation/reassembly, sequence, timeout, duplicate and bus-load calculations.

### HIL Tests

Classic CAN and CAN FD upgrades with monitored error counters.

### Fault Injection

Frame loss, reordering, duplicate, bus-off and recovery.

### Acceptance Criteria

- No CAN frame reaches the update manager directly.
- Classic CAN and CAN FD pass the same protocol/update test vectors.
- Bus-off produces bounded failure and recovery behavior.

### Evidence

CAN logs, bitrate/ID manifest, error counters, bus load and E2E artifacts.

### Exit Gate

G8 CAN Portability Gate.

### Rollback Point

G7 UART revision.

### Risks

- Low MTU and shared-bus load. Mitigation: fixed windows/budgets justified by measurement.

## Phase 9 — Second MCU Portability Proof

### Objective

Port to a second MCU to prove the portable architecture is real.

### Preconditions

G8 has passed and a supported second board/toolchain is selected.

### Scope

Add one MCU platform, Flash/boot-control backend and one proven transport. STM32 is the default candidate because tooling and CherryUSB DCD support reduce unrelated port risk.

### Out of Scope

New protocol features or product functionality.

### Architecture Impact

Adds `Platform/<NEW_MCU>` and backend wiring only.

### Files / Components

Second-MCU platform, linker/memory map, build preset and HIL evidence.

### API Impact

Portable APIs must remain source-compatible. Changes require an abstraction-defect ADR.

### Implementation Steps

1. Freeze board, memory map and probe.
2. Add build/platform/storage/boot-control backend.
3. Run portable HostTests unchanged.
4. Execute signed upgrade E2E and rollback HIL.
5. Audit portable-core diff.

### Automated Tests

Both MCU firmware builds, shared host suites and dependency checks.

### HIL Tests

Second-MCU boot, upgrade, swap/rollback and confirmation.

### Fault Injection

At minimum interrupted transfer and unconfirmed test boot.

### Acceptance Criteria

- Portable core/protocol source requires no MCU-specific conditional compilation.
- Only platform/backend/build/layout files are added or modified, except proven defect fixes.
- Both MCU baselines pass their gates.

### Evidence

Cross-MCU diff audit, builds, HIL bundles and dependency report.

### Exit Gate

G9 Cross-MCU Portability Gate.

### Rollback Point

G8 CAN revision.

### Risks

- Choosing a board that introduces unrelated USB/Flash complexity. Mitigation: select a well-supported reference board and reuse a proven transport.

## Project Definition of Done

A phase is `PASSED` only when all applicable items are complete:

- Code and public English Doxygen contracts.
- Host unit and component integration tests.
- Debug/Release firmware builds and signing/layout checks.
- Static/compiler/dependency checks.
- Required HIL and fault injection.
- Updated architecture, protocol and operational documentation.
- Immutable evidence with revision, exact commands, raw logs and hashes.
- Architecture review and the phase hard gate.

`Code complete`, `works once` and `mostly done` are not completion states.
