# Roadmap Status

Last updated: 2026-08-27

Allowed status values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`.

| Phase | Status | Gate | Tests | HIL | Blockers | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Phase 0 — Verified MCUboot Baseline | PASSED | G0 | HostTests 4/4; Debug/Release/signing CI passed | rollback/confirmation passed | None | `evidence/hil/2026-08-25-339f32c/`, CI run `32922559686` |
| Phase 1 — Architecture Audit and Contract Freeze | PASSED | G1 | Local G1 passed; remote CI passed | Not required | None | revision `1429e30`, CI run `32925552505` |
| Phase 2 — Secondary Storage and Boot Control | PASSED | G2 | Strict HostTests 7/7; Debug/Release/signing and remote CI passed | Storage/range-isolation/Pending/restore passed | None | `evidence/hil/2026-08-26-5ebeae6-phase2/`, revisions `5ebeae6`/`0e1abd8`, CI `32928199086`/`32929570437` |
| Phase 3 — Protocol Core | PASSED | G3 | HostTests 9/9; corpus 10,000; Debug/Release/signing and remote CI PASS | Not required | None | `evidence/host/2026-08-26-e7899bd-phase3/`, revisions `e7899bd`/`515d0c5`, CI `32948384485` |
| Phase 4 — CherryUSB + HC32 DCD Loopback | PASSED | G4 | HostTests 11/11; Debug/Release image verification; CI passed | Enumeration, stall recovery, 10,000-transfer baseline, 10 unplug/re-enumeration recoveries, 30-minute run, counters and exact restore passed | None | `evidence/hil/2026-08-27-fd703cd-phase4-g4/`, revision `fd703cd`, evidence commit `2b63850`, CI `33043244338` |
| Phase 5 — USB Upgrade E2E | IN_PROGRESS | G5 | Rust 11/11; strict HostTests 12/12; Debug/Release App/loopback/updater verification passed locally | One v1→v2→confirm→reset→persist cycle passed; repeat/immutable evidence pending | Clean-revision repeat and evidence promotion; production VID/PID, WinUSB binding and signing inputs required before packaging | `docs/roadmap/PHASE5_USB_UPDATER_PLAN.md` |
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

Prepare a clean Phase 5 revision, repeat the v1 -> v2 -> confirmation ->
persistence cycle from a recorded baseline, and promote the working HIL logs to
an immutable evidence bundle. Keep the target on confirmed v2 until that run is
approved. Slint and packaging remain deferred until the repeat/evidence gate.

## Phase 5A planning start

Date: 2026-08-27

- Detailed plan: `docs/roadmap/PHASE5_USB_UPDATER_PLAN.md`.
- Python `Tools/host/usb_loopback.py` is frozen as a Phase 4 regression tool.
- Host direction changed from planned Python/PyUSB to one Rust client core with
  `info`, `install` and `wait`.
- Reviewed nusb planning reference: v0.2.3 blocking device/interface/Bulk APIs;
  Tokio is excluded.
- Execution order is fake E2E, real USB HIL, v1 -> v2 -> confirmation ->
  persistence, Slint single window, then Windows/Linux packages.
- CLI and GUI are sibling frontends over `FirmwareImage`, `ProtocolV1Client` and
  `UpgradeWorkflow`; nusb is only the USB adapter and re-enumeration policy stays
  in the workflow.
- Production code, Rust scaffold, GUI, package and hardware state changed: no.
- Phase 5/G5 status: `IN_PROGRESS` / not passed.

## Latest Phase 5B local result

Date: 2026-08-27

- Added one dependency-free `hc32-updater` Cargo package with the shared
  `FirmwareImage`, `ProtocolV1Client`, workflow progress events and the
  `info`/`install`/`wait` CLI surface. USB commands intentionally remain gated
  until Phase 5C supplies nusb.
- All 19 existing Protocol V1 Golden Vectors decode and re-encode byte-identically
  in Rust. MCUboot header/version parsing, bounded retry, non-retry disconnect,
  capacity rejection and CLI argument bounds are covered.
- The fake E2E test drives the production C Manager in a child process with fake
  Storage/Boot Control. A 777-byte image completes HELLO -> DEVICE_INFO -> BEGIN
  -> DATA -> END -> COMMIT -> RESET; stored bytes/padding are exact and pending
  is requested once.
- Rust tests: 8/8 passed; `cargo fmt` and clippy with warnings denied passed.
- Strict Werror + ASan/UBSan HostTests: 12/12 passed, including the Rust/C fake
  E2E, portable dependency checks and unchanged Phase 4 Python loopback self-test.
- Debug and Release firmware builds plus App and USB loopback signed-image
  verification passed. No production firmware source or hardware state changed.
- Phase 5B is locally complete. Phase 5C/G5 HIL and immutable evidence remain
  incomplete; G5 is not passed.

## Latest Phase 5C local result

Date: 2026-08-27

- Added the blocking nusb adapter for exactly-one-device discovery, interface 0
  claim and bounded Bulk OUT/IN transfers on the experimental `fffe:ffff`
  identity. The workflow, not nusb, owns re-enumeration polling.
- Added the production `usb_fw_updater` target. USB callbacks only publish
  events; the Application poll loop feeds the existing Manager, drains partial
  TX, reports TX-idle/disconnect/timeouts and calls platform reset only after
  the Manager emits RESET. The Phase 4 loopback target/tool are unchanged.
- Rust format and clippy passed; Rust tests are 11/11, including the real C
  Manager fake E2E and nusb frame assembly checks.
- Strict Werror + ASan/UBSan HostTests are 12/12. Retained evidence validation
  is 5/5. `git diff --check` passed.
- Debug and Release full builds passed. App, USB loopback and updater signed
  images all verified; updater size is 34,680 bytes Debug and 30,616 bytes
  Release. Strict conversion/shadow warnings are enforced on the new updater
  sources without treating vendored CherryUSB/legacy DCD warnings as new code.
- No physical USB command, target Flash write or upgrade HIL was run. Phase 5C
  is complete only at the local implementation/build level; G5 is not passed.

## Latest Phase 5C read-only HIL preflight

Date: 2026-08-27

- Authorized scope was one `info` invocation only: HELLO and DEVICE_INFO, with
  no install, reset, debugger attachment, power control or Flash/storage write.
- Passive USB enumeration found J-Link serial `000020781318` and CMSIS-DAP
  serial `5844333732`; no `fffe:ffff` updater device was present.
- The release updater binary SHA-256 was
  `7f5f55e80bbd3317f0cc02b0fd1bb7f0c102c1ad0631c60a77308b217d5a046a`.
- `hc32-updater info` exited once with `FAIL: USB device not found`. No Protocol
  frame was sent and no target state changed. Classified as
  `hil.hardware.unavailable.usb-device-not-enumerated`, not a product failure.
- Temporary preflight, result and raw log are under `/tmp/hc32-phase5-info-*`.
  Physical `info` remains pending; G5 is not passed.

## Latest Phase 5D core HIL result

Date: 2026-08-27

- Verified the pre-flash range `0x00000000-0x00075FFF` was the expected
  483,328-byte all-`0xff` image, SHA-256
  `410a0acccbb0a231d35508a9e545953b7490406986557750c531bd56edf39b1b`.
- Programmed and verified Boot at `0x00000000` and confirmed v1 updater Primary
  at `0x00010000` with J-Link `20781318`; VTref remained 3.348-3.354 V.
- Physical `info` passed for Application/Bootloader `1.0.0`, hardware
  `0x00004600`, board `1`, revision `2`, capacity `196608`, write alignment `4`
  and erase alignment `8192`.
- Installed the 25,470-byte signed v2.0.0 image through the Rust USB updater.
  Transfer, device readback verification, COMMIT and USB re-enumeration passed.
- v2 `wait` passed. Confirmation result, initialization result, USB error count
  and final Manager result were all zero. Primary contained v2, Secondary v1;
  Primary trailer had `copy_done=1`, `image_ok=1` and valid magic.
- After one separately preflighted J-Link reset, `wait 2.0.0` passed again.
  Primary/Secondary headers and trailers were byte-identical before and after
  reset. The target remains on confirmed v2.0.0.
- Working evidence: `build/HIL/phase5-20260827-ebad62c/`. This run used a dirty
  Phase 5 worktree; repeatability, clean revision and immutable evidence remain
  required. G5 is not passed.

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

## Phase 3E remote gate result

Date: 2026-08-26

- Source revision: `e7899bd8c2f96b138f6daab7d491268286e40a14`.
- Immutable Host Evidence revision: `515d0c548d0c6f243673eda461bd4786d653bd65`.
- GitHub Actions CI run `32948384485`: PASS. Job `build-and-test` and every
  required step passed: retained evidence verification, strict HostTests, Debug
  firmware, Release signing-key policy and Release firmware/signing.
- Phase 3 status: `PASSED`. G3 status: `PASSED`. HIL: not required.
- Phase 4 remains `NOT_STARTED`; no USB/CherryUSB/HC32 DCD implementation was
  started by this gate-close change.

## Latest Phase 4 G4 closure

Date: 2026-08-27

- Clean source revision `fd703cde5a312f05d74926f0c055fca6053d6bbb`;
  evidence commit `2b63850e19640f4691e152a446be850aa9993cfd`.
- Strict Werror + ASan/UBSan HostTests: 11/11 passed. Debug and Release full
  builds plus `verify_app_image` and `verify_usb_loopback_image`: passed.
- Descriptors and endpoint stall/clear-stall recovery passed; the 10,000
  mixed-length baseline completed in 6.028 seconds.
- Ten intentional transfer-time unplug/re-enumeration rounds each completed
  100/100 recovery transfers. Firmware errors stayed zero and packet counts
  increased monotonically.
- Continuous loopback completed 2,960,145 transfers in 1800.001 seconds. Final
  firmware counters were errors `0`, packets `3,496,742`.
- The original 483328-byte all-FF image was restored byte-for-byte at SHA256
  `410a0acccbb0a231d35508a9e545953b7490406986557750c531bd56edf39b1b`.
- Evidence verification and GitHub Actions run `33043244338`: passed.
- Evidence: `evidence/hil/2026-08-27-fd703cd-phase4-g4/`.
- Phase 4/G4 status: `PASSED`. Phase 5 may begin within its frozen scope.
