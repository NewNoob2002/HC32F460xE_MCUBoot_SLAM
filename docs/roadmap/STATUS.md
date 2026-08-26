# Roadmap Status

Last updated: 2026-08-26

Allowed status values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`.

| Phase | Status | Gate | Tests | HIL | Blockers | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Phase 0 — Verified MCUboot Baseline | PASSED | G0 | HostTests 4/4; Debug/Release/signing CI passed | rollback/confirmation passed | None | `evidence/hil/2026-08-25-339f32c/`, CI run `32922559686` |
| Phase 1 — Architecture Audit and Contract Freeze | PASSED | G1 | Local G1 passed; remote CI passed | Not required | None | revision `1429e30`, CI run `32925552505` |
| Phase 2 — Secondary Storage and Boot Control | IN_PROGRESS | G2 | Strict HostTests 7/7; Debug/Release/signing passed | Required before `PASSED` | Secondary range-isolation HIL not executed | Working tree |
| Phase 3 — Protocol Core | NOT_STARTED | G3 | Not run | Not required | G2 | None |
| Phase 4 — CherryUSB + HC32 DCD Loopback | NOT_STARTED | G4 | Not run | Required | G3, USB hardware details | None |
| Phase 5 — USB Upgrade E2E | NOT_STARTED | G5 | Not run | Required | G4 | None |
| Phase 6 — Failure and Recovery | NOT_STARTED | G6 | Not run | Required | G5, power/reset fixture | None |
| Phase 7 — UART Portability | NOT_STARTED | G7 | Not run | Required | G6 | None |
| Phase 8 — CAN/CAN FD Portability | NOT_STARTED | G8 | Not run | Required | G7 | None |
| Phase 9 — Second MCU Portability | NOT_STARTED | G9 | Not run | Required | G8, second board selection | None |

## Current Phase 1 checklist

- [x] Audit repository target graph, code ownership and HC32 coupling.
- [x] Extract Flash layout and Boot flow from source.
- [x] Classify current capability and test gaps.
- [x] Define target architecture, interface semantics and state machine.
- [x] Define phases, hard gates, test strategy and DoD.
- [x] Record firmware-update layering and CherryUSB strategy ADRs.
- [x] Review the plan against scope/YAGNI/layer/security constraints.
- [x] Run G1 baseline checks after all Phase 1 files are complete.
- [x] Confirm the worktree diff contains no production source/build-system change.
- [x] Commit the reviewed documents and pass remote CI.

## Immediate next action

Complete the Phase 2 host contracts and MCUboot Secondary/Boot-Control backends. Do not start USB or Protocol work concurrently.

## Latest local G1 verification

Date: 2026-08-26

- `python3 Tests/HIL/verify_evidence.py`: 1 bundle verified.
- Strict HostTests with ASan/UBSan and `ASAN_OPTIONS=detect_leaks=0`: 4/4 passed.
- Debug build and `verify_app_image`: passed; Boot 26,804 B, App 9,796 B.
- Release build and `verify_app_image`: passed; Boot 24,112 B, App 9,224 B.
- Documentation whitespace/diff review: passed.
- Production source, CMake, CI and hardware state changed: no.
- Phase 1 revision `1429e30`, remote CI run `32925552505`: passed.

## Latest local Phase 2 verification

Date: 2026-08-26

- RED: HostTests configuration failed before `components/fw_update` implementation existed.
- Strict HostTests with ASan/UBSan: 7/7 passed.
- Portable dependency check: passed.
- Debug and Release firmware/signing verification: passed; existing Boot/App sizes unchanged because the updater backend is not yet linked into App.
- Logical writable capacity: `SECONDARY_SLOT_SIZE - MCUBOOT_TRAILER_RESERVE` (`0x30000`).
- Full-slot erase is backend-controlled; Host logical offsets cannot write the final `0x2000` trailer sector.
- HIL Secondary range-isolation and known-valid pending-image checks: not executed.
