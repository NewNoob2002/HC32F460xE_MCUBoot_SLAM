# Minimal MCUboot Boot + App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Produce buildable HC32F460xE Boot and App firmware, ECDSA-P256 signed MCUboot images, and scratch-swap confirmation/revert support with layered CMake targets.

**Architecture:** Root CMake orchestrates target-scoped Driver, Platform, MCUboot port, Boot and App layers. MCUboot owns validation and scratch swap; the HC32 port owns Flash mapping and handover; App uses MCUboot's public confirmation API. Boot and App compile separate startup objects because vendor SystemInit() sets VTOR from VECT_TAB_OFFSET.

**Tech Stack:** C11, GNU Arm Embedded GCC 14.3.1, CMake 3.28, Ninja 1.11, MCUboot 2.4.0, TinyCrypt, Python 3.12 and MCUboot imgtool.

**Spec:** docs/superpowers/specs/2026-08-25-mcuboot-minimal-boot-app-design.md

## Global Constraints

- Prefix every shell command with rtk.
- Keep Boot at 0x00000000/64 KiB, Primary at 0x00010000/200 KiB, Secondary at 0x00042000/200 KiB, Scratch at 0x00074000/8 KiB and Reserved at 0x00076000/40 KiB.
- Use an 0x200 image header, 0x400 TLV allowance, one 0x2000 trailer sector and App linked length 0x2FA00.
- Enable scratch swap, ECDSA-P256, TinyCrypt, primary validation, one image and 25 sectors per slot.
- Do not modify components/mcuboot-2.4.0.
- Do not use global include_directories(), add_definitions() or link_directories().
- Do not create USB, protocol, transport, anti-rollback or automatic flashing code.
- Do not commit a private key. Debug keys live under build/; Release requires an explicit key path.
- Preserve Boot-only ICG at 0x00000400 with size 0x20.
- Compile Boot startup with VECT_TAB_OFFSET=0 and App startup with VECT_TAB_OFFSET=0x00010200.
- Each task ends with its focused check and commit.

## Planned File Responsibilities

- CMakeLists.txt: project entry and subdirectory order.
- CMakePresets.json: Debug, Release and HostTests presets.
- cmake/MemoryMap.cmake: canonical partition/image values.
- cmake/ProjectOptions.cmake: target-scoped CPU, warning and optimization flags.
- cmake/FirmwareArtifacts.cmake: map, HEX, BIN and signed-image commands.
- cmake/Imgtool.cmake: imgtool detection, key/public-source generation and verification.
- Config/boot_memory_map.h.in: generated C constants.
- Drivers/CMakeLists.txt: hc32_device, hc32_ll and per-image startup objects.
- Platform/HC32F460/: retained BSP plus handover code.
- components/CMakeLists.txt: tinycrypt and mcuboot_bootutil.
- components/mcuboot_port/: MCUboot configuration, keys and Flash backend.
- Boot/Core/ and App/Core/: image-specific entry points.
- Linker/: Boot and App scripts.
- Tests/: host layout, Flash, handover and confirmation checks.
- README.md: build/signing instructions and manual HIL procedure.

---

### Task 0: Capture the Reinitialized Repository Baseline

**Files:**
- Add existing: .clang-format, .clangd, .gitignore, CMakeLists.txt, CMakePresets.json, Config/, Core/, Drivers/, HC32F460xE.ld, Libraries/, Platform/, cmake/, components/, docs/
- Exclude: .serena/, .vscode/, .codegraph/, build/, debug_artifacts/ and generated binaries

**Interfaces:**
- Consumes: root commit 1ee825b.
- Produces: tracked baseline for reviewable moves and diffs.

- [ ] **Step 1: Review untracked files**

~~~sh
rtk git status --short
rtk git check-ignore -v .serena .vscode .codegraph build 2>/dev/null || true
~~~

Expected: project sources are untracked; local state is not staged.

- [ ] **Step 2: Stage only project files**

~~~sh
rtk git add .clang-format .clangd .gitignore CMakeLists.txt CMakePresets.json Config Core Drivers HC32F460xE.ld Libraries Platform cmake components docs
rtk git status --short
~~~

Expected: no local-state directory is staged.

- [ ] **Step 3: Commit**

