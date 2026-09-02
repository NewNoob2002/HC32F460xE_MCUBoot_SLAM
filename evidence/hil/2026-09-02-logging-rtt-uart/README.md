# Logging RTT/UART HIL

The Debug Boot and matching signed Primary were backed up, programmed, verified
and run on HC32F460xE through J-Link 63728710 at VTref 3.300 V.

The target reached the App main loop without Cortex-M faults. J-Link MCP captured
the EasyLogger initialization, D/core app startup, and D/core usb=0 confirm=0
from RTT channel 0. USART3 registers showed TX enabled and complete, with the
final newline in TDR, confirming the EasyLogger port wrote the UART peripheral.

No USB-UART adapter was present, so the external PB13 waveform was not captured.
Programmed images and target readbacks are intentionally excluded from Git; their
paths and SHA-256 values remain in the manifest and result contracts.
