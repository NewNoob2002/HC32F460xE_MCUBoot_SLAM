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
- Boot freeze review: checked 200 MHz clock establishment, exact Primary/header
  handover contract, MSP/reset-vector bounds, interrupt cleanup and assembly
  context transfer. Successful handover has no active USB instance.
- Product Config v3 hardware evidence passed on 2026-09-01 with exact full-Flash
  restoration: `evidence/hil/2026-09-01-product-config-v3/`.
- Boot freeze local verification passed: 18 evidence bundles, strict ASan/UBSan
  HostTests 17/17, Debug and Release firmware, all signed-image, descriptor and
  four newlib map checks. Debug Boot uses 64,208/65,536 bytes; Release Boot uses
  51,836/65,536 bytes.
- Final Boot freeze HIL passed on J-Link `63728710`: Boot/Primary verifybin,
  200 MHz clock, exact vectors/VTOR, clean assembly context transfer, App
  confirmation and a breakpoint-free reset/run all passed. Evidence:
  `evidence/hil/2026-09-01-boot-freeze-final/`; tag:
  `boot-freeze-2026-09-01`.

Remote CI is recorded in the associated GitHub Actions run.

## Accepted residuals

- CLI commands do not yet close the protocol session explicitly; immediate
  successive processes can receive `BadSequence` until the 5000 ms timeout.
- CLI still lacks serial selection and automatic post-install reconnect.
- MCUboot scratch swap plus validation takes about three seconds for the current
  57 KiB updater on the first boot after upgrade; ordinary-boot timing is not yet
  separately instrumented.
- Fault-injection hardening and monotonic anti-rollback remain product security
  policy decisions; the current baseline retains full signature validation on
  every boot and MCUboot test-swap rollback.

## Next gate

Build and validate externally signed Windows CLI/GUI executables and complete a
clean-Windows portable run. Power-loss testing and the PB13 capture remain
independent follow-up work.