~~~sh
rtk git diff --cached --check
rtk git commit -m "chore: import HC32 firmware baseline"
~~~

---

### Task 1: Centralize and Test the Flash Memory Map

**Files:**
- Modify: CMakeLists.txt
- Modify: CMakePresets.json
- Create: cmake/MemoryMap.cmake
- Create: Config/boot_memory_map.h.in
- Create: Tests/CMakeLists.txt
- Create: Tests/memory_map_tests.c

**Interfaces:**
- Consumes: no production code.
- Produces: generated/Config/boot_memory_map.h with all partition and image constants.

- [ ] **Step 1: Add the failing test**

~~~c
#include <assert.h>
#include "boot_memory_map.h"
int main(void) {
    assert(BOOT_FLASH_BASE == 0x00000000UL);
    assert(BOOT_FLASH_SIZE == 0x00010000UL);
    assert(PRIMARY_SLOT_BASE == BOOT_FLASH_BASE + BOOT_FLASH_SIZE);
    assert(SECONDARY_SLOT_BASE == PRIMARY_SLOT_BASE + PRIMARY_SLOT_SIZE);
    assert(SCRATCH_BASE == SECONDARY_SLOT_BASE + SECONDARY_SLOT_SIZE);
    assert(RESERVED_BASE == SCRATCH_BASE + SCRATCH_SIZE);
    assert(RESERVED_BASE + RESERVED_SIZE == FLASH_TOTAL_SIZE);
    assert(APP_LINK_ORIGIN == PRIMARY_SLOT_BASE + MCUBOOT_HEADER_SIZE);
    assert(APP_LINK_SIZE == PRIMARY_SLOT_SIZE - MCUBOOT_HEADER_SIZE
                            - MCUBOOT_TLV_RESERVE - MCUBOOT_TRAILER_RESERVE);
    return 0;
}
~~~

Create HostTests preset and target without the generated header.

- [ ] **Step 2: Confirm red**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --target memory_map_tests
~~~

Expected: boot_memory_map.h is missing.

- [ ] **Step 3: Implement MemoryMap.cmake**

~~~cmake
set(FLASH_TOTAL_SIZE         0x00080000)
set(FLASH_SECTOR_SIZE        0x00002000)
set(FLASH_WRITE_ALIGN        4)
set(BOOT_FLASH_BASE          0x00000000)
set(BOOT_FLASH_SIZE          0x00010000)
set(PRIMARY_SLOT_BASE        0x00010000)
set(PRIMARY_SLOT_SIZE        0x00032000)
set(SECONDARY_SLOT_BASE      0x00042000)
set(SECONDARY_SLOT_SIZE      0x00032000)
set(SCRATCH_BASE             0x00074000)
set(SCRATCH_SIZE             0x00002000)
set(RESERVED_BASE            0x00076000)
set(RESERVED_SIZE            0x0000A000)
set(MCUBOOT_HEADER_SIZE      0x00000200)
set(MCUBOOT_TLV_RESERVE      0x00000400)
set(MCUBOOT_TRAILER_RESERVE  0x00002000)
set(APP_LINK_ORIGIN          0x00010200)
set(APP_LINK_SIZE            0x0002FA00)
set(MCUBOOT_MAX_IMG_SECTORS  25)
math(EXPR layout_end "0x00076000 + 0x0000A000")
if(NOT layout_end EQUAL FLASH_TOTAL_SIZE)
    message(FATAL_ERROR "Flash layout does not consume exactly 512 KiB")
endif()
~~~

Add explicit contiguity and 8 KiB alignment checks, generate the header under each preset's binary directory at generated/Config and expose it only through targets.

Root CMake loads MemoryMap.cmake before selecting the build path and uses this exact host/firmware split:

~~~cmake
if(BOOT_BUILD_HOST_TESTS)
    enable_testing()
    add_subdirectory(Tests)
    return()
endif()
add_subdirectory(Drivers)
add_subdirectory(Platform)
add_subdirectory(components)
add_subdirectory(Boot)
add_subdirectory(App)
~~~

- [ ] **Step 4: Run the test**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --target memory_map_tests
rtk ctest --test-dir build/HostTests -R memory_map --output-on-failure
~~~

Expected: pass.

- [ ] **Step 5: Commit**

