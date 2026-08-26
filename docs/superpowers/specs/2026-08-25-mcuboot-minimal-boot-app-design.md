# Minimal MCUboot Boot + App Design

## Status

Implemented historical design. Approved on 2026-08-25 and completed through the physical rollback HIL. Current verification evidence is retained under evidence/hil/2026-08-25-339f32c/; future architecture work must use a separate current-state document rather than extending this milestone specification.

## Goal

Build a GCC/CMake-maintained HC32F460xE firmware workspace that produces:

- a 64 KiB MCUboot bootloader;
- a simple application linked for the primary image slot;
- ECDSA-P256 signed application images;
- confirmed primary-install and test-upgrade images;
- MCUboot scratch-swap support with application confirmation and revert behavior.

The design must keep MCU-specific Flash, startup and handover code outside MCUboot and future transport/protocol-independent upgrade logic.

## First-Milestone Scope

The milestone includes the complete build, image and boot lifecycle needed for signed scratch-swap firmware. It does not include USB, CAN, UART, an OTA wire protocol, anti-rollback security counters, automatic flashing or target HIL execution.

The two 200 KiB application partitions are MCUboot primary and secondary slots, not two independently selected applications. MCUboot always hands control to the validated image in the primary slot after completing any required swap.

## Repository and CMake Structure

The project follows the recognizable CubeMX `Core` and `Drivers` layout while using target-scoped CMake dependencies instead of one executable containing every source file.

```text
CMakeLists.txt
CMakePresets.json
cmake/
├── gcc-arm-none-eabi.cmake
├── ProjectOptions.cmake
├── MemoryMap.cmake
└── FirmwareArtifacts.cmake

Drivers/
└── CMakeLists.txt

Platform/
├── CMakeLists.txt
└── HC32F460/
    ├── Inc/
    └── Src/

components/
├── CMakeLists.txt
├── mcuboot-2.4.0/
└── mcuboot_port/
    ├── CMakeLists.txt
    ├── Inc/
    └── Src/

Boot/
├── CMakeLists.txt
└── Core/
    ├── Inc/
    └── Src/

App/
├── CMakeLists.txt
└── Core/
    ├── Inc/
    └── Src/

Linker/
├── boot.ld
└── app.ld

Tests/
└── CMakeLists.txt
```

The existing root `Core/` sources are moved under `Boot/Core/` so image ownership is explicit. Files are preserved during the move.

### Root CMake Responsibilities

The root `CMakeLists.txt` declares C, C++ and ASM languages, loads the memory map and project options, then adds the layers in dependency order:

```cmake
add_subdirectory(Drivers)
add_subdirectory(Platform)
add_subdirectory(components)
add_subdirectory(Boot)
add_subdirectory(App)

if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
```

It does not own vendor source lists, application source lists or global include directories.

### CMake Target Graph

```text
hc32_project_options (INTERFACE)

hc32_device (INTERFACE)
    └── hc32_ll (STATIC)
          └── hc32_platform (STATIC)
                └── mcuboot_port_hc32 (STATIC)

tinycrypt (STATIC)
mcuboot_asn1 (STATIC)
    └── mcuboot_bootutil (STATIC)

boot_firmware
    ├── hc32_startup_boot (OBJECT)
    ├── hc32_platform
    ├── mcuboot_port_hc32
    └── mcuboot_bootutil

app_firmware
    ├── hc32_startup_app (OBJECT)
    ├── hc32_platform
    ├── mcuboot_port_hc32
    └── mcuboot_bootutil
```

`hc32_startup_boot` and `hc32_startup_app` compile the same vendor startup and system sources with target-specific `VECT_TAB_OFFSET` values, so `SystemInit()` preserves the correct VTOR for each image. The ICG source is included only in the Boot startup object. Object libraries ensure the vector table, `Reset_Handler` and Boot ICG data cannot be omitted by static-library extraction. The App links `mcuboot_bootutil` so `boot_set_confirmed()` uses MCUboot's public API rather than duplicating trailer layout logic. Static-library extraction keeps unrelated bootloader objects out of the App image.

All compile definitions, include directories and options use `target_*` commands. The project does not use global `include_directories()`, `add_definitions()` or duplicated Boot/App vendor source lists.

