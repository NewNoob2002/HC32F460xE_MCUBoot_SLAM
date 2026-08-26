# Roadmap Status

Last updated: 2026-08-26

Allowed status values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`.

| Phase | Status | Gate | Tests | HIL | Blockers | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Phase 0 — Verified MCUboot Baseline | PASSED | G0 | HostTests 4/4; Debug/Release/signing CI passed | rollback/confirmation passed | None | `evidence/hil/2026-08-25-339f32c/`, CI run `32922559686` |
| Phase 1 — Architecture Audit and Contract Freeze | READY_FOR_REVIEW | G1 | Evidence 1/1; HostTests 4/4; Debug/Release/signing passed | Not required | Commit and CI required before `PASSED` | `docs/roadmap/`, `docs/adr/` |
| Phase 2 — Secondary Storage and Boot Control | NOT_STARTED | G2 | Not run | Required | G1 | None |
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
- [ ] Commit the reviewed documents and pass remote CI.

## Immediate next action

Commit the reviewed Phase 1 documents and pass remote CI. After G1 passes, start only Phase 2 by writing failing Secondary Storage/Boot-Control host contract tests. Do not start USB or Protocol work concurrently.

## Latest local G1 verification

Date: 2026-08-26

- `python3 Tests/HIL/verify_evidence.py`: 1 bundle verified.
- Strict HostTests with ASan/UBSan and `ASAN_OPTIONS=detect_leaks=0`: 4/4 passed.
- Debug build and `verify_app_image`: passed; Boot 26,804 B, App 9,796 B.
- Release build and `verify_app_image`: passed; Boot 24,112 B, App 9,224 B.
- Documentation whitespace/diff review: passed.
- Production source, CMake, CI and hardware state changed: no.
