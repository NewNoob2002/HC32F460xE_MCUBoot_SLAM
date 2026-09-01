# Current Repository State

Audit date: 2026-09-01

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
  -> Boot clock/timebase/log initialization
  -> boot_go(): validate, select swap/revert/bootstrap, perform pending work
  -> valid image: validate vectors, clear SysTick/NVIC state, hand over
  -> no valid image: start cafe:0001 recovery updater

USB install
  -> HELLO once
  -> PRODUCT_CONFIG_GET must report provisioned=true
  -> DEVICE_INFO and compatibility/capacity checks
  -> BEGIN -> DATA -> END/CRC -> COMMIT/signature+TLV validation
  -> deferred reset -> MCUboot swap/bootstrap
  -> Application enumeration and version/SN verification
```

`usb_fw_updater` initializes clock, status LED, timebase, debug UART, watchdog,
FlashDB, Manager and USB, then confirms the running image after initialization.
USB callbacks publish events; the cooperative poll loop owns protocol, Flash and
reset work.

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
- Earlier evidence covers rollback/confirmation, Secondary isolation, protocol
  corpus, USB loopback endurance, Boot recovery/bootstrap, Application upgrade,
  GUI installation and exact restoration.
- Hosted CI covers strict HostTests, Rust fmt/clippy/fake E2E, Debug/Release
  firmware, signed-image verification, signing policy and newlib syscall maps.
- Host/build tests do not prove physical swap timing, USB enumeration, power-loss
  behavior or Windows execution.

## Remaining gates

- Production-signed Windows CLI/GUI executables and clean-Windows portable run.
- Systematic update/swap power-loss and interrupted-write recovery testing.
- Health-based confirmation and anti-rollback policy.
- Pending PB13 debug-UART capture on the all-IO board.
- UART updater, CAN/CAN-FD updater and second-MCU portability phases.
