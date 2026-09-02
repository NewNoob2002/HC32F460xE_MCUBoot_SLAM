# Application Charger and Battery Plan

Status: identity and shared-bus node blocked on target device response

Last updated: 2026-09-02

## Scope

Develop the product Application for a 2S, nominal 7.2 V, 6.9 Ah / 49.68 Wh
battery pack with an 8.4 V limited charge voltage. Firmware-update expansion
phases remain deferred. Boot and the frozen updater contracts are not changed.

## Confirmed hardware contract

- I2C2 is 100 kHz on PA9 SCL (`GPIO_FUNC_51`) and PA8 SDA
  (`GPIO_FUNC_50`).
- Current-board USB VBUS sensing is disabled so PA9 remains owned by I2C2.
- Known 7-bit addresses: BQ40Z50 `0x0B`, HUSB238 `0x08`, MP2762A `0x5C`.
- PA4=`CHARGE_STAT1`, PA5=`PG`, PA7=`PSYS`, PB0=`IAM`, PB1=`IBM`.
- ADC1 channels 7, 8 and 9 cover PA7, PB0 and PB1 respectively.

## Node A1 - Bus and identity confirmation

Status: BLOCKED

Implementation requirements:

1. Disable the current PA9 USB VBUS alternate function.
2. Initialize HC32 I2C2 as a bounded blocking 100 kHz master.
3. Probe only `0x0B`, `0x08` and `0x5C`; no full-bus scan.
4. Read BQ40Z50 Device Type, Firmware Version, Hardware Version and ChemID
   using read-only ManufacturerAccess subcommands.
5. Keep HUSB238 and MP2762A access to fixed-address probes until their read-only
   telemetry drivers are introduced in A2.
6. Publish results through Debug UART/RTT and retained global symbols.
7. A missing power device is degraded status, not an App panic or USB failure.

Current evidence (2026-09-01):

- Strict HostTests passed 18/18 and the Debug App/signature verification passed.
- J-Link `63728710` programmed and read back the signed Debug Primary at
  `0x00010000`; the deployed/readback hash is
  `db4bfb0aed29c03dca84f6162b0a29bd10c2fc3644298920521ffc2f8b787645`.
  A later clean build regenerated only the ECDSA signature (`15b8d830...`); two
  bounded Commander attempts did not replace the verified deployed image.
- PA9/PA8 retain the I2C2 functions, the idle pins are high, and I2C2 timing
  remains configured after failed transfers (`CCR=0x00031B1A`, BUSWAIT enabled).
- The earlier A1 artifact logged `i2c2=0`, `bq@0b=-3`, `mp@5c=-3`,
  `husb@08=-3`; BQ identity therefore remained unavailable. The revised result
  codes and startup wiring have not yet been redeployed, so the three addresses
  are still not physically confirmed.
- Blocker: power/connect the charger, gauge and USB-PD devices on the identified
  target, verify their rails and pull-ups, then rerun the same bounded probes.

Software refinement (2026-09-02):

- The I2C2 BSP transaction order now matches the previously validated
  `Platform/Core/src/Wire.cpp`, including configuring single-byte receive NACK
  before the RX address phase.
- BSP results now distinguish init stages, bus busy, start/address/data/restart/
  receive/stop timeouts, address/data NACK and arbitration loss. Debug builds
  continue to publish those numeric results to both UART and RTT through the
  existing EasyLogger port; the BSP does not add a second logging dependency.
- The reviewed App target now compiles the BQ40Z50 driver and invokes the bounded
  A1 probes during normal startup. Strict HostTests 18/18 and the clean Debug
  firmware plus image/descriptor/syscall verification pass locally.
- A1 remains BLOCKED until the revised image is deployed and the three bounded
  probes are repeated on powered, connected hardware.

Acceptance requires strict host tests, a clean Debug firmware build, preserved
USB/update verification, and target RTT evidence from the identified J-Link
probe. Hardware evidence must record exact firmware hashes and target identity.

## Later nodes

- A2: read-only MP2762A/BQ40Z50 status and telemetry.
- A3: PA7/PB0/PB1 ADC cross-check and calibration.
- A4: bounded charging policy using BQ recommendations, an 8.4 V hard ceiling,
  the confirmed pack current limit and input-power limits.
- A5: fault degradation, charge/discharge HIL and release evidence.

OTG, persistent telemetry, a voltage-derived SOC estimator, RTOS, DMA and
firmware-update transport expansion are outside the current scope.
