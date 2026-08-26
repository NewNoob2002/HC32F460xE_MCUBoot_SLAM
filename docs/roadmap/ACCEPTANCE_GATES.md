# Acceptance Gates

These are hard gates. A later phase cannot be marked `PASSED` while its preceding gate is failed, incomplete or lacks required evidence. Commands for future components are normative deliverables: the named test/script must exist before that gate can pass.

## G0 — Baseline Gate

### Preconditions

Arm toolchain, Ninja, CMake, Python/imgtool and an explicit non-production Release key are available.

### Test commands

```sh
python3 Tests/HIL/verify_evidence.py
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests --output-on-failure
cmake --preset Debug --fresh -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Debug --clean-first --parallel
cmake --build build/Debug --target verify_app_image
cmake --preset Release --fresh -DMCUBOOT_SIGNING_KEY=<TEST_EC_P256_KEY> -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Release --clean-first --parallel
cmake --build build/Release --target verify_app_image
```

### Automated tests

Evidence checksum, four strict HostTests, Debug/Release build, signing, padded image size and signing-key policy.

### Manual / HIL tests

Confirmed v1 boot, v2 test swap, unconfirmed revert, confirmed v2 and persistence. Historical evidence is acceptable only for the exact tested revision/artifacts.

### Expected results

All commands pass; Boot fits 64 KiB; images verify and padded slot files are 204,800 bytes.

### Required artifacts

CTest/build/imgtool output, sizes/maps, firmware hashes, HIL logs/manifests and target identity.

### PASS criteria

All automated checks and required HIL pass with immutable evidence.

### FAIL criteria

Any failed/omitted check, missing artifact, invalid signature/layout or unproven rollback/confirmation behavior.

## G1 — Architecture Gate

### Preconditions

G0 passed and the baseline worktree was clean before Phase 1.

### Test commands

```sh
git diff --check
python3 Tests/HIL/verify_evidence.py
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests --output-on-failure
cmake --preset Debug --fresh -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Debug --clean-first --parallel
cmake --build build/Debug --target verify_app_image
cmake --preset Release --fresh -DMCUBOOT_SIGNING_KEY=<TEST_EC_P256_KEY> -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Release --clean-first --parallel
cmake --build build/Release --target verify_app_image
```

Review commands must also prove every required roadmap/ADR file exists and that milestone files are not silently redefined.

### Automated tests

Protected evidence, HostTests, Debug/Release/signing checks plus documentation path/link review.

### Manual / HIL tests

None; hardware state must not change.

### Expected results

Current-state claims match code; all dependency questions in `ARCHITECTURE.md` answer yes for the target design; Phase 1 changes only documentation.

### Required artifacts

Six roadmap documents, two ADRs, review checklist, exact commands/results and source revision.

### PASS criteria

- Boot/App/slot ownership and current gaps are explicit.
- Storage and Boot Control are separate.
- Protocol/Transport/Storage/MCU boundaries are enforceable.
- All later phases contain objective, scope exclusions, tests, evidence, PASS/FAIL and rollback.
- Baseline HostTests remain green.

### FAIL criteria

Any architecture ambiguity, missing hard gate, speculative production interface/code, stale fact presented as current or baseline regression.

## G2 — Storage Gate

### Preconditions

G1 passed; Secondary Slot geometry and V1 Storage/Boot-Control semantics are approved.

### Test commands

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests -R 'fw_storage|fw_boot_control|flash_map' --output-on-failure
python3 Tests/Architecture/check_portable_dependencies.py
cmake --preset Debug --fresh
cmake --build build/Debug --parallel
```

### Automated tests

Secondary-only bounds/overflow/alignment, erase/write/read/finalize/abort, fake driver failures, pending-call ordering and forbidden includes.

### Manual / HIL tests

Bounded Secondary erase/write/readback; before/after hashes prove Boot, Primary, Scratch and Reserved were not modified. Request test-pending only with a known valid signed image.

### Expected results

Logical offsets cannot escape Secondary; failures never mark pending.

### Required artifacts

CTest output, dependency report, region hashes, HIL raw log, firmware/map sizes and target identity.

### PASS criteria

All tests pass, no HC32 header enters portable code, and HIL proves range isolation.

### FAIL criteria

Any out-of-range access, hidden physical address, mixed storage/boot-control responsibility, unclassified error or missing HIL isolation evidence.

## G3 — Protocol Gate

### Preconditions

G2 passed; protocol header fields, maxima and command/state table are frozen.

### Test commands

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests -R 'fw_protocol|fw_update_manager|fw_fake' --output-on-failure
python3 Tests/Protocol/run_malformed_corpus.py
python3 Tests/Architecture/check_portable_dependencies.py
```

