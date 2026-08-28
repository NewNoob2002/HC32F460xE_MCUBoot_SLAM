# Phase 5 Slint GUI physical-install HIL evidence

On 2026-08-28 the archived Release Slint GUI completed one physical firmware
installation on the HC32F460xE board through the same Rust updater core used by
the CLI. Source revision was 469f9ca6083e32bfaaaaba1cc3bd1b5f2f72178d
with retained working-tree changes.

The current 483328-byte Flash range was backed up first and matched the prior
restored baseline byte-for-byte with SHA256
68749a28839ef5b1e98473e014b611261add7434adb3dbc4419793c3fabc9b05. J-Link
20781318 then verified the corrected Boot and invalidated only the first
Primary and Secondary sectors, producing cafe:0001 Boot recovery with the
expected identity and geometry.

The user clicked Refresh, Browse and Update exactly once. The selected
38339-byte signed image reported version 1.0.0 and a valid SHA256 indicator.
The completion screenshot shows Connected, Current Version 1.0.0, 100% progress
and Upgrade complete: application 1.0.0. udev captured cafe:0001 removal
followed by cafe:0002 addition, and an independent CLI info confirmed
Application 1.0.0. Primary's exact 38339 bytes are byte-identical to the signed
image; Primary trailer has copy_done=1, image_ok=1 and valid MCUboot magic,
while Secondary is erased.

The GUI and passive monitor were intentionally stopped with SIGINT only after
the completion screenshot and independent checks; their exit code 130 is test
cleanup, not a product failure. Finally, the pre-HIL Flash was restored,
verified, read back and compared byte-for-byte. Reserved
0x00076000-0x0007FFFF was never accessed, and the board was reset and left
running in its exact pre-GUI-HIL Flash state.
