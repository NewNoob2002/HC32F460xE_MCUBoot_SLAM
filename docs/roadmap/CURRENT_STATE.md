# Current Repository State

Audit date: 2026-08-26

Audited baseline revision: `9d29ee9843c711d7f853d7c24a7df58be8e8e1e9`

Current Phase 3 scope: reviewed Phase 3A-3D source revision `e7899bd` has a
complete immutable Phase 3E Host Evidence bundle. Its evidence revision must pass
remote CI before G3 may be marked PASSED.

This document describes the repository as implemented. Historical milestone documents are supporting evidence, not the source of truth for current code.

## Repository architecture

### Build and target graph

```text
CMakeLists.txt
├── HostTests path
│   ├── Tests/{memory_map,flash_map,boot_handover,app_confirm}
│   ├── Tests/{fw_update_contract,fw_update_mcuboot_backend}
│   └── portable dependency check
└── Firmware path
    ├── hc32_project_options
    ├── hc32_device -> hc32_ll -> hc32_platform
    ├── tinycrypt + mcuboot_asn1
    ├── mcuboot_port_hc32 -> hc32_platform
    ├── mcuboot_bootutil -> mcuboot_port_hc32
    ├── fw_update_core
    ├── fw_update_mcuboot -> fw_update_core + mcuboot_bootutil
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
| `App/` | Clock init, default optional immediate confirmation and test-only Phase 2 HIL profiles | HC32-bound executable; default confirmation wrapper directly uses MCUboot public API |
| `components/mcuboot-2.4.0/` | Upstream validation, scratch swap, rollback and trailer state | Vendored upstream; project-specific edits are prohibited |
| `components/mcuboot_port/` | Flash areas, MCUboot configuration, signing key bridge | HC32 implementation and generated memory map are coupled |
| `components/fw_update/` | Portable Storage/Boot-Control contracts, Protocol V1 codec/parser, receive/verification Manager and MCUboot backends | Core is host-buildable and dependency-checked; MCUboot backend is intentionally non-portable |
| `Platform/HC32F460/` | Clock, Flash, startup support and boot handover | Intentionally HC32-specific |
| `Drivers/` | CMSIS/device/LL libraries and Boot/App startup objects | HC32-specific |
| `Tests/` | Nine strict HostTests, Golden Vector verifier, fixed-seed malformed corpus, portable dependency check and reusable HIL assets | G3 tracked evidence/remote CI remain |
| `cmake/` | Memory map, toolchain/options, signing and artifacts | Project/HC32 build policy |

There is now a bounded portable `fw_update` Storage/Boot-Control foundation,
Protocol V1 codec/parser and Manager lifecycle through sequence/replay, receive,
verification, COMMIT and an emitted RESET action. Production Application glue,
Transport, USB, UART, CAN and the host updater are not implemented.

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
- Portable Secondary-only Storage and Boot-Control C contracts with named errors.
- MCUboot Secondary backend with logical capacity `0x30000`, backend-controlled
  full-slot erase, aligned write/read and trailer exclusion from Host offsets.
- MCUboot Boot-Control backend for test-pending and running-image confirmation.
- Host contract/backend tests, portable forbidden-dependency check and retained
  Phase 2 Storage/Pending/range-isolation/restoration HIL evidence.
- Portable Protocol V1 framing/parser and caller-allocated Manager with bounded
  sequence/error handling, exact duplicate replay, timeout/disconnect, aligned
  receive/readback verification, single-effect COMMIT and TX-drain/TX-idle RESET
  action lifecycle.
- Fixed-seed 10,000-case malformed parser corpus under ASan/UBSan.

### Partially implemented

- Application confirmation exists, but it occurs immediately after clock initialization and is not guarded by bounded product health checks.
- Safe Application-facing Storage/Boot-Control contracts and the Phase 3D
  portable Manager exist, but no production Application loop currently feeds
  bytes, polls time, reports disconnect/TX-idle or executes the RESET action.
- Manual debugger/probe programming can place an update into Secondary; there is no runtime receive/session path.
- Rollback is implemented; anti-rollback/version security counters are not.

### Not implemented

- Production Transport C contract/implementations and Application orchestration.
- USB Vendor Bulk, CherryUSB, HC32 USB DCD, UART, CAN or CAN FD.
- Host updater tool and device discovery.
- Download resume, Host retry policy and transfer recovery after reset.
- Runtime App wiring that invokes the portable Manager against real time,
  Transport, MCUboot backend and platform reset.
- Signed hardware/board compatibility metadata and downgrade policy.
- Minimal Boot recovery profile.

MCUboot's upstream ability to validate or swap an image is not evidence that this repository can receive that image at runtime.

## Current test coverage

| Level | Current coverage | Main gap |
| --- | --- | --- |
| Host unit | Memory/Flash map, handover, confirmation, fw_update contracts/backends, Protocol codec/parser, 10,000-case corpus and Manager reliability/receive/verify/COMMIT/reset lifecycle with fault injection | No production App/Transport integration exists |
| Component integration | MCUboot Flash backend with mocked BSP Flash plus Storage/Boot-Control backend fakes | No full `boot_go()` host integration or Flash power-loss model |
| Firmware build | Debug/Release, image signing/verification, layout and key policy in CI | No size regression threshold beyond partition/link failure |
| HIL/manual | Boot/swap/revert/confirmation plus Phase 2 Secondary erase/boundary readback, Pending trailer and exact restoration | No runtime transfer or reset/power loss during individual operations |
| Fault injection | Unconfirmed test boot followed by reset/revert | No erase/write failure, corrupted trailer or interrupted swap matrix |
| CI | Evidence checksum, nine strict local HostTests, portable dependency rule, firmware builds/signing | G3 Host Evidence revision is not yet remote-verified |

The largest current delivery gap is Phase 3E remote CI, followed by production
Application/Transport integration. The largest baseline reliability gap remains systematic
power-interruption testing during swap/update operations.

## Technical debt and risks

### P0 Critical

No P0 defect was identified in the protected baseline. A new P0 is any path that permits host-controlled physical addresses, writes outside Secondary, or bypasses MCUboot image trust.

### P1 High

- Application confirmation is immediate; a broken test image can confirm before product health is established.
- Anti-rollback/downgrade protection is absent. Signed older images remain acceptable unless a later policy rejects them.
- The future updater has no bounded power-loss/download recovery behavior yet.
- The App currently links MCUboot Boot Utility to call one confirmation API; future updater code must not gain unrestricted Flash-area selection through that dependency.

### P2 Medium

- `hc32_project_options` and firmware targets still propagate HC32 compile
  definitions broadly, although `fw_update` core is independently exercised by
  HostTests.
- The MCUboot Flash port retains generic native error returns internally; the
  public updater contract maps them to named errors.
- The dependency checker is token-based and must expand with new portable
  Protocol/Manager files and forbidden APIs.
- Boot watchdog hooks are no-ops, which may matter when validation/swap time grows.
- Fault-injection hardening is configured off (`MCUBOOT_FIH_PROFILE_OFF`).

### P3 Low

- Historical milestone plan/spec are intentionally frozen and must not be mistaken for the active roadmap.
- Current status/evidence tracking was split across README, build report and milestone plan before this roadmap.

## Baseline conclusion

The minimal compile/build/sign/boot/swap/rollback/confirmation baseline and the
Phase 2 Secondary Storage/Boot-Control foundation are complete and CI/HIL
evidenced. Phase 3A-3D portable Protocol/Manager implementation and tracked Host
Evidence are complete, but G3 remains open until the evidence revision passes
remote CI.
The full runtime firmware-update framework is not implemented; no USB/UART/CAN
work may start before G3.
