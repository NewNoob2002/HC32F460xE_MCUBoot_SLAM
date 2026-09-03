# Current Repository State

Audit date: 2026-09-03

This file is the compact source of truth for the implemented repository. Detailed
history remains in Git, ADRs and retained evidence bundles.

## Summary

- Bare-metal HC32F460xE Boot and Application firmware use MCUboot 2.4.0,
  ECDSA-P256 verification, Primary/Secondary slots, 8 KiB scratch swap, rollback
  and application confirmation.
- Boot enters `cafe:0001` USB recovery only when `boot_go()` finds no bootable
  image. Application USB uses the configured product PID, defaulting to
  `cafe:0002`.
- `components/fw_update/` provides a bounded, caller-allocated Protocol V1
  Manager with Secondary-only storage and MCUboot boot-control backends.
- `Tools/updater/` provides a shared Rust image/parser/client/workflow, a CLI and
  a Slint GUI using blocking nusb.
- Product Config v3 stores a write-once device serial, hardware-version string
  and Application PID in FlashDB. Signed-image compatibility remains fixed at
  build time. Earlier development schemas are intentionally unsupported.
- Physical Product Config v3 persistence, descriptors, install, exact-SN
  reconnect and full-Flash restoration passed on 2026-09-01.
- The product Application charger track is active. A1 initializes PA9/PA8 shared
  100 kHz I2C2, probes only BQ40Z50/HUSB238/MP2762A, and reads BQ40Z50 identity
  after a successful address response. Target acceptance remains blocked because
  the retained hardware run received no successful response from any device.

## Ownership

| Area | Responsibility |
| --- | --- |
| `Boot/` | MCUboot entry, recovery fallback and handover |
| `App/` | Default App, USB loopback and production USB updater executables |
| `components/fw_update/` | Portable protocol/manager plus MCUboot and FlashDB backends |
| `Platform/HC32F460/` | HC32 BSP, USB DCD, newlib syscalls and component ports |
| `Config/` and `cmake/` | Product identity, memory map, signing and artifact policy |
| `Tools/updater/` | Rust CLI, Slint GUI, nusb transport and packaging |
| `Tests/` | Host tests, protocol corpus, Rust/C fake E2E and HIL tools |
| `evidence/` | Checksummed host/HIL results; generated build trees do not belong here |

Vendored MCUboot, FlashDB, CherryUSB and most driver sources remain unmodified.
MCU-specific adaptation stays under `Platform/<MCU>/Ports/`.

## Flash layout

`cmake/MemoryMap.cmake` is canonical.

| Area | Start | Size | Role |
| --- | ---: | ---: | --- |
| Boot | `0x00000000` | `0x10000` | MCUboot and recovery updater |
| Primary | `0x00010000` | `0x32000` | Active MCUboot image |
| Secondary | `0x00042000` | `0x32000` | Candidate/previous image |
| Scratch | `0x00074000` | `0x2000` | Scratch-swap sector |
| Reserved | `0x00076000` | `0xA000` | FlashDB Product Config |

The App links at `0x00010200`. Updater writes are logical Secondary offsets,
aligned and bounded to `0x30000`; Host-provided physical addresses are never
accepted. MCUboot trailer offsets remain derived by upstream helpers.

## Boot and update flow

```text
reset
  -> checked Boot clock/timebase/log initialization
  -> boot_go(): validate, select swap/revert/bootstrap, perform pending work
  -> valid image: require internal Primary + fixed header, validate MSP/reset
  -> clear SysTick/NVIC state, assembly handover
  -> no valid image: start cafe:0001 recovery updater

USB install
  -> HELLO once
  -> PRODUCT_CONFIG_GET must report provisioned=true
  -> DEVICE_INFO and compatibility/capacity checks
  -> BEGIN -> DATA -> END/CRC -> COMMIT/signature+TLV validation
  -> deferred reset -> MCUboot swap/bootstrap
  -> Application enumeration and version/SN verification
```

The single `app_firmware` target initializes clock, status LED, timebase, debug
logging, bounded I2C2 power-device probes, watchdog, FlashDB, Manager and USB,
then confirms the running image. Missing power devices are reported without
blocking USB/update startup. USB callbacks publish events; the cooperative poll
loop owns protocol, Flash and reset work.

The current development board does not route PA9 as USB VBUS sense. Firmware
keeps that alternate function disabled and owns PA9/PA8 as I2C2 SCL/SDA at
100 kHz. When physical VBUS sensing is disabled, the HC32 USB device port
asserts the controller's software VBUS-valid override after core reset and
before device connection. Charger/battery implementation status is tracked in
`docs/roadmap/APP_CHARGER_PLAN.md`.

Debug Boot and Application EasyLogger output is mirrored to the existing UART
and SEGGER RTT channel 0. The Debug App places `_SEGGER_RTT` at
`0x1FFF8000` for J-Link MCP reconnect across reset. Platform code remains
independent of EasyLogger and writes its raw/panic output through USART3;
Release firmware omits EasyLogger and RTT.