The vendored `components/mcuboot-2.4.0/` directory is not modified. `components/CMakeLists.txt` and `components/mcuboot_port/` wrap the required upstream sources and implement the HC32 port.

Future transport, protocol and upgrade-manager targets are added only when their first implementation is introduced. Empty interface libraries are not created in this milestone.

## Flash Layout

The HC32F460xE provides 512 KiB of internal Flash with an 8 KiB erase sector and 4-byte program alignment.

| Area | Start | End, inclusive | Size | MCUboot role |
|---|---:|---:|---:|---|
| Boot | `0x00000000` | `0x0000FFFF` | 64 KiB | Bootloader |
| Primary | `0x00010000` | `0x00041FFF` | 200 KiB | Image 0 slot 0 |
| Secondary | `0x00042000` | `0x00073FFF` | 200 KiB | Image 0 slot 1 |
| Scratch | `0x00074000` | `0x00075FFF` | 8 KiB | Swap scratch |
| Reserved | `0x00076000` | `0x0007FFFF` | 40 KiB | Unused |

`MCUBOOT_SWAP_USING_SCRATCH` is selected because it supports equal-size primary and secondary slots, test swaps and reverts with one additional erase sector. Overwrite-only cannot meet the revert requirement. Swap using offset would require a slot-size asymmetry and therefore does not match the approved two 200 KiB slots.

`cmake/MemoryMap.cmake` is the build-system source of truth for these values. It generates a C header for the port and passes the corresponding origin and length symbols to the two linker scripts and to `imgtool`. Configure-time checks enforce contiguity, total Flash capacity and 8 KiB alignment.

## Linker Layout

The Boot image begins at `0x00000000`, is limited to 64 KiB and retains the required HC32 ICG block at `0x00000400`. A linker assertion rejects Boot overflow or incorrect ICG placement.

The signed application uses these reserved sizes:

| Item | Size |
|---|---:|
| MCUboot image header | `0x0200` |
| Maximum signing TLV allowance | `0x0400` |
| Trailer erase-sector reserve | `0x2000` |

The App vector table is linked at `0x00010200`, immediately after the image header. Its maximum linked Flash length is `0x0002FA00`, calculated as:

```text
0x32000 slot - 0x200 header - 0x400 TLV allowance - 0x2000 trailer = 0x2FA00
```

`imgtool --slot-size 0x32000` remains the authoritative final overflow check after adding the actual TLV data.

## MCUboot Port

`mcuboot_port_hc32` provides only the platform contracts required by the selected MCUboot configuration:

- `flash_area_open()`, close, read, write and erase;
- Flash area descriptors for primary, secondary and scratch;
- sector enumeration, erased value and 4-byte minimum write alignment;
- boot logging stubs or the selected minimal log backend;
- bootloader public key data;
- HC32 application handover.

The Flash backend validates area IDs, offsets, lengths and overflow before translating an area-relative offset to an absolute address. Erase operations require complete 8 KiB sectors, writes require 4-byte address alignment, and no slot API exposes the Boot region. Hardware driver failures are returned to MCUboot unchanged as port errors.

Application handover uses the `boot_rsp` returned by `boot_go()`. The vector address is calculated from `rsp.br_image_off + rsp.br_hdr->ih_hdr_size`; it is not hard-coded to the current header size. The HC32 platform disables interrupts and SysTick, clears relevant NVIC state, updates VTOR and MSP, then branches to the application's reset handler.

## Cryptography and Key Handling

The initial configuration uses ECDSA-P256 signatures with MCUboot's vendored TinyCrypt implementation and SHA-256. The small vendored `mbedtls-asn1` parser is compiled only to decode DER public keys and signatures; the Mbed TLS cryptography backend is not added.

Debug builds may generate a temporary ECDSA-P256 development key under the build directory. The private key is never added to the source tree. The generated public key source is an explicit dependency of `boot_firmware`.

Release configuration requires an explicit `MCUBOOT_SIGNING_KEY` path. Configuration fails if the key is absent. The same key drives public-key generation for Boot and `imgtool` signing for App, preventing an accidental key mismatch.

## Image Artifacts

The normal build produces:

```text
boot_firmware.elf
boot_firmware.map
boot_firmware.hex
boot_firmware.bin
app_firmware.elf
app_firmware.map
app_firmware.bin
app_signed.bin
app_primary.bin
app_update.bin
```

