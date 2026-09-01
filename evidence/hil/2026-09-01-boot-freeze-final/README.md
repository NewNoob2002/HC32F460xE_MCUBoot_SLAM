# Boot Freeze Final HIL

Final real-hardware validation of the current Boot freeze candidate ran on
2026-09-01 using J-Link MCP, J-Link CE 63728710, and the HC32F460xE all-IO
board.

- Debug Boot and its matching signed Primary were programmed at 0x00000000 and
  0x00010000; J-Link verifybin passed for both.
- Boot reached boot_handover_jump() with a 200 MHz system clock, the exact
  Primary/header contract, MSP 0x1FFF8C98, reset vector 0x00011131, and VTOR
  0x00010200.
- App Reset_Handler observed CONTROL/PRIMASK/BASEPRI/FAULTMASK all zero and the
  expected MSP. App clock initialization and automatic confirmation returned 0.
- A final reset with no armed breakpoint ran in the App at PC 0x0001184E, IPSR
  0, with CFSR/HFSR both zero. The target was left running.

The first high-level MCP flash call returned no write/verify evidence, so the
same MCP server's native J-Link loadfile plus verifybin path was used. A later
apparent HardFault was diagnosed as HFSR.DEBUGEVT/DFSR.BKPT from a retained FPB
breakpoint; clearing all FPB comparators and repeating the reset removed the
fault. Neither event is counted as a product pass or product retry.

The 512 KiB pre-flash backup is kept only under ignored
build/local-evidence-backup/2026-09-01-boot-freeze-final/ because it may contain
device data. Its SHA-256 is retained in the manifest.