### Automated tests

Golden vectors, split/coalesced frames, CRC, lengths, sequence, duplicates, timeout, invalid state and fake transport/storage failures under ASan/UBSan.

### Manual / HIL tests

None.

### Expected results

All arbitrary chunk boundaries yield identical protocol behavior; malformed input has bounded failure.

### Required artifacts

Protocol specification/version, golden vectors, sanitizer logs, malformed corpus result and dependency report.

### PASS criteria

No memory error, unbounded wait/allocation or concrete transport/MCU include; all state and error paths have deterministic assertions.

### FAIL criteria

Parser crash/UB, one-read-one-frame assumption, transport-specific command semantics, uncontrolled retry or missing golden vectors.

## G4 — USB Stack Gate

### Preconditions

G3 passed; CherryUSB tag/commit/license and HC32 DCD boundary are reviewed; safe HIL preflight exists.

### Test commands

```sh
cmake --preset Debug --fresh
cmake --build build/Debug --target usb_vendor_bulk_loopback --parallel
python3 Tools/host/usb_loopback.py descriptors
python3 Tools/host/usb_loopback.py run --iterations 10000 --lengths 0,1,63,64,65,512,1024
python3 Tests/Architecture/check_portable_dependencies.py
```

### Automated tests

USB descriptor parser, host loopback logic, dependency rule and firmware build.

### Manual / HIL tests

Linux enumeration, descriptor capture, 10,000 byte-identical transfers, 30-minute run and at least 10 unplug/replug cycles.

### Expected results

Specified VID/PID/interface and Bulk endpoints enumerate; no unhandled stall, crash or required power cycle.

### Required artifacts

Pinned upstream metadata/license, descriptor dump, host/raw target logs, test counts, HIL manifest and firmware hash.

### PASS criteria

All transfer/replug criteria pass and USB code contains no Protocol/Storage/MCUboot call.

### FAIL criteria

Enumeration/descriptor mismatch, data mismatch, endpoint hang/stall, core CherryUSB patch without approved exception or missing raw logs.

## G5 — USB Upgrade E2E Gate

### Preconditions

G4 passed; compatible signed v1/v2 images and safe rollback route exist.

### Test commands

```sh
python3 Tools/host/fw_update.py info
python3 Tools/host/fw_update.py install <SIGNED_V2_IMAGE> --test --reboot
python3 Tools/host/fw_update.py wait --version 2.0.0 --confirmed
python3 Tests/HIL/verify_evidence.py
```

### Automated tests

Host CLI unit tests, fake E2E session, golden protocol traces and compatibility rejections.

### Manual / HIL tests

Discover v1, transfer v2 over USB, mark test, swap, boot v2, health-confirm and verify persistence.

### Expected results

No debugger writes Secondary; final Primary is confirmed v2 and device reports matching versions/identity.

### Required artifacts

Host transcript, USB/target logs, v1/v2 hashes, slot state, version/identity output and immutable evidence index.

### PASS criteria

Complete E2E path passes repeatedly with exact evidence and no layering bypass.

### FAIL criteria

Manual Flash injection, wrong final version/state, missing confirmation, protocol/backend bypass or incomplete evidence.

## G6 — Reliability Gate

### Preconditions

G5 passed; automated reset/power fixture has safe voltage/current/range preflight.

