# Roadmap Status

Last updated: 2026-10-28

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
| A1 — I2C2 bus and BQ identity | READY_FOR_REVIEW | C++ typed nodes/HAL fakes and bounded broker pass host/build gates; V1.1 two-pack image still needs HIL |
| A2 — Charger/gauge telemetry | NOT_STARTED | Starts after A1 HIL |
| A3 — Register telemetry validation | NOT_STARTED | Starts after A2 |
| A4 — Charging policy | BLOCKED | Requires maximum allowed pack charge current |
| A5 — Fault and charge/discharge HIL | NOT_STARTED | Starts after A4 |

## Current reviewed node

- Debug Boot/App logs share one EasyLogger port mirrored to USART3 and SEGGER
  RTT; Platform code remains independent of both logging frameworks.
- Fatal startup, handover and reset failures retain the `_Noreturn bsp_panic()`
  contract and solid-red error state.
- App A1 uses V1.1 PA3/SCL and PA2/SDA as 100 kHz I2C2 and probes BQ40Z50
  `0x0B`/`0x0C`, HUSB238 `0x08` and MP2762A `0x5C` through the C++ Power
  node and injected HAL bus. PA9 is tied to 3V3 through 0 Ohm and must never
  be driven. PA4=INT#, PA5=PG (inverted ACOK#); ADC telemetry on PA7/PB0/PB1
  is removed. A3 now validates MP2762A/BQ40Z50 register telemetry.
- `main.cpp` and `App` preserve startup/loop ordering. The C++17 bounded
  DataBroker uses fixed arrays, embedded timers and typed DataProc readers.
  Watchdog/USB poll directly every pass; Charger uses 100 ms and Diagnostics
  5000 ms timers. Boot/components/shared BSP and shared App C sources stay C.
- V1.1 has red PB6/blue PB5 LEDs only; APP_PRIMARY uses red+blue heartbeat.
  The cyan diagnostic mode was removed.
- `App/Diagnostics` retains power-device startup results, uptime, loop/report
  counts, watchdog state and USB runtime state without heap, persistence or RTOS.
- The C++ rewrite passes HostTests 24/24, Debug image/WinUSB/syscall gates, Phase2
  HIL-mode compilation and Release image/syscall gates, including the new
  allocation-operator/guard/exception/RTTI map gate. Release uses the specified
  development key for compile proof only. No hardware was flashed.
- `arm-none-eabi-size build/Debug/App/app-1.0.0.elf` after the rewrite reports
  text=64,784, data=1,080, bss=8,284 bytes. Local `8446d9b` before editing
  measured 63,064/152/8,920: Flash (text+data) increases 2,648 bytes and RAM
  (data+bss) increases 292 bytes. Against the supplied reference baseline
  63,008/152/8,920, Flash increases 2,704 bytes; local baseline text differs
  by 56 bytes. These sizes exclude the MCUboot image header/signature/trailer.
- Historical local gates on 2026-09-03 passed: strict ASan/UBSan HostTests 20/20, Rust
  fmt/clippy/fake E2E, 23 retained evidence bundles, Debug/Release firmware,
  signed-image, WinUSB descriptor, newlib syscall and signing-key policy checks.
  Debug App uses 60,560/195,072 bytes Flash; Release App uses 47,580/195,072.
- The retained pre-refactor HIL proves the BQ40Z50 `0x0C` protocol and identity;
  the new `0x0B` pack probe and refactored image are not yet hardware-verified.

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

Deploy the reviewed App and retain one bounded RTT/UART capture proving both BQ
addresses (`0x0B`, `0x0C`), HUSB238 `0x08`, MP2762A `0x5C`, periodic diagnostics
and fault-free runtime. Do not start A2 before this gate.
