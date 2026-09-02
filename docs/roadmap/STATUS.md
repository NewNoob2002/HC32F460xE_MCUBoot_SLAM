# Roadmap Status

Last updated: 2026-09-02

Allowed values: `NOT_STARTED`, `IN_PROGRESS`, `BLOCKED`, `READY_FOR_REVIEW`, `PASSED`, `DEFERRED`.

| Phase | Status | Current evidence / blocker |
| --- | --- | --- |
| 0 — MCUboot baseline | PASSED | Rollback/confirmation HIL and CI |
| 1 — Architecture/contracts | PASSED | Ownership, memory map and ADR gates |
| 2 — Secondary storage/boot control | PASSED | Bounds/isolation/Pending HIL and CI |
| 3 — Protocol core | PASSED | Strict HostTests and 10,000-case malformed corpus |
| 4 — CherryUSB/HC32 loopback | PASSED | Enumeration, recovery, 10,000 transfers and 30-minute HIL |
| 5 — USB upgrade E2E | PASSED | Current development scope is complete and frozen |
| 6 — Failure/recovery matrix | DEFERRED | Expansion work explicitly paused |
| 7 — UART updater | DEFERRED | Expansion work explicitly paused |
| 8 — CAN/CAN-FD updater | DEFERRED | Expansion work explicitly paused |
| 9 — Second MCU | DEFERRED | Expansion work explicitly paused |

## Product Application track

| Node | Status | Current evidence / blocker |
| --- | --- | --- |
| A1 — I2C2 bus and BQ identity | BLOCKED | Strict HostTests 18/18 and clean Debug build pass; retained target evidence has no successful response from `0x0B`, `0x08` or `0x5C` |
| A2 — Charger/gauge telemetry | NOT_STARTED | Starts after A1 HIL |
| A3 — IAM/IBM/PSYS ADC validation | NOT_STARTED | Starts after A2 |
| A4 — Charging policy | BLOCKED | Requires maximum allowed pack charge current |
| A5 — Fault and charge/discharge HIL | NOT_STARTED | Starts after A4 |

## Current reviewed node

- Debug Boot/App logs share one EasyLogger port mirrored to USART3 and SEGGER
  RTT; Platform code remains independent of both logging frameworks.
- Fatal startup, handover and reset failures retain the `_Noreturn bsp_panic()`
  contract and solid-red error state.
- App A1 owns PA9/PA8 as 100 kHz I2C2, performs only the three configured address
  probes, and reads BQ40Z50 identity only after its address responds.
- Local gates on 2026-09-02: strict ASan/UBSan HostTests 18/18, Rust fmt/clippy,
  21 retained evidence bundles, clean Debug firmware, signed image, WinUSB
  descriptors and newlib syscall verification all pass. Debug Flash usage is
  64,860/65,536 bytes for Boot and 59,748/195,072 bytes for App.
- Hardware evidence remains unchanged: logging HIL passed with external PB13
  waveform capture pending, while charger A1 is blocked on powered device access.

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

Power/connect the three I2C devices on the identified charger target, verify
their rails and pull-ups, then rerun the exact A1 image to retain successful RTT
identity evidence for `0x0B`, `0x08` and `0x5C`. Do not start A2 before this gate.
