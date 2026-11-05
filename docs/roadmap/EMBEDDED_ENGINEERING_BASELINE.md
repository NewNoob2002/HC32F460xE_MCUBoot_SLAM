# Embedded Engineering Baseline Refactor

Status: **Approved on 2026-11-05; implementation in progress**

Baseline commit: `0e203b133a3d2b1fe56b15e81821caea1d4efabe`

This document records the W02 architecture review and approved W09 refactor plan
from the external `embedded-engineering` skill.

## Scope and profile

The production firmware is `mcu-baremetal`, resource tier T1:

- HC32F460xE, Cortex-M4F, bare-metal C11 project with C++17 Application services.
- 64 KiB Boot region, 200 KiB Primary and Secondary slots each, 8 KiB Scratch.
- Static ownership, a cooperative main loop and no RTOS in project code.

`scripts/detect_profile.py` suggests `mcu-rtos` with an RTOS score of 19, but all
evidence is under vendored CMSIS, MCUboot or FlashDB directories. Project code
contains no RTOS task creation, scheduler or RTOS include. The manual profile
decision therefore remains `mcu-baremetal`.

The repository's explicit `.clang-format` is authoritative: four-space indents,
120 columns, Attach braces and LF. This project rule overrides the skill's
default 80-column/Allman presentation. Braces, LF, tabs, warning gates,
architecture rules, safety rules and C++ feature limits still apply.

## Architecture assessment

The current top-level architecture is sound:

```text
Boot / App composition roots
  -> DataBroker and business nodes
     -> HAL service contracts
        -> HC32 BSP / MCUboot / FlashDB adapters

USB poll -> portable fw_update manager
  -> Secondary-only storage backend
  -> MCUboot boot control backend
```

The App is a static composition root. `App::runApplication()` owns startup order,
polling, image confirmation and reset requests. DataProc does not depend on
Diagnostics, and the fixed-capacity DataBroker remains deterministic. Boot uses
MCUboot for image verification, swap and rollback, then validates the Primary
image, MSP and Thumb reset vector before handover. The update core remains
portable and Secondary-only. These boundaries should stay in place.

The defects are at the boundaries around this sound design:

| Severity | Rule | Finding | Required evidence / remedy |
| --- | --- | --- | --- |
| High | ARCH-001 | `App/DataProc/Inc/DataProc.h:4-5` makes the business module depend on concrete `BQ40Z50.h` and `ChargerDevices.h`. | Inject a `PowerDevices` service contract; bind the HC32 device adapter in `App`. |
| High | ARCH-001 | `App/HAL/I2cBus.h:3` makes a port interface reverse-include `bsp_i2c2.h`, leaking BSP error codes into drivers, DataProc and Debug. | Define an HCI-owned bus status and have `Hc32I2cBus` map BSP errors at the adapter. |
| High | ARCH-001 | `App/DataProc/Src/dp_system.cpp:3` calls the external watchdog BSP directly from a business node. | Add an `ExternalWatchdog` contract and inject `Hc32ExternalWatchdog`. |
| High | ARCH-001 | `App/Core/Inc/usb_fw_update.h:9` couples the shared USB transport contract to `bsp_status_led.h`; the transport also chooses LED policy. | Return update activity/state from the transport; leave board LED policy to Boot/App composition. |
| Medium | ARCH-002 | `App/App.cpp:13` includes `cmsis_gcc.h` and calls `__get_IPSR()` in the composition root. | Add `bsp_in_isr()` to the BSP and use that from the App. |
| Medium | CPP-011 | `App/Framework/DataBroker/DataNode.h:66` gives a polymorphic base a public non-virtual destructor. | Make the destructor protected and non-virtual, destroy concrete nodes through their declared types and enable `-Wnon-virtual-dtor`. |
| Medium | CPP-015 | C++ warnings omit strict conversion, old-style-cast, polymorphic-destructor and overload checks. Existing C-style casts in `BQ40Z50.cpp` and the empty variadic debug call fail those checks. | Consolidate strict warning options, mark vendor includes `SYSTEM`, and fix the defects before making `-Werror` effective. |
| Medium | C-016 | Owned headers use `#pragma once`, including DataBroker, DataProc, HAL and HC32 BSP headers. | Replace with unique `#ifndef` / `#define` / `#endif` guards. |
| Low | BARR-C 4.1.b | C module stems collide in their first eight characters in several pairs, including `bsp_boot_info`/`bsp_boot_request`, `admin_auth`/`admin_auth_ecdsa`, `boot_control`/`boot_control_mcuboot` and `product_config`/`product_config_flashdb`. | Rename project-owned modules and update includes/CMake; exclude vendored code. |
| Low | C-018 | C targets do not pin a language standard. Owned sources already use C11 `_Noreturn` and `_Static_assert`, so C99 is not a valid mechanical target without a downgrade. | Declare C11 explicitly as the project exception, keep GNU extensions where the vendor toolchain requires them and document the choice. |