~~~sh
rtk git add CMakeLists.txt CMakePresets.json cmake/MemoryMap.cmake Config/boot_memory_map.h.in Tests
rtk git diff --cached --check
rtk git commit -m "build: centralize MCUboot flash layout"
~~~

---

### Task 2: Build Layered HC32 Boot and App Skeletons

**Files:**
- Modify: CMakeLists.txt
- Modify: cmake/ProjectOptions.cmake
- Create: cmake/FirmwareArtifacts.cmake
- Create: Drivers/CMakeLists.txt
- Move: Platform/BSP/ to Platform/HC32F460/
- Create: Platform/CMakeLists.txt
- Move: Core/ to Boot/Core/
- Replace: Boot/Core/Inc/main.h and Boot/Core/Src/main.c
- Create: Boot/CMakeLists.txt
- Create: App/CMakeLists.txt, App/Core/Inc/main.h and App/Core/Src/main.c
- Create: Linker/boot.ld and Linker/app.ld

**Interfaces:**
- Consumes: generated memory header and vendor CMSIS/LL sources.
- Produces: Boot/App ELF/MAP/HEX/BIN skeletons.

- [ ] **Step 1: Reference missing layered targets and confirm red**

~~~sh
rtk cmake --preset Debug
~~~

Expected: missing hc32_startup_boot, hc32_startup_app or hc32_platform.

- [ ] **Step 2: Implement target-scoped options**

~~~cmake
target_compile_options(hc32_project_options INTERFACE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -ffunction-sections -fdata-sections -Wall -Wextra -Werror=return-type
    $<$<CONFIG:Debug>:-Og -g3> $<$<CONFIG:Release>:-Os -g0>)
target_link_options(hc32_project_options INTERFACE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs)
~~~

- [ ] **Step 3: Implement Driver/startup targets**

Create hc32_device INTERFACE; hc32_ll STATIC from hc32_ll.c, clk, efm, fcg, gpio, pwc, sram and utility sources; hc32_startup_boot OBJECT from startup_hc32f460.S, system_hc32f460.c and hc32_ll_icg.c with VECT_TAB_OFFSET=0; hc32_startup_app OBJECT from startup/system sources with VECT_TAB_OFFSET=0x00010200. Do not place hc32_ll_icg.c in hc32_ll.

- [ ] **Step 4: Implement hc32_platform**

Move the BSP directory intact. Initially compile bsp_clock.c and bsp_flash.c and publish Platform/HC32F460/Inc.

- [ ] **Step 5: Implement linker scripts**

Boot script keeps normal sections plus:

~~~ld
FLASH_ORIGIN = DEFINED(FLASH_ORIGIN) ? FLASH_ORIGIN : 0x00000000;
FLASH_LENGTH = DEFINED(FLASH_LENGTH) ? FLASH_LENGTH : 0x00010000;
ASSERT(ADDR(.icg_sec) == 0x00000400, "ICG must start at 0x400")
ASSERT(SIZEOF(.icg_sec) == 0x20, "ICG must contain eight words")
~~~

App script removes OTP/ICG sections and uses:

~~~ld
FLASH_ORIGIN = DEFINED(FLASH_ORIGIN) ? FLASH_ORIGIN : 0x00010200;
FLASH_LENGTH = DEFINED(FLASH_LENGTH) ? FLASH_LENGTH : 0x0002FA00;
ASSERT(FLASH_ORIGIN == 0x00010200, "App vector must follow header")
~~~

- [ ] **Step 6: Add minimal mains and raw artifacts**

Boot initializes the clock and loops; App loops. Add map plus objcopy HEX/BIN commands. Do not sign yet.

- [ ] **Step 7: Build and inspect**

~~~sh
rtk cmake --preset Debug
rtk cmake --build build/Debug --clean-first --parallel
rtk arm-none-eabi-size build/Debug/Boot/boot_firmware.elf build/Debug/App/app_firmware.elf
rtk arm-none-eabi-objdump -h build/Debug/Boot/boot_firmware.elf
rtk arm-none-eabi-objdump -h build/Debug/App/app_firmware.elf
~~~

Expected: Boot ICG=0x400/0x20; App vectors=0x10200 and no ICG.

- [ ] **Step 8: Commit**

