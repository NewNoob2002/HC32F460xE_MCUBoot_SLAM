# Phase 5 Boot recovery/bootstrap HIL evidence

On 2026-08-28 the HC32F460xE board and J-Link `20781318` completed the
invalid-slot Boot recovery path from `cafe:0001` to Application `cafe:0002`.
The run used source revision `469f9ca6083e32bfaaaaba1cc3bd1b5f2f72178d`
with the retained working-tree patches in this bundle.

Attempt 1 transferred and read back the complete signed image, then COMMIT
returned `BootControlError`. The failure is retained in
`logs/08_install_from_boot_recovery.txt`. Diagnosis found that the project
MCUboot backend called `bootutil_img_validate()` with a null loader state even
though scratch-swap image-size validation requires initialized sector state.
The smallest fix initializes and closes MCUboot's existing loader state around
validation; HostTests then passed 13/13 and the corrected Boot/image artifacts
were retained for attempt 2.

Attempt 2 passed without retry: Boot recovery `info` reported the expected
identity and geometry, 38339/38339 bytes transferred, device verification and
COMMIT passed, udev captured `cafe:0001` removal and `cafe:0002` addition, and
`wait --version 1.0.0 --timeout 30` succeeded. The first 38339 bytes read from
Primary are byte-identical to `artifacts/updater_signed_attempt2.bin` and share
SHA256 `acc9be453440823b13045da96d42c769e1f5ecd85a2364821e49b1a5c1cfe1b0`.
Primary contains the v1.0.0 image, Secondary's header is erased, and Primary's
trailer has `copy_done=1`, `image_ok=1` and valid MCUboot magic.

One evidence-collection defect is retained: J-Link interpreted the unprefixed
save length `38339` as hexadecimal and captured 230201 read-only bytes instead
of 38339. The read ended at `0x00048338`, below Reserved, changed no target
state, and the exact 38339-byte prefix passed `cmp` and SHA256 verification.
The four explicitly hexadecimal header/trailer snapshots have their intended
sizes.

Finally, `raw_commands/99_restore.jlink` erased, restored and verified exactly
`0x00000000-0x00075FFF`, then saved a complete 483328-byte readback. The
readback is byte-identical to both pre-HIL backups with SHA256
`68749a28839ef5b1e98473e014b611261add7434adb3dbc4419793c3fabc9b05`.
Reserved `0x00076000-0x0007FFFF` was never erased, programmed or read. The board
was reset and left running in its exact pre-HIL Flash state.
