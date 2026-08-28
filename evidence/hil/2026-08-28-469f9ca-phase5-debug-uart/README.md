# Phase 5 Debug UART HIL

This bundle records the Debug EasyLogger UART investigation on the revision-2
HC32F460xE board. The firmware uses USART3 TX on PB13/TP2 at 115200 8N1.

Verified:

- Debug Boot and updater Primary matched their expected images before testing.
- USART3 was initialized successfully; TXE and TC were set and the calculated
  baud-rate error was approximately +0.077%.
- PB13 PFSR selected Func32 (`USART3_TX`).
- A complete `UART_TEST\r\n` stimulus was accepted by USART3.
- The DAPLink VCOM TX/RX self-loop passed byte-for-byte.
- The initialization sequence matches the official HC32F460 DDL example.

Not verified:

- No byte was captured from TP2 through the DAPLink RX input. The remaining
  fault boundary is the physical PB13-to-TP2-to-DAPLink RX path. This is a
  probable board/connection issue, not a confirmed PCB defect.

Result: Debug UART capture is deferred and is not recorded as passed. Resume
only with a TP2 idle-voltage/waveform measurement and PB13-to-TP2 continuity
check. The full `0x00000000-0x00075FFF` pre-HIL image was restored and read
back byte-for-byte; both files have SHA256
`68749a28839ef5b1e98473e014b611261add7434adb3dbc4419793c3fabc9b05`.
