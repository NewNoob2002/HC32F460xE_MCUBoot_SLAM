# Application Charger and Battery Plan

Status: A1 software complete; refactored image requires target validation

Last updated: 2026-09-03

## Scope

Develop the product Application for two BQ40Z50-managed battery packs plus the
HUSB238 and MP2762A devices on the shared I2C2 bus. Boot and the frozen firmware
update contracts are unchanged.

## Hardware contract

- I2C2 is a bounded blocking 100 kHz master on PA9/SCL (`GPIO_FUNC_51`) and
  PA8/SDA (`GPIO_FUNC_50`).
- USB VBUS sensing remains disabled because PA9 belongs to I2C2 on this board.
- BQ40Z50 pack addresses are default `0x0B` and configured alternate `0x0C`.
- HUSB238 is `0x08`; MP2762A is `0x5C`.
- PA4=`CHARGE_STAT1`, PA5=`PG`, PA7=`PSYS`, PB0=`IAM`, PB1=`IBM`.
- ADC1 channels 7, 8 and 9 cover PA7, PB0 and PB1 respectively.

## Application layering

```text
App/Core/main.c
  -> App/Services/power_devices
       -> App/Devices/{bq40z50, husb238, mp2762a}
            -> Platform/HC32F460/bsp_i2c2
  -> App/Diagnostics/app_diagnostics
       -> power-device startup snapshot
       -> watchdog and USB runtime status
```

`main.c` only sequences startup and cooperative polling. Device addresses and
protocol parsing live under `App/Devices`; bus composition lives in
`App/Services`. The production image does not contain a full-bus scan.

## A1 implementation

- Initialize I2C2 once, then probe `0x0B`, `0x0C`, `0x08` and `0x5C` in that
  order. Missing devices are degraded status and do not block USB startup.
- Read BQ40Z50 identity from the alternate `0x0C` pack after its address ACK.
- ManufacturerAccess words are sent low byte first. Firmware Version multi-byte
  fields use their documented byte order.
- Retain the complete result set in `g_app_power_devices_status`.
- Retain uptime, main-loop count, report count, watchdog state and USB runtime
  state in `g_app_diagnostics`; emit a Debug report every five seconds.

## Evidence

- The pre-refactor 2026-09-03 HIL run found BQ40Z50 at `0x0C` and read Device
  Type `0x4500`, firmware `0x0106`, build `0x0024`, IT version `0x0385`,
  hardware `0x000C` and ChemID `0x2107`. Evidence is retained under
  `evidence/hil/2026-09-03-app-i2c2-scan/`.
- An operator log on 2026-09-03 reported successful responses from the existing
  `0x0C`, `0x08` and `0x5C` devices. It predates the two-pack probe change and
  is not claimed as validation of `0x0B` or the refactored image.
- HostTests cover BQ parsing, fixed probe order/failure handling and diagnostic
  scheduling. The 2026-09-03 local CI run passed strict ASan/UBSan HostTests
  20/20, Rust checks, Debug/Release firmware and all image/descriptor/syscall/
  signing-policy gates. Target HIL remains required for final two-pack acceptance.

## Next nodes

- A2: read-only MP2762A/BQ40Z50 status and telemetry.
- A3: PA7/PB0/PB1 ADC cross-check and calibration.
- A4: bounded charging policy using BQ recommendations, an 8.4 V hard ceiling,
  the confirmed pack current limit and input-power limits.
- A5: fault degradation, charge/discharge HIL and release evidence.

OTG, persistent telemetry, a voltage-derived SOC estimator, RTOS, DMA and new
firmware-update transports remain outside the current scope.
