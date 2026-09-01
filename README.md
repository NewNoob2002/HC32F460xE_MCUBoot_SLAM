# HC32F460xE MCUboot

Minimal HC32F460xE Boot + App firmware using MCUboot 2.4.0, ECDSA-P256 image verification, scratch-based swapping, and application confirmation.

## Prerequisites

- CMake 3.28 or newer
- Ninja
- GNU Arm Embedded toolchain with Newlib headers
- Python 3

Prepare MCUboot's image tool in a local virtual environment:

~~~sh
python3 -m venv .venv
.venv/bin/pip install -r components/mcuboot-2.4.0/scripts/requirements.txt
~~~

## Build and test

Run the host tests:

~~~sh
cmake --preset HostTests --fresh
cmake --build build/HostTests --clean-first --parallel
ctest --test-dir build/HostTests --output-on-failure
~~~

Verify retained HIL evidence from previous release-qualified runs:

~~~sh
python3 Tests/HIL/verify_evidence.py
~~~

Build and verify Debug images. The Debug preset uses the external development key at
`$HOME/Desktop/workspace/MyKey/HC32_MCUBOOT_SLAM/dev-ec-p256.pem`; it must not be committed or used for production:

~~~sh
cmake --preset Debug -DAPP_VERSION=1.0.0 -DAPP_AUTO_CONFIRM=ON
cmake --build build/Debug --clean-first --parallel
cmake --build build/Debug --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls
~~~

Release configuration never creates a private key. Supply an existing ECDSA-P256 key explicitly:

~~~sh
cmake --preset Release \
  -DMCUBOOT_SIGNING_KEY=/secure/path/release-ec-p256.pem \
  -DAPP_VERSION=1.0.0 \
  -DAPP_AUTO_CONFIRM=ON
cmake --build build/Release --clean-first --parallel
cmake --build build/Release --target verify_app_image verify_winusb_descriptors verify_newlib_syscalls
~~~

## Programmable images

Only these binary files are intended for direct programming:

| Image | Flash address | Purpose |
| --- | ---: | --- |
| build/&lt;preset&gt;/Boot/boot_firmware.bin | 0x00000000 | Bootloader |
| build/&lt;preset&gt;/artifacts/app-primary-&lt;version&gt;.bin | 0x00010000 | Confirmed image for the Primary slot |
| build/&lt;preset&gt;/artifacts/app-update-&lt;version&gt;.bin | 0x00042000 | Test image for the Secondary slot |

`App/app-<version>.bin` is a raw linked payload, not an MCUboot-bootable image.
`artifacts/app-signed-<version>.bin` is signed but not slot-padded and is the USB updater input.

## Manual rollback HIL procedure

Use the same signing key for both application versions and preserve each build's artifacts before rebuilding another version.

1. Build version 1 with APP_VERSION=1.0.0; program boot_firmware.bin at 0x00000000 and its confirmed app-primary-1.0.0.bin at 0x00010000.
2. Reset and verify that version 1 starts from the Primary slot.
3. Build version 2 with APP_VERSION=2.0.0 and APP_AUTO_CONFIRM=OFF; program its app-update-2.0.0.bin at 0x00042000.
4. Reset once and verify that version 2 runs as a test upgrade.
5. Reset again without confirming version 2; verify that MCUboot reverts to version 1.
6. Rebuild version 2 with APP_AUTO_CONFIRM=ON, program the new app-update-2.0.0.bin, and boot it so the application confirms the image.
7. Reset again and verify that version 2 remains active.

The build and host tests validate layout, signing, handover, and confirmation contracts. They do not prove physical flash swapping, reset behavior, or rollback on hardware; the HIL sequence above remains required for a new release baseline.

The 2026-08-25 rollback HIL logs, exact programmed images, manifests, and checksums are retained under evidence/hil/2026-08-25-339f32c/. Reusable path-independent command templates live under Tests/HIL/. The full pre-test Flash backup is excluded because it may contain device-specific data; its size and hash remain in the evidence manifest.

See docs/build_report.md for the latest recorded verification report.

## Active roadmap

The completed minimal Boot/App milestone remains documented under `docs/superpowers/`. Current architecture, phases, hard gates, test strategy and status are maintained under `docs/roadmap/`; Phase 5 execution is detailed in `docs/roadmap/PHASE5_USB_UPDATER_PLAN.md`; major decisions are recorded under `docs/adr/`.
