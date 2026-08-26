# Roadmap Status

Last updated: 2026-08-26

Allowed status values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`.

| Phase | Status | Gate | Tests | HIL | Blockers | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Phase 0 — Verified MCUboot Baseline | PASSED | G0 | HostTests 4/4; Debug/Release/signing CI passed | rollback/confirmation passed | None | `evidence/hil/2026-08-25-339f32c/`, CI run `32922559686` |
| Phase 1 — Architecture Audit and Contract Freeze | PASSED | G1 | Local G1 passed; remote CI passed | Not required | None | revision `1429e30`, CI run `32925552505` |
| Phase 2 — Secondary Storage and Boot Control | PASSED | G2 | Strict HostTests 7/7; Debug/Release/signing and remote CI passed | Storage/range-isolation/Pending/restore passed | None | `evidence/hil/2026-08-26-5ebeae6-phase2/`, revisions `5ebeae6`/`0e1abd8`, CI `32928199086`/`32929570437` |
| Phase 3 — Protocol Core | IN_PROGRESS | G3 | Phase 3E local matrix PASS: HostTests 9/9; corpus 10,000; Debug/Release/signing PASS | Not required | Remote CI | `evidence/host/2026-08-26-e7899bd-phase3/` |
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

Push the immutable G3 Host Evidence revision, pass remote CI, then mark G3 `PASSED` in a separate commit.

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
- Storage HIL: passed with logical capacity `0x30000`, first/last logical-word readback and erased trailer boundary.
- Boot-Control HIL: passed with known-valid unpadded signed image, TEST swap-info `0x02` and MCUboot magic written only in the trailer.
- Scratch SHA-256 was unchanged before/after both HIL profiles; Reserved was never accessed.
- The final non-Reserved readback was byte-identical to the pre-HIL backup, SHA-256 `3cadc91626a565bf3ee5454d21e29a58243f58f1ae1b118279d1c70e1f6a8dcf`.
- Evidence: `evidence/hil/2026-08-26-5ebeae6-phase2/`; evidence revision `0e1abd8`, remote CI run `32929570437`: passed.
- G2 status: `PASSED`.

## Latest Phase 3A execution

Date: 2026-08-26

- ADR-003 and Protocol V1 were reviewed and accepted.
- Detailed plan: `docs/roadmap/PHASE3_PROTOCOL_CORE_PLAN.md`.
- Wire contract: `docs/protocol/PROTOCOL_V1.md` (Accepted for Phase 3 implementation).
- Boundary decision: `docs/adr/ADR-003-protocol-v1-boundary.md` (Accepted).
- `python3 Tests/Protocol/verify_golden_vectors.py`: 19/19 vectors passed.
- Existing strict HostTests before RED reconfiguration: 7/7 passed.
- HostTests reconfiguration with `fw_protocol_tests`: passed.
- Expected local RED: `fw_protocol_tests.c:6` cannot find the intentionally
  absent `fw_update/protocol.h`; classified as test-first `build.compile.error`,
  not a product regression.
- Production source changed: no; Phase 3B started: no; HIL required: no.
- Phase 3 status: `IN_PROGRESS`; current subphase: Phase 3A complete, awaiting
  explicit Phase 3B approval.

## Latest Phase 3B execution

Date: 2026-08-26

- Implemented `fw_update/protocol.h` and `protocol.c`: stable wire constants,
  CRC-32/ISO-HDLC, exact-frame encode/decode and incremental byte parser.
- All 19 Golden Vectors decode and re-encode byte-identically.
- Tested payloads 0/1/511/512, all split points for minimum/DATA/maximum
  frames, byte-at-a-time input, coalesced frames and overlapping-magic resync.
- Tested malformed lengths 513/65535, flags, reserved field, truncation, extra
  bytes and CRC corruption in Header/Payload/CRC classes.
- Strict Werror + ASan/UBSan HostTests: 8/8 passed.
- Portable dependency check: passed.
- Debug/Release ARM builds and signed-image verification: passed; Boot/App sizes
  remained 26,804/9,796 B and 24,112/9,224 B respectively.
- C/C++ review findings: none blocking. Product runtime behavior and hardware
  state changed: no; HIL required: no.
- Phase 3 status: `IN_PROGRESS`; current subphase: Phase 3B complete, awaiting
  explicit Phase 3C approval. Commit/push: not performed.

## Latest Phase 3C execution

Date: 2026-08-26

- Implemented caller-allocated Manager and HELLO, DEVICE_INFO, BEGIN, DATA, END
  and ABORT command lifecycle; COMMIT remains unsupported.
- Strict logical offsets never become physical addresses. Manager-owned offsets
  are passed through the existing bounded Secondary Storage contract.
- Storage alignments 1/4/8/16/24/256/512 pass exact and boundary-tail cases;
  logical image bytes/CRC exclude erased-value physical padding.
- END performs bounded 512-byte readback CRC and enters READY_TO_COMMIT only on
  exact size and CRC success.
- Deterministic erase/write/read failures and read corruption pass expected
  STORAGE_ERROR/VERIFY_ERROR assertions; Boot-Control calls remain zero.
- Phase 3C HELLO capabilities are READBACK_CRC + STRICT_DATA only; TEST_UPGRADE
  stays clear until Phase 3D COMMIT exists.
- Manager size: 1720 bytes on Host ABI, below the 2048-byte hard limit.
- Strict Werror + ASan/UBSan HostTests: 9/9 passed.
- Portable dependency check, Debug/Release ARM build and signing verification:
  passed; existing Boot/App sizes remained unchanged.
- Review corrections: one signed capability-mask compiler finding and one fake
  failure-counter test defect were fixed and cleanly rerun.
- Phase 3 status: `IN_PROGRESS`; current subphase: Phase 3C complete, awaiting
  explicit Phase 3D approval. HIL: not required. Commit/push: not performed.

## Latest Phase 3D execution

Date: 2026-08-26

- Implemented expected-sequence progression, exact immediately-previous request
  replay and a three-error CRC/frame/sequence budget. Semantic responses advance
  sequence; BAD_FRAME, incompatible version and BAD_SEQUENCE do not.
- Exact duplicate HELLO/BEGIN/DATA/END/COMMIT/ABORT responses are byte-identical.
  Fake erase/write/read/Boot-Control call counts prove side effects occur once.
- Added explicit wrapping `now_ms` ingress/poll timeout, pre-COMMIT disconnect
  cleanup and post-COMMIT disconnect preservation of MCUboot pending state and
  COMMIT replay.
- Implemented COMMIT through the existing Boot-Control contract. RESET is emitted
  exactly once only after successful-response drain and TX-idle, in either event
  order; Boot-Control failure enters ERROR and emits no RESET.
- HELLO now truthfully advertises TEST_UPGRADE + READBACK_CRC + STRICT_DATA.
- Fixed-seed malformed corpus: 10,000 cases, seed `0x5EED3001`; 3,920 decoded
  frames, 1,960 format errors and 1,960 CRC errors; ASan/UBSan clean.
- Manager size: 1,824 bytes on the 64-bit Host ABI, below the 2,048-byte hard
  limit; ARM Debug/Release builds pass the same compile-time limit.
- Strict Werror + ASan/UBSan HostTests: 9/9 passed. Golden Vectors: 19/19.
  Portable dependency check, scoped cppcheck, Debug/Release ARM builds and signed
  image verification: passed.
- Phase 3 status: `IN_PROGRESS`; Phase 3D complete, Phase 3E is `IN_PROGRESS`. G3 is
  not PASSED. HIL: not required. Commit/push: not performed.

## Latest Phase 3E local evidence

Date: 2026-08-26

- Source revision: `e7899bd8c2f96b138f6daab7d491268286e40a14`;
  worktree was clean before the evidence matrix.
- Immutable bundle prepared at
  `evidence/host/2026-08-26-e7899bd-phase3/` with commands, normalized
  build/test/review contracts, raw result summaries, source/spec/vector hashes
  and artifact hashes.
- Strict Werror + ASan/UBSan HostTests: 9/9 PASS; corpus: 10,000 PASS;
  Golden Vectors: 19/19 PASS; Manager Host ABI: 1,824/2,048 bytes.
- Portable dependency, clang-format and scoped cppcheck checks: PASS.
- Debug build/signing: PASS, Boot/App 26,804/9,796 B. Release build/signing:
  PASS, Boot/App 24,112/9,224 B. Missing-key and ECDSA-P384 policy negatives
  were rejected as required.
- Existing retained evidence verification: 2/2 bundles PASS before adding the
  new Host bundle. HIL: not required and not performed.
- Phase 3/G3 remain `IN_PROGRESS`; evidence push and remote CI are pending.
