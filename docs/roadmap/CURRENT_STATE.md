# Current Repository State

Audit date: 2026-08-26

Audited revision: `138d37ff3afe0a8655c98cade11bd7421c219918`

This document describes the repository as implemented. Historical milestone documents are supporting evidence, not the source of truth for current code.

## Repository architecture

### Build and target graph

```text
CMakeLists.txt
├── HostTests path
│   └── Tests/{memory_map,flash_map,boot_handover,app_confirm}
└── Firmware path
    ├── hc32_project_options
    ├── hc32_device -> hc32_ll -> hc32_platform
    ├── tinycrypt + mcuboot_asn1
    ├── mcuboot_port_hc32 -> hc32_platform
    ├── mcuboot_bootutil -> mcuboot_port_hc32
    ├── boot_firmware
    │   ├── hc32_startup_boot
    │   ├── hc32_platform / hc32_device
    │   └── mcuboot_bootutil
    └── app_firmware
        ├── hc32_startup_app
        ├── hc32_platform / hc32_device
        └── mcuboot_bootutil
```

Sources: `CMakeLists.txt`, `Drivers/CMakeLists.txt`, `Platform/CMakeLists.txt`, `components/CMakeLists.txt`, `components/mcuboot_port/CMakeLists.txt`, `Boot/CMakeLists.txt`, `App/CMakeLists.txt` and `Tests/CMakeLists.txt`.

### Ownership and portability

| Area | Current responsibility | Portability status |
| --- | --- | --- |
| `Boot/` | `boot_go()` invocation, safe failure loop and HC32 handover | Boot policy is portable in concept; `main.c` directly includes `hc32f460.h` |
| `App/` | Clock init and optional immediate image confirmation | HC32-bound executable; confirmation wrapper directly uses MCUboot public API |
| `components/mcuboot-2.4.0/` | Upstream validation, scratch swap, rollback and trailer state | Vendored upstream; project-specific edits are prohibited |
| `components/mcuboot_port/` | Flash areas, MCUboot configuration, signing key bridge | HC32 implementation and generated memory map are coupled |
| `Platform/HC32F460/` | Clock, Flash, startup support and boot handover | Intentionally HC32-specific |
| `Drivers/` | CMSIS/device/LL libraries and Boot/App startup objects | HC32-specific |
| `Tests/` | Four host contract tests plus reusable HIL assets | Host tests are portable only at mocked boundaries |
| `cmake/` | Memory map, toolchain/options, signing and artifacts | Project/HC32 build policy |

There is currently no `fw_update`, Protocol, Transport, USB, UART, CAN or host-updater component. No current directory can be treated as an existing portable updater core.

## Flash layout

The canonical project values are in `cmake/MemoryMap.cmake` and generated into `boot_memory_map.h`. The linker scripts independently assert the Boot/App link origins and limits.

| Area | Start | End inclusive | Size | Role |
| --- | ---: | ---: | ---: | --- |
| Boot | `0x00000000` | `0x0000FFFF` | `0x10000` (64 KiB) | MCUboot firmware |
| Primary Slot | `0x00010000` | `0x00041FFF` | `0x32000` (200 KiB) | Active MCUboot image slot |
| Secondary Slot | `0x00042000` | `0x00073FFF` | `0x32000` (200 KiB) | Candidate/previous image slot |
| Scratch | `0x00074000` | `0x00075FFF` | `0x2000` (8 KiB) | Scratch swap sector |
| Reserved | `0x00076000` | `0x0007FFFF` | `0xA000` (40 KiB) | Not exposed by the Flash map |

Application image layout within a slot:

| Item | Offset / size | Source |
| --- | --- | --- |
| MCUboot header | slot offset `0x0000`, reserved `0x0200` | `MCUBOOT_HEADER_SIZE`, `imgtool --header-size` |
| Linked App | Primary address `0x00010200`, maximum `0x2FA00` | `Linker/app.ld`, `APP_LINK_ORIGIN/SIZE` |
| TLV allowance | up to `0x0400` reserved by project sizing | `MCUBOOT_TLV_RESERVE` |
| Trailer erase reserve | final `0x2000` sector | `MCUBOOT_TRAILER_RESERVE` |

With 25 sectors, three scratch-swap status states and 4-byte Flash write alignment, upstream MCUboot computes 300 bytes of status plus 48 bytes of trailer information: 348 bytes (`0x15C`) inside the reserved final sector. Important final-slot offsets are derived by upstream helpers, not duplicated project constants:

| Field | Slot-relative offset | Primary absolute | Secondary absolute |
| --- | ---: | ---: | ---: |
| swap size | `0x31FD0` | `0x00041FD0` | `0x00073FD0` |
| swap info | `0x31FD8` | `0x00041FD8` | `0x00073FD8` |
| copy done | `0x31FE0` | `0x00041FE0` | `0x00073FE0` |
| image OK | `0x31FE8` | `0x00041FE8` | `0x00073FE8` |
| magic | `0x31FF0..0x31FFF` | `0x00041FF0..0x00041FFF` | `0x00073FF0..0x00073FFF` |

Sources: `components/mcuboot-2.4.0/boot/bootutil/src/bootutil_area.c`, `bootutil_area.h`, `bootutil_misc.h`, `components/mcuboot_port/Src/flash_map_backend.c` and `components/mcuboot_port/Inc/mcuboot_config/mcuboot_config.h`.