`app_firmware.bin` is an intermediate raw binary and is not bootable by MCUboot. `app_signed.bin` contains the MCUboot header, hash TLV and ECDSA signature without slot padding.

`app_primary.bin` is padded to the 200 KiB slot and marked confirmed for initial programming into Primary. `app_update.bin` is padded to the same slot size and marked for a test swap when programmed into Secondary.

The signing commands use the vendored MCUboot `imgtool.py` with these shared parameters:

```text
--align 4
--header-size 0x200
--slot-size 0x32000
--pad-header
--version <APP_VERSION>
--key <MCUBOOT_SIGNING_KEY>
```

The primary artifact adds `--pad --confirm`; the update artifact adds `--pad --test`. `APP_VERSION` is a CMake cache setting used both by the application and the signing command.

## Boot, Confirmation and Revert Flow

For initial installation, `boot_firmware.bin` is programmed into Boot and `app_primary.bin` into Primary. The confirmed trailer makes the initial image permanent.

For an offline test upgrade, `app_update.bin` is programmed into Secondary. On reset, MCUboot validates the image, performs the scratch swap and starts the new image from Primary. MCUboot trailer state provides power-loss progress recovery during the swap.

The simple App initializes the clock and then follows the `APP_AUTO_CONFIRM` CMake setting:

- `ON`: call `boot_set_confirmed()` and store the result in a debugger-visible variable;
- `OFF`: remain unconfirmed so the next reset exercises MCUboot revert.

If the test image resets before confirmation, MCUboot swaps the previous image back from Secondary. If confirmation succeeds, subsequent resets retain the new image.

This behavior is rollback/revert. Version security counters, OTP storage and rejection of older signed versions are anti-rollback features and remain outside this milestone.

## Failure Behavior

- A failed `boot_go()` call or absence of a valid Primary image enters a safe loop and never performs a raw vector jump.
- Invalid image headers, hashes, signatures or trailer state are handled by MCUboot and are not overridden by platform code.
- Flash range, alignment and driver errors abort the affected operation through MCUboot's error path.
- A failed App confirmation remains failed; the App does not write trailer flags directly or claim success.
- No build target automatically flashes hardware or handles a production private key.

## Verification

### Host Tests

CTest covers:

- partition contiguity, total size and 8 KiB alignment;
- flash area IDs, offsets, lengths and sector enumeration;
- rejection of out-of-range, overflowed and misaligned operations using a mocked low-level Flash driver;
- calculation of the application vector address from image offset and header size.

### Firmware and Artifact Checks

Debug and Release presets must build both firmware targets without warnings. Link and post-build checks prove:

- Boot does not exceed 64 KiB;
- the ICG block starts at `0x00000400` and has the expected size;
- the App linked region stays within its configured allowance;
- signed and padded images do not exceed 200 KiB;
- `imgtool` can parse and verify the generated signed image;
- the Boot public key matches the private key used for signing.

The firmware build enables `MCUBOOT_SWAP_USING_SCRATCH`; it does not substitute a local swap implementation. Upstream MCUboot simulator tests may be run as additional evidence but are not added to the normal GCC/CMake firmware build.

### Hardware Acceptance

Hardware execution was completed separately on 2026-08-25 after the software-only milestone. The retained procedure is:

1. program Boot and a v1 confirmed Primary image;
2. confirm that v1 boots;
3. program a v2 test image into Secondary;
4. reset and confirm that v2 boots after the swap;
5. leave v2 unconfirmed, reset and confirm that v1 is restored;
6. repeat the upgrade with automatic confirmation enabled and verify that v2 persists across resets.

## Completion Criteria

The software milestone is complete when:

- Debug, Release and HostTests configure and build through CMake presets;
- all CTest checks pass;
- Boot, raw App, signed App, confirmed Primary and test Secondary artifacts are generated;
- signature, size, memory-map and ICG checks pass;
- MCUboot scratch-swap and App confirmation code are linked into the corresponding firmware;
- build instructions state that only signed/padded artifacts are programmable images;
- USB and transport protocol remain explicitly documented as later work;
- physical rollback and confirmation persistence have passed, with evidence retained outside the build tree.
