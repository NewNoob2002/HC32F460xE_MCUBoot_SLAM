# SLAMRTK Charge Board V1.1 hardware archive

This directory is the board-level source of truth for firmware pin ownership.
The primary source is `SLAMRTK_Chargeborad_V1.1.pdf` (one A4 page, OrCAD
`Charge_board.opj`, PDF date 2026-10-28, SHA256
`3d2008621771674cf0a9eb02d2cac4dd26a07f03854508caa699274d8588179c`).

`SLAMRTK_Chargeborad_V1.0.pdf` (SHA256
`07e75363b1b97b2df771b024f825e3bc89c33713d4e4083df9a75c22b23e73b5`) and the
2026-08-28 all-pins breakout board are historical. Their HIL evidence does not
validate V1.1 pin ownership; see "V1.0 to V1.1 changes" below.

## MCU and clocks

- MCU: U2 HC32F460JEUA, QFN48.
- Main crystal: Y3 NX3225SA 8 MHz on XTAL_I/XTAL_O (PH0/PH1), C1/C6 10 pF.
- RTC crystal: Y2 FC-12M 32.768 kHz on XTAL32_I/O, C12/C15 5 pF.
- PB11/MD is pulled up to `VCC_3V3` by R1 4.7 kOhm.
- MCU supply: `VCC_3V3`, generated from `TYPEC_IN` by U11 SCT2432STER
  (FB divider R7 31.6 kOhm / R10 10.2 kOhm). D9 is the 3.3 V power indicator;
  TP1 is the 3.3 V test point.

## Firmware pin map

| MCU pin | QFN pin | Signal/net | Direction | Firmware use |
| --- | ---: | --- | --- | --- |
| PA2 | 12 | `I2C2_SDA` | bidirectional | I2C2 SDA, `GPIO_FUNC_50`, R3 4.7 kOhm pull-up |
| PA3 | 13 | `I2C2_SCL` | bidirectional | I2C2 SCL, `GPIO_FUNC_51`, R4 4.7 kOhm pull-up |
| PA4 | 14 | `CHARGE_STAT1` | input | MP2762A `INT#` through R311 0 Ohm, active low, R50 100 kOhm pull-up |
| PA5 | 15 | `PG` | input | Inverted MP2762A `ACOK#` through Q9 2N7002, R45 100 kOhm pull-up |
| PA6 | 16 | `WDOG_DONE` | output | TPL5010 DONE through R18 0 Ohm |
| PA9 | 30 | tied to `VCC_3V3` | input only | R320 0 Ohm; firmware must never drive PA9 |
| PA11 | 32 | `USB_DM` | bidirectional | USB FS D- |
| PA12 | 33 | `USB_DP` | bidirectional | USB FS D+ |
| PA13 | 34 | `SWDIO` | bidirectional | SWD |
| PA14 | 37 | `SWCLK` | input | SWD clock |
| PB4 | 40 | `VER_DET` | input | Board-version detect, R38 4.7 kOhm pull-up; not read by firmware |
| PB5 | 41 | `LED_B` | output | Blue status LED cathode, active low |
| PB6 | 42 | `LED_R` | output | Red status LED cathode, active low |
| PB12 | 25 | `USART1_RX` schematic net | input | Debug UART RX test point TP7; not used |
| PB13 | 26 | `USART1_TX` schematic net | output | Debug UART TX test point TP8 |
| NRST | 7 | `MCU_RESET` | input | TPL5010 RSTn through R17 0 Ohm, R15 10 kOhm pull-up, TP2 |

All other MCU GPIOs (PA0, PA1, PA7, PA8, PA10, PA15, PB0-PB3, PB7-PB10,
PB14, PB15, PC13, PH2) are unconnected on V1.1.

Net names are logical labels. The QFN symbol prints `I2C1_SDA/SCL` beside
PA2/PA3, but the board nets are `I2C2_*`; firmware keeps `CM_I2C2` and selects
it with the HC32 Func_Grp functions 50/51. PB12/PB13 likewise route USART3
(`GPIO_FUNC_33/32`), not `CM_USART1`.

PA9 is the HC32 USB VBUS-sense function pin. On V1.1 it is hard-wired to
`VCC_3V3`, so configuring it as an output low would short the 3.3 V rail
through 0 Ohm. Firmware keeps the pin at its reset input state and continues to
use the software VBUS-valid override (`BSP_USB_VBUS_SENSE_ENABLE=0`).

PB4 resets as JTAG `NJTRST`. Reading `VER_DET` requires
`bsp_debug_port_use_swd()` first; the pull-up-only V1.1 circuit reads high.

## Status LEDs

- D32 (board edge) and D33 (`LED-2`) are common-anode red/blue dual LEDs on
  `VCC_3V3`. Both share `LED_R`/`LED_B`; R321-R324 are 200 Ohm.
- A low level turns a channel on. There is no green channel on V1.1.
- Boot/App LED patterns are defined by `bsp_status_led_sample()`.

## Debug and log access