The machine architecture check currently reports seven violations:

```text
App/App.cpp:13                                  forbidden cmsis_gcc.h include
App/Core/Inc/usb_fw_update.h:9                  interface -> bsp
App/DataProc/Inc/DataProc.h:4                   module -> driver_device
App/DataProc/Inc/DataProc.h:5                   module -> driver_device
App/DataProc/Src/dp_power.cpp:1                  module -> driver_device
App/DataProc/Src/dp_system.cpp:3                 module -> bsp
App/HAL/I2cBus.h:3                               interface -> bsp
```

The custom layer configuration is based on the repository's existing uppercase
directory layout. The skill's default lowercase-directory config exits with a
configuration error and is not counted as a pass.

## Target contracts

The refactor will add narrow runtime interfaces because the App supports host
fakes and currently selects hardware at composition time:

```cpp
namespace hal {
enum Status : int; // preserves current observable error numbers

class I2cBus {
    // init, probe, write and readReg return hal::Status
};

class ExternalWatchdog {
    // init, poll, ready and pulse-active; protected non-virtual destructor
};

class PowerDevices {
    // probe/read Battery, Source and Charger telemetry;
    // apply input limit, charge voltage, charge current and suspend state
};
}
```

Concrete `Hc32*` adapters implement these contracts. `PowerNode` receives
`PowerDevices&`, and `SystemNode` receives `ExternalWatchdog&`; neither knows a
device address, BSP function or register. Battery/source/charger snapshots use
semantic names such as `BatteryTelemetry`, `SourceContract` and
`ChargerTelemetry`, so device-specific headers remain below the interface.

`usb_fw_update` exposes transport activity derived from manager state. Boot and
App map that activity to their own LED modes. This keeps recovery/update/error
priority behavior while removing LED control from the transport.

All interfaces are main-loop-only and caller-owned. They add no heap, exceptions,
RTTI or dynamic initialization. Existing public debug-link result numbers will
remain unchanged.

## Behavior baseline

The baseline before refactoring is:

| Check | Result | Evidence |
| --- | --- | --- |
| HostTests | PASS | Earlier clean configure/build followed by `ctest`: 29/29 passed. |
| Existing architecture CTests | PASS | Application/platform, portable update and USB device-port boundary checks passed. |
| Debug firmware | PASS | Build succeeded; the App and Boot images linked. |
| Debug image/USB/newlib verifiers | PASS | `verify_app_image`, `verify_winusb_descriptors` and `verify_newlib_syscalls` passed. |
| Release firmware | PASS | Build succeeded. |
| Release image/USB/newlib verifiers | PASS | Run on 2026-11-05; App image validation, descriptor check and newlib/runtime check passed. |
| C++ resource gate | PASS with warnings | `cpp_cost.py check ... --tier T1` passed; Debug-only heap/stdio warnings remain. |
| Profile detector | REVIEWED | Heuristic result conflicts with vendored-only RTOS evidence; manual bare-metal classification is authoritative. |
| Skill architecture lint | FAIL | Seven violations listed above, exit code 1. |
| BARR-C C module gate | FAIL | Project-owned C module stems collide in their first eight characters. |
| Strict C++ warning gate | FAIL | Public polymorphic destructor, old-style casts and empty variadic macro call were reproduced with the stricter trial flags. |
| HIL / hardware behavior | NOT_RUN | No flashing or hardware action is part of this refactor. |