The project reserve is deliberately larger than the currently computed trailer. Future code must use MCUboot APIs/helpers rather than copying these offsets.

## Current boot flow

```text
Power on / reset
  -> HC32 Reset_Handler and SystemInit (Boot VTOR = 0)
  -> Boot main
  -> bsp_clock_init()
  -> boot_go(&rsp)
       -> open Primary / Secondary / Scratch areas
       -> validate configured Primary image
       -> inspect trailer state and select swap type
       -> validate candidate image when applicable
       -> perform/resume scratch swap or revert
       -> return selected Primary image offset/header
  -> boot_handover(&rsp)
       -> validate vector address, MSP and Thumb reset vector
       -> disable IRQ/SysTick and clear NVIC state
       -> set VTOR/MSP and branch to App Reset_Handler
  -> App main
  -> bsp_clock_init()
  -> app_confirm_running_image(APP_AUTO_CONFIRM)
       -> boot_set_confirmed() when enabled
  -> idle loop
```

Repository entry points:

- Boot entry and `boot_go`: `Boot/Core/Src/main.c`.
- MCUboot configuration: `components/mcuboot_port/Inc/mcuboot_config/mcuboot_config.h`.
- Flash areas: `components/mcuboot_port/Src/flash_map_backend.c`.
- Handover validation/state cleanup: `Platform/HC32F460/Src/boot_handover.c`.
- App entry/confirmation: `App/Core/Src/main.c` and `App/Core/Src/app_confirm.c`.

## Current upgrade capability

### Implemented

- MCUboot 2.4.0 scratch swap with one image and Primary/Secondary/Scratch areas.
- ECDSA-P256 signed image generation and Boot public-key generation.
- Primary validation, test upgrade, revert, confirmation and handover.
- Confirmed Primary and test-pending Secondary image artifacts.
- Host tests, Debug/Release builds, signing policy checks and retained rollback HIL evidence.

### Partially implemented

- Application confirmation exists, but it occurs immediately after clock initialization and is not guarded by bounded product health checks.
- Flash area APIs can access Secondary, but they are an MCUboot port rather than a safe Application updater storage contract.
- Manual debugger/probe programming can place an update into Secondary; there is no runtime receive/session path.
- Rollback is implemented; anti-rollback/version security counters are not.

### Not implemented

- FW Update Manager/session/state machine.
- Transport or protocol contracts and implementations.
- USB Vendor Bulk, CherryUSB, HC32 USB DCD, UART, CAN or CAN FD.
- Host updater tool, device discovery and protocol specification.
- Download resume, capability negotiation and transfer error recovery.
- Runtime Secondary erase/write/finalize/request-pending flow.
- Hardware/board compatibility enforcement and downgrade policy.
- Minimal Boot recovery profile.

MCUboot's upstream ability to validate or swap an image is not evidence that this repository can receive that image at runtime.

## Current test coverage

| Level | Current coverage | Main gap |
| --- | --- | --- |
| Host unit | Memory map, Flash map bounds/alignment, handover address helper, confirmation wrapper | No updater/protocol/state-machine code exists |
| Component integration | MCUboot Flash backend with mocked BSP Flash | No full `boot_go()` host integration or Flash power-loss model |
| Firmware build | Debug/Release, image signing/verification, layout and key policy in CI | No size regression threshold beyond partition/link failure |
| HIL/manual | Boot, test swap, revert, confirmation and persistence on HC32 | No reset/power loss during individual swap operations |
| Fault injection | Unconfirmed test boot followed by reset/revert | No erase/write failure, corrupted trailer or interrupted swap matrix |
| CI | Evidence checksum, strict host sanitizers, firmware builds/signing | No architecture dependency rule yet |

The largest current gap is the entire Application-side update path. The largest baseline reliability gap is systematic power-interruption testing during swap.

## Technical debt and risks

### P0 Critical

No P0 defect was identified in the protected baseline. A new P0 is any path that permits host-controlled physical addresses, writes outside Secondary, or bypasses MCUboot image trust.

### P1 High

- Application confirmation is immediate; a broken test image can confirm before product health is established.
- Anti-rollback/downgrade protection is absent. Signed older images remain acceptable unless a later policy rejects them.
- The future updater has no bounded power-loss/download recovery behavior yet.
- The App currently links MCUboot Boot Utility to call one confirmation API; future updater code must not gain unrestricted Flash-area selection through that dependency.

### P2 Medium

- `hc32_project_options` and current firmware targets propagate HC32 compile definitions broadly. Portable code needs an isolated host-buildable target when it first exists.
- The MCUboot Flash port uses generic `-1` returns and exposes Primary/Secondary/Scratch area IDs; a public updater contract needs defined errors and Secondary-only capability.
- No automated rule prevents future portable components from including HC32 headers.
- Boot watchdog hooks are no-ops, which may matter when validation/swap time grows.
- Fault-injection hardening is configured off (`MCUBOOT_FIH_PROFILE_OFF`).

### P3 Low

- Historical milestone plan/spec are intentionally frozen and must not be mistaken for the active roadmap.
- Current status/evidence tracking was split across README, build report and milestone plan before this roadmap.

## Baseline conclusion

The minimal compile/build/sign/boot/swap/rollback/confirmation project is complete and CI/HIL evidenced. The product firmware-update framework is not implemented. The next work is the architecture contract freeze described by G1; no transport should be started before it passes.