Boot's frozen scope is secure image selection/swap, invalid-image USB recovery,
recovery-only provisioning and validated handover. Successful handover never
initializes USB; recovery exits only through system reset, so USB teardown is
not part of the handover path. Product logic and new transports stay outside
Boot. Boot/App startup unlock protected peripheral registers before
initialization and restore write protection before entering boot selection or
the cooperative main loop. BSP initialization functions do not close that
caller-owned startup window.

## Product identity and configuration

- Build-time compatibility: `hardware_id`, `board_id`, `board_revision`. These
  values are protected by the signed compatibility TLV and cannot be provisioned.
- Write-once Product Config v3: device serial (1-32 ASCII alphanumeric), hardware
  version (1-16 ASCII alphanumeric or dot), and approved Application PID.
- Boot PID is permanently `0001`. Application PID must be inside the configured
  range and cannot equal Boot PID.
- Before provisioning, USB serial is `HC32F460-<96-bit-UQID>` and App PID is
  `0002`. After provisioning, Boot/App use the configured serial and App uses the
  configured PID.
- Configuration is writable only in Boot recovery and only once. Changing it
  requires an explicitly preflighted Reserved-region erase or full restore.
- Installation is rejected before BEGIN when Product Config is not provisioned.

## Host updater

- Refresh enumerates candidates only. Connect opens the selected enumeration
  `DeviceId`, claims interface 0, performs HELLO and DeviceInfo, and then reads
  Product Config.
- GUI USB operations run on one worker. DeviceInfo is the 2000 ms heartbeat;
  cached HELLO is not retransmitted. Configuration and install cannot race the
  sequence number.
- After COMMIT, GUI reconnects only the original configured serial in Application
  mode and verifies the requested version.
- CLI and GUI share `FirmwareImage`, `ProtocolV1Client` and `UpgradeWorkflow`.

Known deferred host issues for a later node:

- Independent one-shot CLI commands may receive `BadSequence` until the 5000 ms
  device session expires because releasing a Host interface is not a USB physical
  disconnect. An explicit existing-ABORT close path is not yet implemented.
- CLI still requires a unique device and uses a separate `wait` command; GUI has
  selectable multi-device discovery and automatic SN-bound reconnect.

## Build and signing

- Firmware uses CMake/Ninja and GNU Arm Embedded; Host logic uses CTest and Cargo.
- Debug uses the fixed external development key configured by the Debug preset.
- Release/ReleaseNoLog never inherit the development key and require an explicit
  external ECDSA-P256 private key. Private keys are never generated or stored in
  the repository build tree.
- Generated public-key C source lives under `build/<preset>/generated/`.
- `hc32_newlib` supplies the project syscall implementations to every firmware
  ELF; `verify_newlib_syscalls` rejects libnosys fallbacks.

## Verification state

- Retained evidence integrity is checked by `Tests/HIL/verify_evidence.py`.
- Product Config v3 HIL is retained under
  `evidence/hil/2026-09-01-product-config-v3/`.
- Initial Boot freeze HIL is retained under
  `evidence/hil/2026-09-01-boot-freeze-final/`. The final single-App, USB
  descriptor and RTT HIL is retained locally under
  `build/local-evidence-backup/2026-09-01-single-app-hil/`; the frozen source
  is tagged `boot-freeze-2026-09-01-v2`.
- The PA9/I2C2 USB VBUS regression fix and repeated recovery, signed-image
  boot, unconfirmed upgrade/revert, confirmed upgrade persistence and fault-free
  runtime HIL are retained under `evidence/hil/2026-09-03-boot-freeze-v3/`;
  the source is tagged `boot-freeze-2026-09-03-v3`.
- Debug UART/RTT logging architecture and runtime HIL is retained under
  `evidence/hil/2026-09-02-logging-rtt-uart/`. RTT is closed-loop verified;
  USART3 configuration/transmit state is verified, while external PB13 waveform
  capture remains pending a USB-UART adapter.
- Earlier evidence covers rollback/confirmation, Secondary isolation, protocol
  corpus, USB loopback endurance, Boot recovery/bootstrap, Application upgrade,
  GUI installation and exact restoration.
- Hosted CI covers strict HostTests, Rust fmt/clippy/fake E2E, Debug/Release
  firmware, signed-image verification, signing policy and newlib syscall maps.
- Host/build tests do not prove physical swap timing, USB enumeration, power-loss
  behavior or Windows execution.

## Remaining gates

- Restore powered access to the three A1 I2C devices and capture successful
  address evidence plus BQ40Z50 identity; A2 remains gated on this result.
- Confirm the battery pack maximum allowed continuous charge current.
- Archive the current development-board schematic and revision.
- Updater reliability, UART, CAN/CAN-FD and second-MCU expansion remain
  explicitly deferred.
