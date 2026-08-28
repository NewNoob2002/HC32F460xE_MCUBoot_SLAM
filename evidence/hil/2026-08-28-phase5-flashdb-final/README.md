# Phase 5 FlashDB product-configuration HIL evidence

This HIL run started on 2026-08-28 and crossed local midnight into
2026-08-29 (UTC remained 2026-08-28). It used the dirty working tree based on
revision `323ebea6bbdfc57258ccfcc40eaa3e45ba61d3b1` and J-Link CE `63728710`
on the HC32F460xE all-IO breakout board.

Post-format software acceptance also passed: HostTests 15/15, Rust fmt/clippy
and the repository-defined Rust/C fake E2E, plus clean Debug and Release builds
with image and WinUSB verification. The exact build outputs and review are
recorded by the bundle contracts.

Attempt 1 prepared the exact Debug Boot but the USB device did not enumerate
after the J-Link software reset. No protocol request reached the target. The
retained snapshot showed Thread mode in `bsp_delay_ms()`, zero CFSR/HFSR and no
HardFault snapshot magic. The exact 512 KiB pre-HIL image was restored before
Attempt 2. This was classified as an infrastructure/reset-enumeration skip, not
a product failure.

Attempt 2 used the same artifacts and address bounds plus a physical power
cycle before USB discovery. Boot recovery enumerated as `cafe:0001`; `config
get` reported `provisioned=false`; the single `config set` stored hardware
`0x00004600`, board `1`, revision `2`. After a second physical power cycle,
`config get` still reported the same provisioned identity.

The CLI transferred and verified all 55,267 bytes of `updater_signed.bin`,
COMMIT passed, and `wait` observed `cafe:0002` Application `1.0.0`. Application
mode reported the same identity and `provisioned=true`. The post-install Flash
snapshot proves that Primary matches the signed image exactly, Primary has
`copy_done=1`, `image_ok=1` and valid MCUboot magic, Secondary header/trailer
are erased, and the FlashDB identity blob is present at `0x0007605C`.

Finally, the complete `0x00000000-0x0007FFFF` pre-HIL image was restored,
verified, read back and compared byte-for-byte. Both backup and restored
readback have SHA256
`ae1f723d4ef84ea2c27c622f048bf01cdbdd9ad47d3cd66c42cd590c3ca9d8ca`;
the target was reset and left running.