~~~sh
rtk git add CMakeLists.txt cmake Drivers/CMakeLists.txt Platform Boot App Linker
rtk git diff --cached --check
rtk git commit -m "build: add layered HC32 boot and app targets"
~~~

---

### Task 3: Implement and Host-Test the MCUboot Flash Map

**Files:**
- Create: components/CMakeLists.txt
- Create: components/mcuboot_port/CMakeLists.txt
- Create: components/mcuboot_port/Inc/flash_map_backend/flash_map_backend.h
- Create: components/mcuboot_port/Src/flash_map_backend.c
- Create: Tests/flash_map_tests.c
- Modify: Tests/CMakeLists.txt

**Interfaces:**
- Consumes: bsp_flash_read/write/erase_sector.
- Produces: MCUboot flash_area APIs and slot-ID mapping.

- [ ] **Step 1: Write failing mock-backed tests**

~~~c
assert(flash_area_open(FLASH_AREA_IMAGE_PRIMARY(0), &area) == 0);
assert(area->fa_off == PRIMARY_SLOT_BASE);
assert(flash_area_write(area, 0, word, sizeof word) == 0);
assert(mock_address == PRIMARY_SLOT_BASE);
assert(flash_area_write(area, 1, word, sizeof word) != 0);
assert(flash_area_write(area, area->fa_size, word, sizeof word) != 0);
assert(flash_area_erase(area, 0, FLASH_SECTOR_SIZE) == 0);
assert(mock_sector == PRIMARY_SLOT_BASE / FLASH_SECTOR_SIZE);
assert(flash_area_erase(area, 4, FLASH_SECTOR_SIZE) != 0);
assert(flash_area_open(FLASH_AREA_BOOTLOADER, &area) != 0);
~~~

- [ ] **Step 2: Confirm red**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --target flash_map_tests
~~~

- [ ] **Step 3: Implement descriptors and bounds**

~~~c
static const struct flash_area areas[] = {
    { FLASH_AREA_ID_PRIMARY,   0U, PRIMARY_SLOT_BASE,   PRIMARY_SLOT_SIZE },
    { FLASH_AREA_ID_SECONDARY, 0U, SECONDARY_SLOT_BASE, SECONDARY_SLOT_SIZE },
    { FLASH_AREA_ID_SCRATCH,   0U, SCRATCH_BASE,        SCRATCH_SIZE },
};
~~~

Use len > 0 and off <= fa_size - len. Writes require offset/length divisible by 4; erases require offset/length divisible by 0x2000. Boot is absent. flash_area_get_sectors returns 25/25/1 relative sectors; align=4; erased=0xff.

- [ ] **Step 4: Run tests and commit**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --clean-first --parallel
rtk ctest --test-dir build/HostTests --output-on-failure
rtk git add components/CMakeLists.txt components/mcuboot_port Tests
rtk git diff --cached --check
rtk git commit -m "feat: port MCUboot flash map to HC32"
~~~

---

### Task 4: Build MCUboot, TinyCrypt and Signing-Key Sources

**Files:**
- Modify: .gitignore
- Create: cmake/Imgtool.cmake
- Modify: components/CMakeLists.txt and components/mcuboot_port/CMakeLists.txt
- Create: components/mcuboot_port/Inc/mcuboot_config/mcuboot_config.h
- Create: components/mcuboot_port/Inc/mcuboot_config/mcuboot_logging.h
- Create: components/mcuboot_port/Inc/mcuboot_config/mcuboot_assert.h
- Create: components/mcuboot_port/Src/keys.c and config_checks.c

**Interfaces:**
- Consumes: MCUboot, TinyCrypt, imgtool and Flash backend.
- Produces: tinycrypt, mcuboot_asn1, mcuboot_bootutil and generated ECDSA public source.

- [ ] **Step 1: Prepare imgtool**

~~~sh
rtk python3 -m venv .venv
rtk .venv/bin/pip install -r components/mcuboot-2.4.0/scripts/requirements.txt
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py version
~~~

Add .venv/ to .gitignore. Network approval is required for pip.

- [ ] **Step 2: Add exact MCUboot configuration**