- J1 (`CON3`, board edge): pin 1 ground, pin 2 SWDIO through R9 0 Ohm,
  pin 3 SWCLK through R8 0 Ohm. The silkscreen note says "download UART", but
  the schematic routes SWD.
- TP7 is the debug UART RX net (PB12); TP8 is the debug UART TX net (PB13).
- Debug firmware logs on TP8 at 115200 baud, 8 data bits, no parity, one stop
  bit, 3.3 V TTL, and mirrors the same log to SEGGER RTT channel 0.
- For passive log capture connect USB-UART RX to TP8 and share ground. Do not
  connect a 5 V UART signal to the board.
- Historical USART3 results under `evidence/hil/2026-08-28-*` and
  `2026-09-02-logging-rtt-uart` were captured on earlier boards.

## USB, Type-C input and PD

- J3 `typec-304s` carries USB FS on PA11/PA12 and VBUS to `TYPEC_IN`
  (C66 2.2 uF, D14 SD1571P6W and D28 SMBJ24CA protection).
- U10 HUSB238 (`0x08`) is the PD sink on J3 CC1/CC2. Its GATE pin drives the
  Q10 SI7617DN input switch from `TYPEC_IN` to `TYPEC`. VSET R65 17.8 kOhm,
  ISET R66 0 Ohm; its supply `USB238_3V3` comes from `TYPEC_IN` through R62.
- Firmware USB identities and WinUSB descriptors are software contracts; keep
  them in `Config/Product/ProductIdentity.env` and the Phase 5 ADRs.

## Charger

- U9 MP2762A (`0x5C`) on I2C2 through R51/R52 33 Ohm. Input `TYPEC` through
  the R49 10 mOhm sense resistor; output `SYS`, battery `VBAT`.
- `INT#` (pin 9) drives `CHARGE_STAT` -> R311 0 Ohm -> `CHARGE_STAT1`/PA4.
- `ACOK#` (pin 28, R48 100 kOhm pull-up to `LDO_3V6`) drives Q9; `PG`/PA5 is
  its open-drain inverse. Confirm `ACOK#` polarity against the MP2762A datasheet
  before using PA5 as an input-present decision.
- `IAM` and `IBM` are not connected. `PSYS` (R57 15 kOhm to ground), `PROCHOT`
  (R56 100 kOhm pull-up) and `OTG`/`USB2_ID` (R317 4.7 kOhm pull-up) end at
  net labels only and do not reach the MCU, so V1.1 has no MCU ADC charger
  telemetry; current/power data must come from MP2762A/BQ40Z50 registers.
- NTC network: R55 49.9 kOhm, R58 4.7 kOhm, R59 49.9 kOhm, R61 2.49 kOhm.

## I2C2 bus and connectors

- Shared I2C2 at 100 kHz. Known 7-bit addresses: BQ40Z50 packs `0x0B`/`0x0C`,
  HUSB238 `0x08`, MP2762A `0x5C`. Production firmware probes only these
  configured devices.
- J2 `WAFER-HY2.0-4PLB` battery connector: 1 `VBAT` (TP5), 2 `I2C2_SCL`,
  3 `I2C2_SDA`, 4 ground (TP6). D29/D30 ESD7551N2T5G protect SCL/SDA.
- J4 `FWF20019-S04B24W5M` output: 1 `VCC_SYS` (D31 SMAJ10CA), 2 `I2C2_SCL`,
  3 `I2C2_SDA`, 4 ground.
- FPC3 `HC-FPC-05-09-14RLTAG` and FPC4 `AFC11-S14ICC-00` carry `VCC_3V3`,
  `I2C2_SDA/SCL`, `CHARGE_STAT`, `MP2762_ACOK`, `LED_B`, `LED_R`, `TYPEC` and
  ground to the companion board.

## Watchdog

- External watchdog: U4 TPL5010DDCT, DELAY R21 22 kOhm (R20 100 kOhm NC).
- `DONE` is driven by PA6 through R18 0 Ohm; `RSTn` reaches `MCU_RESET`
  through R17 0 Ohm.
- The schematic note specifies a 60 s watchdog interval. Firmware pulses DONE
  for 1 ms every 3 s, leaving margin before the external deadline.

## V1.0 to V1.1 changes

| Item | V1.0 / breakout firmware | V1.1 |
| --- | --- | --- |
| I2C2 | PA9 SCL / PA8 SDA | PA3 SCL / PA2 SDA; PA9 tied to 3.3 V |
| Status LED | RGB on PA0/PA1/PA2 | Red PB6 / blue PB5, no green |
| PA4 | `CHARGE_STAT1` | MP2762A `INT#` via R311 |
| PA5 | `PG` | `PG` = inverted `ACOK#` via Q9 |
| PA7/PB0/PB1 | `PSYS`/`IAM`/`IBM` ADC | Unconnected |
| PB4 | unused | `VER_DET` pull-up |
| UART test points | TP1 RX / TP2 TX | TP7 RX / TP8 TX; TP2 is `MCU_RESET` |
| Connectors | — | J2 battery, J4 output, FPC3/FPC4 companion |

When the board schematic changes, add the new source PDF and update this file
in the same commit before changing BSP pin definitions.
