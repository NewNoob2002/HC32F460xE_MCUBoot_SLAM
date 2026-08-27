# Phase 4 G4 USB Loopback HIL Evidence

- Date: 2026-08-27 (Thursday, Asia/Shanghai)
- Source: `fd703cde5a312f05d74926f0c055fca6053d6bbb`, clean tree `04a46a77e3089280193b88d22202bd3c533021e2`
- Target: HC32F460xE, J-Link `20781318`, SWD 4000 kHz, VTref about 3.35 V
- Unique USB Primary: 204800 bytes, SHA256 `97491e08ffe00732136576578352a52c7ff9eb8e7699abd8e2231eca8d4a15b4`

Results: descriptors PASS; endpoint stall/clear-stall PASS; 10,000 transfers PASS; ten intentional transfer-time unplug/re-enumeration rounds each recovered with 100/100 transfers; 30-minute run PASS with 2,960,145 transfers in 1800.001 seconds; final firmware counters were `errors=0`, `packets=3,496,742`. Udev retained 18 complete device-level remove/add/bind groups, including the ten required manual rounds, and no target USB event occurred during the final 13:00:02-13:30:02 continuous window.

The pre-HIL range `0x00000000-0x00075FFF` was restored byte-for-byte. Backup and readback are both 483328 bytes, SHA256 `410a0acccbb0a231d35508a9e545953b7490406986557750c531bd56edf39b1b`, with zero non-`0xFF` bytes. Reserved Flash `0x00076000-0x0007FFFF` was not accessed.

`failures/b3da613/` retains the earlier J-Link FlashDL failure and the product `LIBUSB_ERROR_OVERFLOW (-8)` that led to the final `DIEPEMPMSK` reset fix. Two fd703cd `device not found` starts are retained as enumeration-window infrastructure failures and were not counted as manual rounds.