~~~c
#pragma once
#define MCUBOOT_IMAGE_NUMBER 1
#define MCUBOOT_MAX_IMG_SECTORS 25
#define MCUBOOT_SWAP_USING_SCRATCH 1
#define MCUBOOT_SIGN_EC256 1
#define MCUBOOT_USE_TINYCRYPT 1
#define MCUBOOT_VALIDATE_PRIMARY_SLOT 1
#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS 1
#define MCUBOOT_BOOT_MAX_ALIGN 4
#define MCUBOOT_FIH_PROFILE_OFF 1
#define MCUBOOT_HAVE_ASSERT_H 1
~~~

Logging macros are no-ops; assert header includes assert.h.

- [ ] **Step 3: Add library source lists**

TinyCrypt: sha256.c, utils.c, ecc.c, ecc_dsa.c, ecc_platform_specific.c.

mcuboot_asn1: ext/mbedtls-asn1/src/asn1parse.c and platform_util.c, exposing ext/mbedtls-asn1/include. This is the DER parser required by MCUboot's TinyCrypt ECDSA path, not the Mbed TLS cryptography backend.

MCUboot: bootutil_area.c, bootutil_find_key.c, bootutil_img_hash.c, bootutil_img_security_cnt.c, bootutil_loader.c, bootutil_misc.c, bootutil_public.c, caps.c, fault_injection_hardening.c, image_ecdsa.c, image_validate.c, loader.c, swap_misc.c, swap_scratch.c and tlv.c.

- [ ] **Step 4: Implement key plumbing**

Debug without a key runs:

~~~sh
imgtool.py keygen -t ecdsa-p256 -k build/Debug/generated/keys/dev-ec-p256.pem
imgtool.py getpub -k build/Debug/generated/keys/dev-ec-p256.pem -l c -o build/Debug/generated/keys/signing_keys.c
~~~

Release fails configure without MCUBOOT_SIGNING_KEY. keys.c wraps generated ecdsa_pub_key/length in bootutil_keys[]. config_checks.c asserts image count=1, max sectors=25 and align=4.

- [ ] **Step 5: Build and commit**

~~~sh
rtk cmake --preset Debug
rtk cmake --build build/Debug --target mcuboot_bootutil --parallel
rtk git add .gitignore cmake/Imgtool.cmake components
rtk git diff --cached --check
rtk git commit -m "build: integrate MCUboot and TinyCrypt"
~~~

---

### Task 5: Implement Validated Boot and HC32 Handover

**Files:**
- Replace: Boot/Core/Src/main.c
- Create: Platform/HC32F460/Inc/boot_handover.h
- Create: Platform/HC32F460/Src/boot_handover.c
- Modify: Platform/CMakeLists.txt
- Modify: Boot/CMakeLists.txt
- Create: Tests/boot_handover_tests.c
- Modify: Tests/CMakeLists.txt

**Interfaces:**
- Consumes: fih_ret boot_go(struct boot_rsp *rsp).
- Produces: uint32_t boot_handover_vector_address(uint32_t image_offset, uint16_t header_size) and noreturn boot_handover(const struct boot_rsp *rsp).

- [ ] **Step 1: Write the failing helper test**

~~~c
#include <assert.h>
#include "boot_handover.h"
int main(void) {
    assert(boot_handover_vector_address(0x10000UL, 0x200U) == 0x10200UL);
    assert(boot_handover_vector_address(0x10000UL, 0U) == 0x10000UL);
    return 0;
}
~~~

- [ ] **Step 2: Confirm red**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --target boot_handover_tests
~~~

Expected: boot_handover_vector_address is undefined.

- [ ] **Step 3: Implement the pure address helper and target handover**

~~~c
uint32_t boot_handover_vector_address(uint32_t image_offset, uint16_t header_size) {
    return image_offset + (uint32_t)header_size;
}
~~~

The target path validates MSP in 0x1FFF8000..0x20027FFF and the reset vector, with bit zero cleared, in 0x00010200..0x0003FBFF. It disables IRQs and SysTick, clears every implemented NVIC enable/pending word, sets SCB->VTOR, executes DSB/ISB, sets MSP and calls the reset handler. Guard CMSIS-only code with BOOT_HOST_TEST so the helper remains host-buildable.

- [ ] **Step 4: Replace Boot main with MCUboot flow**