### Test commands

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests -R 'fw_fault|fw_protocol|fw_storage' --output-on-failure
python3 Tests/HIL/run_power_interruption_matrix.py --manifest Tests/HIL/power_matrix.yaml
python3 Tests/HIL/verify_evidence.py
```

### Automated tests

All session failure branches, retry limits and simulated reset points.

### Manual / HIL tests

Power/reset at Secondary erase, first/middle/final write, verify, mark pending, swap, first test boot and confirmation.

### Expected results

Primary remains bootable or MCUboot safely rejects/reverts; no partial image becomes pending.

### Required artifacts

Failure matrix, each attempt/result, raw power/target/host logs, artifact hashes and cleanup state.

### PASS criteria

Every required injection has a deterministic safe result and no unresolved product failure.

### FAIL criteria

Brick/unrecoverable state, silent retry-to-pass, missing failure evidence, unsafe fixture bounds or partial image selected.

## G7 — UART Portability Gate

### Preconditions

G6 passed; UART pins/baud/fixture are frozen.

### Test commands

```sh
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests -R 'fw_protocol|fw_update_manager|uart_transport' --output-on-failure
python3 Tools/host/fw_update.py --transport uart info
python3 Tools/host/fw_update.py --transport uart install <SIGNED_IMAGE> --test --reboot
python3 Tests/Architecture/check_portable_dependencies.py
```

### Automated tests

Byte-stream chunking, overflow, timeout and unchanged protocol vectors.

### Manual / HIL tests

UART E2E upgrade, disconnect/reconnect and configured baud stress.

### Expected results

Same host commands/protocol behavior as USB; ISR only moves bytes/events.

### Required artifacts

Portable-core diff audit, serial logs, HIL evidence and version/slot result.

### PASS criteria

No manager/protocol transport fork and full UART E2E passes.

### FAIL criteria

UART-specific protocol, updater calls from ISR, changed Storage semantics or failed E2E.

## G8 — CAN Portability Gate

### Preconditions

G7 passed; arbitration IDs, bitrate, CAN FD data bitrate and fixture ownership are frozen.

### Test commands

```sh
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/HostTests -R 'fw_protocol|can_transport|can_fragment' --output-on-failure
python3 Tools/host/fw_update.py --transport can info
python3 Tools/host/fw_update.py --transport can install <SIGNED_IMAGE> --test --reboot
python3 Tests/CAN/run_fault_matrix.py
```

### Automated tests

8/64-byte adaptation, sequence, duplicate, loss, timeout, flow control and bus-load bounds.

### Manual / HIL tests

Classic CAN and CAN FD E2E, monitored bus errors, bus-off and recovery.

### Expected results

Same protocol/manager semantics; bounded bus use; deterministic bus-off result.

### Required artifacts

DBC/spec or ID manifest, bitrate, CAN logs/error counters, bus load, firmware hashes and HIL results.

### PASS criteria

Both CAN modes pass without fragmentation entering the update manager.

### FAIL criteria

Direct CAN-frame manager coupling, unbounded bus load/retry, silent frame loss or failed E2E.

## G9 — Cross-MCU Portability Gate

### Preconditions

G8 passed; second MCU/board/toolchain/probe and one proven transport are selected.

### Test commands

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --parallel
ctest --test-dir build/HostTests --output-on-failure
cmake --preset <SECOND_MCU_DEBUG> --fresh
cmake --build build/<SECOND_MCU_DEBUG> --parallel
python3 Tests/Architecture/check_portable_dependencies.py
python3 Tools/audit_portable_diff.py <G8_REVISION> HEAD
```

### Automated tests

Shared host suites, both MCU builds, dependency checks and portable-core diff audit.

### Manual / HIL tests

Second-MCU signed boot, update, rollback and confirmation using a proven transport.

### Expected results

New work is confined to platform/backend/build/layout except documented abstraction defect fixes.

### Required artifacts

Cross-MCU diff report, builds/maps/hashes, target identity and immutable HIL evidence.

### PASS criteria

Portable core/protocol builds unchanged for both platforms and second-MCU E2E/rollback passes.

### FAIL criteria

MCU conditionals in portable core, large core rewrite, missing second-target HIL or incompatible API fork.
