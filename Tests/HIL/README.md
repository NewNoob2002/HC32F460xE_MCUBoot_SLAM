# HC32F460 MCUboot HIL

This directory owns the reusable, path-independent HIL procedure. Historical
commands, logs, and exact programmed images are retained under evidence/hil/.

## Render command files

Generate J-Link command files into an ignored build directory:

~~~sh
cmake \
  -DHIL_OUTPUT_DIR=build/HIL/commands \
  -DHIL_BACKUP_BIN=build/HIL/evidence/backup_before_hil.bin \
  -DHIL_BOOT_BIN=build/HIL/v1/Boot/boot_firmware.bin \
  -DHIL_V1_PRIMARY_BIN=build/HIL/v1/artifacts/app_primary.bin \
  -DHIL_V2_TEST_BIN=build/HIL/v2-test/artifacts/app_update.bin \
  -DHIL_V2_CONFIRM_BIN=build/HIL/v2-confirm/artifacts/app_update.bin \
  -P Tests/HIL/render_scripts.cmake
~~~

The renderer uses absolute paths in generated files because J-Link Commander
resolves paths from its process directory. The tracked templates contain no
workstation-specific path.

Run the generated files in numeric order. Select the intended probe explicitly
with the option supported by the installed J-Link Commander; never rely on the
first connected probe when multiple probes are present.

## Safety bounds

- Target: HC32F460xE over SWD.
- Boot: 0x00000000-0x0000FFFF.
- Primary: 0x00010000-0x00041FFF.
- Secondary: 0x00042000-0x00073FFF.
- Scratch: 0x00074000-0x00075FFF.
- Never access Reserved Flash 0x00076000-0x0007FFFF.
- Do not mass erase, unlock, recover, write OTP, or change protection state.
- Capture evidence before retrying a failed stage.

These templates do not authorize a hardware operation. A run still requires a
recorded safety preflight, exact artifact hashes, target identity, and recovery
plan.
