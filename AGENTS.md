@/home/gtc/.codex/RTK.md

# HC32F460xE MCUboot project guide

## Scope and current state

- Bare-metal HC32F460xE Boot + App firmware built with CMake/Ninja and GNU Arm Embedded.
- MCUboot 2.4.0 uses one image, ECDSA-P256 verification, Primary/Secondary slots, scratch swap, rollback, and application confirmation.
- `components/fw_update/` contains the portable protocol/manager plus MCUboot storage and boot-control backends. The single `app_firmware` target wires it to the production Application USB poll loop.
- `Tools/updater/` contains the shared Rust CLI/Slint GUI, blocking nusb adapter, Product Config v3 and SN-bound reconnect workflow. Linux and HC32 HIL pass; signed Windows packaging, UART/CAN and G5 closure remain pending.
- `Tools/host/usb_loopback.py` remains a host-side Phase 4 regression utility; no loopback firmware target is built.

## Navigate before editing

- This repository is indexed by CodeGraph (`.codegraph/`). Use `codegraph explore "<question or symbols>"` before `rg`, `find`, or broad file reads when locating code or tracing behavior.
- Treat code and CMake as the source of truth. Use `docs/roadmap/CURRENT_STATE.md` for the current architectural summary; completed history remains in Git, ADRs and retained evidence.
- Trace callers before changing shared Boot, Flash, handover, protocol, manager, or storage functions.

## Ownership boundaries

- Project code: `Boot/`, `App/`, `Platform/HC32F460/`, `components/fw_update/`, `Tests/`, `cmake/`, `Config/`, `Linker/`, and `Tools/`.
- Vendored code: `components/mcuboot-2.4.0/`, `components/FlashDB-2.2.0/`, `components/cherryusb/`, most of `Drivers/`, and third-party `Libraries/`. Avoid edits there unless the task explicitly requires a vendor change.
- Keep `components/` limited to vendored or portable component code. MCU-specific ports belong under `Platform/<MCU>/Ports/<component>/`.
- Generated/output files: `build/`, generated `boot_memory_map.h`, generated signing-key sources, ELF/HEX/BIN/map files, and signed artifacts. Change their inputs, never the generated files.
- Preserve unrelated working-tree changes, especially roadmap and evidence documents.

## Architecture

- Boot entry: `Boot/Core/Src/main.c` -> `bsp_clock_init()` -> `boot_go()` -> `boot_handover()`.
- Handover: `Platform/HC32F460/Src/boot_handover.c` accepts only internal Primary with the fixed header, validates MSP/Thumb reset vectors, clears interrupt/SysTick state, then uses a small assembly trampoline to restore CONTROL/MSP and branch.
- Application entry: `App/Core/Src/main.c` sequences startup and polling; `App/Devices/` owns device contracts, `App/Services/` owns I2C device composition, and `App/Diagnostics/` owns bounded runtime status. USB callbacks publish events and the cooperative poll loop owns Manager, Flash and deferred reset work.
- Active-low RGB status LED: PA0=red, PA1=green, PA2=blue. Boot is solid blue; recovery is slow red; firmware update is fast blue; default App is green heartbeat; updater App is blue heartbeat; USB diagnostic App is cyan heartbeat; panic/error is solid red.
- Host updater: `Tools/updater/`; CLI and Slint GUI share `FirmwareImage`, `ProtocolV1Client` and `UpgradeWorkflow`.
- MCUboot port: `Platform/HC32F460/Ports/mcuboot/` owns Flash areas, MCUboot configuration, assertions/logging, and the generated public-key bridge.
- Update core: `components/fw_update/src/` must remain host-buildable and platform-independent. Hardware/MCUboot access belongs in `components/fw_update/backends/`.

## Critical invariants