Recorded build sizes at baseline:

| Image | Configuration | Flash used | Limit | RAM used |
| --- | --- | ---: | ---: | ---: |
| App | Debug | 71,888 B | 195,072 B | 16,276 B |
| Boot | Debug with administrator key | 61,272 B | 65,536 B | 8,704 B |
| App | Release | 52,972 B | 195,072 B | 14,724 B |
| Boot | Release without administrator key | 44,648 B | 65,536 B | 8,704 B |

## Refactor plan

Every checkpoint is a separate commit based on the previous approved commit.
Each must build and pass its focused host tests before the next starts.

1. **Freeze behavior with characterization tests.**
   Add host tests for power device call ordering and failure propagation,
   watchdog lifecycle injection, update LED/activity mapping and unchanged
   public result numbers. Commit tests before implementation so they satisfy
   the skill's independent-oracle requirement.
2. **Apply interface hygiene.**
   Replace owned `#pragma once` with guards, make the `DataNode` destructor
   protected, replace C++ C-style casts, fix the empty debug macro call, add
   `bsp_in_isr()` and rename colliding project-owned C modules.
3. **Introduce the status and I2C boundary.**
   Add `hal::Status`, remove the BSP include from `I2cBus.h`, map BSP results in
   `Hc32I2cBus`, and remove BSP status names from DataProc and Debug.
4. **Introduce `PowerDevices`.**
   Add semantic telemetry types, the `PowerDevices` contract and the HC32
   adapter. Change `PowerNode` to use only that contract. Preserve the existing
   charger sequencing, alarm stops and input-limit failure behavior.
5. **Inject the external watchdog.**
   Add `ExternalWatchdog`, bind it in the composition root and replace the
   link-time BSP fake with a host fake. Preserve feed intervals and readiness
   telemetry.
6. **Move USB LED policy upward.**
   Remove LED types and LED writes from `usb_fw_update`; expose activity from
   manager state and apply LED mode/override in Boot and App composition loops.
   Exercise this shared C source from both Boot and App.
7. **Make quality gates executable.**
   Consolidate C/C++ warning interfaces, mark vendor headers `SYSTEM`, set the
   C11 project dialect, extend the checked-in architecture test and add a
   project-specific `.clang-tidy` that excludes known vendor/C++ naming false
   positives.
8. **Run full acceptance checks.**
   Run HostTests, Debug full build and three verify targets, Release build and
   three verify targets, architecture tests, skill architecture lint, BARR-C on
   its documented project-owned C scope, formatting, `cpp_cost.py` and retained
   evidence verification. Record HIL as NOT_RUN unless separately authorized.

## Validation and rollback

Each implementation checkpoint must report changed files, public APIs, actual
commands and results. All checkpoint commits must keep these properties:

- Flash layout, image signing, image validation, rollback and Secondary-only
  writes are unchanged.
- Power control order remains: disable/suspend before risky writes, apply limits
  before enabling, read back charger configuration, and stop charging on any
  failed required write.
- Watchdog scheduling and USB polling cadence are unchanged by the indirection.
- Boot remains within its 64 KiB region. Stop and return to architecture review
  if any checkpoint adds more than 512 B Boot Flash, 1 KiB App Flash or 64 B RAM.
- No hardware result is claimed without retained HIL evidence.

Rollback is by `git revert <checkpoint-commit>` in reverse order. No reset,
restore, clean or stash is used, so unrelated work and evidence remain intact.

## Decision

Current architecture conformance: **FAIL** because mandatory ARCH and CPP/C
rules are violated.

Proposed target architecture: **Proceed**, with no platform or OS change and no
change to update, boot or recovery security boundaries.

The user approved this concrete target design and the eight-checkpoint
implementation sequence, including the App composition, DataProc and USB
interfaces described above.

HIL remains a separate hardware authorization. The plan does not flash, reset or
modify target hardware.