~~~c
int main(void) {
    struct boot_rsp rsp = {0};
    FIH_DECLARE(rc, FIH_FAILURE);
    bsp_clock_init();
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS)) {
        for (;;) { __WFI(); }
    }
    boot_handover(&rsp);
}
~~~

Do not add a raw-vector fallback.

- [ ] **Step 5: Test and build Boot**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --parallel
rtk ctest --test-dir build/HostTests --output-on-failure
rtk cmake --preset Debug
rtk cmake --build build/Debug --target boot_firmware --clean-first --parallel
rtk arm-none-eabi-size build/Debug/Boot/boot_firmware.elf
~~~

Expected: host tests pass and Boot remains at most 65536 bytes.

- [ ] **Step 6: Commit**

~~~sh
rtk git add Boot Platform Tests
rtk git diff --cached --check
rtk git commit -m "feat: boot validated MCUboot image on HC32"
~~~

---

### Task 6: Implement the Simple App and Confirmation Policy

**Files:**
- Replace: App/Core/Src/main.c
- Create: App/Core/Inc/app_confirm.h
- Create: App/Core/Src/app_confirm.c
- Modify: App/CMakeLists.txt
- Create: Tests/app_confirm_tests.c
- Modify: Tests/CMakeLists.txt

**Interfaces:**
- Consumes: int boot_set_confirmed(void).
- Produces: int app_confirm_running_image(bool auto_confirm).

- [ ] **Step 1: Write the failing test**

~~~c
#include <assert.h>
#include "app_confirm.h"
static int calls;
static int result;
int boot_set_confirmed(void) { ++calls; return result; }
int main(void) {
    calls = 0;
    assert(app_confirm_running_image(false) == 0);
    assert(calls == 0);
    result = -7;
    assert(app_confirm_running_image(true) == -7);
    assert(calls == 1);
    return 0;
}
~~~

- [ ] **Step 2: Confirm red**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --target app_confirm_tests
~~~

Expected: app_confirm_running_image is undefined.

- [ ] **Step 3: Implement confirmation**

~~~c
int app_confirm_running_image(bool auto_confirm) {
    return auto_confirm ? boot_set_confirmed() : 0;
}
~~~

App/CMakeLists exposes APP_AUTO_CONFIRM as ON/OFF and compiles it to 1/0. App main initializes the clock, calls the wrapper, stores the result in a volatile debugger-visible variable and loops. Do not add LED or Key dependencies.

- [ ] **Step 4: Test and build both modes**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --parallel
rtk ctest --test-dir build/HostTests --output-on-failure
rtk cmake --preset Debug -DAPP_AUTO_CONFIRM=ON
rtk cmake --build build/Debug --target app_firmware --clean-first --parallel
rtk cmake --preset Debug -DAPP_AUTO_CONFIRM=OFF
rtk cmake --build build/Debug --target app_firmware --clean-first --parallel
~~~

Expected: both branches are tested and both firmware variants link at 0x10200.

- [ ] **Step 5: Commit**

~~~sh
rtk git add App Tests
rtk git diff --cached --check
rtk git commit -m "feat: confirm running MCUboot application"
~~~

---

### Task 7: Generate and Verify Signed Images

**Files:**
- Modify: cmake/FirmwareArtifacts.cmake
- Modify: App/CMakeLists.txt
- Modify: CMakePresets.json

**Interfaces:**
- Consumes: app_firmware.bin, APP_VERSION and the selected ECDSA key.
- Produces: app_signed.bin, app_primary.bin, app_update.bin and verify_app_image.

- [ ] **Step 1: Declare signing targets without commands**

Declare app_signed, app_primary, app_update and verify_app_image dependencies before adding the custom commands.

- [ ] **Step 2: Confirm red**

~~~sh
rtk cmake --preset Debug
rtk cmake --build build/Debug --target app_update
~~~

Expected: app_update has no producing command.

- [ ] **Step 3: Implement exact signing commands**

All images use:

~~~text
sign --align 4 --max-sectors 25 --version 1.0.0
     --header-size 0x200 --slot-size 0x32000 --pad-header
     --erased-val 0xff --key build/Debug/generated/keys/dev-ec-p256.pem
~~~

The implementation substitutes the configured APP_VERSION and key path, then generates:

- app_signed.bin with no --pad flag;
- app_primary.bin with --pad --confirm;
- app_update.bin with --pad --test.