- `cmake/MemoryMap.cmake` is the canonical Flash layout. Keep CMake generation, linker scripts, Flash maps, tests, signing arguments, and documentation consistent. Do not duplicate derived MCUboot trailer offsets; use MCUboot APIs/helpers.
- Layout: Boot `0x00000000/0x10000`, Primary `0x00010000/0x32000`, Secondary `0x00042000/0x32000`, Scratch `0x00074000/0x2000`, Reserved `0x00076000/0xA000`. App links at `0x00010200`.
- Firmware-update writes are Secondary-only, aligned, bounds-checked, and exclude the reserved trailer sector. Never accept host-provided physical Flash addresses.
- Keep the portable updater bounded and caller-allocated. Do not add heap, RTOS, transport registries, or new dependencies without a demonstrated need.
- Do not weaken image validation, ECDSA-P256 key checks, rollback behavior, handover validation, or trust-boundary input validation.
- Firmware builds require an explicitly supplied external private key. Never commit private keys or generate them under `build/`; the Debug preset uses the fixed development key outside the repository.
- Immediate App confirmation is the current baseline, not an ideal health policy. If changing confirmation timing, confirm only after explicit bounded health checks.

## Build and verification

Prepare imgtool once if `.venv` is absent:

```sh
python3 -m venv .venv
.venv/bin/pip install -r components/mcuboot-2.4.0/scripts/requirements.txt
```

Run host tests for logic, protocol, storage, Flash-map, handover, or confirmation changes:

```sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ctest --test-dir build/HostTests --output-on-failure
```

Run Rust quality checks for updater changes:

```sh
cargo fmt --manifest-path Tools/updater/Cargo.toml -- --check
cargo clippy --manifest-path Tools/updater/Cargo.toml --locked --all-targets --features fake-e2e -- -D warnings
```

Build and verify Debug firmware for firmware/CMake/linker changes:

```sh
cmake --preset Debug -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Debug --clean-first --parallel
cmake --build build/Debug --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls
```

Release verification requires an existing ECDSA-P256 key outside the repository:

```sh
cmake --preset Release -DMCUBOOT_SIGNING_KEY=/secure/path/release-ec-p256.pem
cmake --build build/Release --clean-first --parallel
cmake --build build/Release --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls
```

Verify retained evidence when changing HIL assets, evidence manifests, or release documentation:

```sh
python3 Tests/HIL/verify_evidence.py
```

- Match `.clang-format`; format only touched project-owned C/C++ files.
- Add or update one focused host test for non-trivial logic. Keep portable dependency and USB boundary checks passing.
- Hardware flashing/HIL is required for claims about physical swapping, rollback, reset, USB enumeration, or power-loss behavior; host tests and builds do not prove those behaviors.

## Programmable artifacts and hardware safety

- Before every J-Link flash or USB updater install, state the build configuration and exact artifact path, role, and destination. For raw binaries, state the absolute Flash address; for USB installs, state that delivery is protocol-controlled Secondary-only.
- Direct-program only `build/<preset>/Boot/boot_firmware.bin` at `0x00000000`, a matching `*_primary.bin` at `0x00010000`, or a matching `*_update.bin` at `0x00042000`, under an approved HIL preflight.
- USB updater `install` accepts `artifacts/app-signed-<version>.bin`; do not pass the 204800-byte slot-padded `app-primary-<version>.bin` or `app-update-<version>.bin`. Raw linked payloads are not direct-programming artifacts.
- Never mix artifacts from different presets or silently substitute files from `build/HIL/`, `evidence/`, or an earlier build. Any exceptional artifact must be named explicitly and checksum-verified before use.
- Before programming, identify the exact probe and target, verify addresses and artifact hashes, preserve required device data, and avoid the Reserved region. Do not claim HIL success without retained logs/evidence.

## Change discipline

- Prefer the smallest root-cause change in the fewest project-owned files. Reuse existing contracts and helpers; avoid speculative abstractions.
- Keep hardware-specific code below portable interfaces. Extend the architecture boundary checks when adding portable files or forbidden dependencies.
- Update `README.md` or `docs/roadmap/` only when behavior, commands, architecture, gates, or implementation status actually changes.
