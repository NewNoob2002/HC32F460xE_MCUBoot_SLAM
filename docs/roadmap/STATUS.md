# Roadmap Status

Last updated: 2026-09-01

Allowed values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`.

| Phase | Status | Current evidence / blocker |
| --- | --- | --- |
| 0 — MCUboot baseline | PASSED | Rollback/confirmation HIL and CI |
| 1 — Architecture/contracts | PASSED | Ownership, memory map and ADR gates |
| 2 — Secondary storage/boot control | PASSED | Bounds/isolation/Pending HIL and CI |
| 3 — Protocol core | PASSED | Strict HostTests and 10,000-case malformed corpus |
| 4 — CherryUSB/HC32 loopback | PASSED | Enumeration, recovery, 10,000 transfers and 30-minute HIL |
| 5 — USB upgrade E2E | IN_PROGRESS | Linux and HC32 paths pass; signed Windows package/run remain |
| 6 — Failure/recovery matrix | NOT_STARTED | Requires bounded power/reset fixture |
| 7 — UART updater | NOT_STARTED | Starts after Phase 6 |
| 8 — CAN/CAN-FD updater | NOT_STARTED | Starts after Phase 7 |
| 9 — Second MCU | NOT_STARTED | Requires board selection |

## Current reviewed node

- Multi-device GUI: enumerate-only Refresh, selected-device Connect, single USB
  worker, DeviceInfo heartbeat and configured-SN post-upgrade reconnect.
- Product Config v3: write-once serial, hardware version and Application PID;
  signed compatibility remains build-time-only. Installation is blocked before
  BEGIN until the device is provisioned.
- Boot remains `cafe:0001`; App defaults to `cafe:0002` and applies the persisted
  PID before descriptor registration. Provisioned Boot/App use the same serial.
- Fixed external Debug signing key; Release requires an explicit external key.
  Public key source is generated without generating private keys under `build/`.
- `hc32_newlib` is included in all firmware ELFs and has a dedicated map-file
  verification target.
- Product Config v3 hardware evidence passed on 2026-09-01 with exact full-Flash
  restoration: `evidence/hil/2026-09-01-product-config-v3/`.
- Final local CI passed: 18 evidence bundles, Rust 21/21 plus Clippy, strict
  ASan/UBSan HostTests 17/17, Debug and Release firmware, all signed-image
  checks, four newlib map checks, and missing/P-384 signing-key rejection. Debug
  Boot uses 63,996/65,536 bytes; Release Boot uses 51,676/65,536 bytes.

Remote CI is recorded in the associated GitHub Actions run.

## Accepted residuals

- CLI commands do not yet close the protocol session explicitly; immediate
  successive processes can receive `BadSequence` until the 5000 ms timeout.
- CLI still lacks serial selection and automatic post-install reconnect.
- MCUboot scratch swap plus validation takes about three seconds for the current
  57 KiB updater on the first boot after upgrade; ordinary-boot timing is not yet
  separately instrumented.
- `boot_handover()` clears core interrupt/SysTick state. Boot USB is not active on
  the successful handover path; broader peripheral quiescing is deferred until a
  concrete ownership failure is demonstrated.

## Next gate

Build and validate externally signed Windows CLI/GUI executables and complete a
clean-Windows portable run. Power-loss testing and the PB13 capture remain
independent follow-up work.