Each command depends on app_firmware.bin, the selected key and imgtool.py.

- [ ] **Step 4: Add verification**

~~~sh
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py verify -k build/Debug/generated/keys/dev-ec-p256.pem build/Debug/artifacts/app_signed.bin
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py verify -k build/Debug/generated/keys/dev-ec-p256.pem build/Debug/artifacts/app_primary.bin
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py verify -k build/Debug/generated/keys/dev-ec-p256.pem build/Debug/artifacts/app_update.bin
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py dumpinfo build/Debug/artifacts/app_primary.bin
rtk .venv/bin/python components/mcuboot-2.4.0/scripts/imgtool.py dumpinfo build/Debug/artifacts/app_update.bin
~~~

verify_app_image also fails if either padded file is not exactly 204800 bytes.

- [ ] **Step 5: Build Debug images**

~~~sh
rtk cmake --preset Debug -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
rtk cmake --build build/Debug --target verify_app_image --clean-first --parallel
rtk ls -lh build/Debug/artifacts
~~~

Expected: raw, signed, confirmed-primary and test-secondary images exist.

- [ ] **Step 6: Commit**

~~~sh
rtk git add cmake/FirmwareArtifacts.cmake App/CMakeLists.txt CMakePresets.json
rtk git diff --cached --check
rtk git commit -m "build: generate signed MCUboot images"
~~~

---

### Task 8: Complete Clean Verification and Documentation

**Files:**
- Create: README.md
- Replace stale content: docs/build_report.md
- Modify: approved design only for factual corrections exposed by verification

**Interfaces:**
- Consumes: all targets and artifacts.
- Produces: reproducible instructions, evidence and manual HIL steps.

- [ ] **Step 1: Document prerequisites and safe artifacts**

README states how to create .venv; only boot_firmware.bin, app_primary.bin and app_update.bin are directly programmable; app_firmware.bin is not an MCUboot image; Debug keys are ephemeral; Release requires MCUBOOT_SIGNING_KEY; software builds do not prove hardware rollback.

- [ ] **Step 2: Run clean HostTests**

~~~sh
rtk cmake --preset HostTests
rtk cmake --build build/HostTests --clean-first --parallel
rtk ctest --test-dir build/HostTests --output-on-failure
~~~

Expected: memory_map, flash_map, boot_handover and app_confirm pass.

- [ ] **Step 3: Run clean Debug**

~~~sh
rtk cmake --preset Debug -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
rtk cmake --build build/Debug --clean-first --parallel
rtk cmake --build build/Debug --target verify_app_image
rtk arm-none-eabi-size build/Debug/Boot/boot_firmware.elf build/Debug/App/app_firmware.elf
~~~

- [ ] **Step 4: Run clean Release with an explicit test key**

~~~sh
rtk cmake --preset Release -DMCUBOOT_SIGNING_KEY=$PWD/build/Debug/generated/keys/dev-ec-p256.pem -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
rtk cmake --build build/Release --clean-first --parallel
rtk cmake --build build/Release --target verify_app_image
~~~

Expected: Release does not generate a key.

- [ ] **Step 5: Verify placement, sizes and hashes**

~~~sh
rtk arm-none-eabi-objdump -h build/Debug/Boot/boot_firmware.elf
rtk arm-none-eabi-objdump -h build/Debug/App/app_firmware.elf
rtk stat -c '%n %s' build/Debug/artifacts/app_primary.bin build/Debug/artifacts/app_update.bin
rtk sha256sum build/Debug/artifacts/*
~~~

Expected: Boot ICG=0x400/0x20, App vectors=0x10200, App has no ICG, padded images=204800 bytes and Boot<=65536 bytes.

- [ ] **Step 6: Record evidence and manual HIL sequence**

Record tool versions, presets, ELF sizes, section addresses, image versions and hashes. Include v1 confirmed -> v2 test -> unconfirmed reset/revert -> confirmed v2 persistence. Mark physical execution pending until run.

- [ ] **Step 7: Commit documentation**

~~~sh
rtk git add README.md docs
rtk git diff --cached --check
rtk git commit -m "docs: add MCUboot build and rollback procedure"
rtk git status --short
~~~

Expected: no tracked modification remains.
