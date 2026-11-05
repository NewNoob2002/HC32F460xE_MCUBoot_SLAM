# Application Charger and Battery Plan

Status: A1 C++ software complete; V1.1 image requires target validation

Last updated: 2026-10-28

## Scope

Develop the product Application for two BQ40Z50-managed battery packs plus the
HUSB238 and MP2762A devices on the shared I2C2 bus. Boot and the frozen firmware
update contracts are unchanged.

## Hardware contract

- V1.1 I2C2 is a bounded blocking 100 kHz master on PA3/SCL and PA2/SDA.
- PA9 is hard-tied to 3V3 through 0 Ohm and must never be driven. USB physical
  VBUS sensing remains disabled; it is not an I2C pin on V1.1.
- BQ40Z50 pack addresses are default `0x0B` and configured alternate `0x0C`.
- HUSB238 is `0x08`; MP2762A is `0x5C`.
- PA4 is MP2762A INT# (active low). PA5 is PG, the inverted ACOK# signal.
- PA7/PB0/PB1 ADC telemetry no longer exists on V1.1. MP2762A and BQ40Z50
  register reads will supply telemetry; no obsolete ADC channels are sampled.
- Active-low LEDs are red PB6 and blue PB5 only. APP_PRIMARY is a red+blue
  heartbeat; the removed cyan diagnostic mode is unavailable.

## Application layering

```text
main.cpp -> App (startup and cooperative loop)
  -> DataProc::{Power, Charger, Update, System} nodes
       -> HAL::{I2cBus, ChargerSignals, BQ40Z50, HUSB238, MP2762A}
            -> HC32 adapters -> shared C BSP
  -> Diagnostics (typed subscriptions and periodic pulls)
       -> g_app_diagnostics snapshot / CORE_DEBUG_PRINTF
  -> Framework/DataBroker (fixed arrays and embedded DataTimer)
```

The C++17 Application uses static/caller-owned objects, exact-size typed pulls
and synchronous publish snapshots; no heap, exceptions, RTTI or ISR dispatch.
Power/System are pull-only. Charger samples initially and on a 100 ms timer,
publishing changes. Update polls USB every main-loop pass and publishes only
configured/manager-state/errors changes. Watchdog feeding also runs directly
every loop pass. Diagnostics pulls all four nodes every 5000 ms and logs
charger changes. Boot/shared C boundaries and startup panic messages are
preserved; see [ADR-007](../adr/ADR-007-app-cpp-databroker.md).

## A1 implementation

- Initialize I2C2 once, then probe `0x0B`, `0x0C`, `0x08` and `0x5C` in that
  order. Missing devices are degraded status and do not block USB startup.
- Read BQ40Z50 identity from the alternate `0x0C` pack after its address ACK.
- ManufacturerAccess words are sent low byte first. Firmware Version multi-byte
  fields use their documented byte order.
- Retain the complete result set in the Power state and
  `g_app_diagnostics.power`; the former standalone power global is superseded.
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
- Current C++ HostTests use fake I2cBus/ChargerSignals objects to cover BQ
  parsing and failure atomicity, fixed probe order/failure handling, charger
  change detection, typed diagnostics and direct watchdog/USB scheduling.
  Debug, Release and Phase2 builds pass; this is compile/host evidence only.
- Historical verification: The 2026-09-03 local CI run passed strict ASan/UBSan
  HostTests 20/20, Rust checks, Debug/Release firmware and all image/descriptor/
  syscall/signing-policy gates. Target HIL remains required for final two-pack
  acceptance.

## Next nodes

- A2: read-only MP2762A/BQ40Z50 status and register telemetry. BQ40Z50 SBS
  status for both packs is polled at 1 Hz and exported as Debug Link tags
  `0x50`/`0x51`; MP2762A raw status/ADC registers are exported as tag `0x12`
  with host-side scaling that A3 must validate.
- A3: validate MP2762A/BQ40Z50 register telemetry, units, freshness and
  cross-device consistency on V1.1; the former ADC node is removed.
- A4 (started): without a pack the MP2762A keeps cycling between fast charge
  and battery OVP while its DC/DC boosts SYS. The Power node now suspends it
  (REG08H `SUSP_EN=1`, `CHG_EN=0`, read-back checked) after 3 s with the J2
  BQ40Z50 (`0x0B`) not answering, re-applies that every second because a replug
  resets the charger, and lifts suspend for 5 s every 60 s so a pack in BQ
  shutdown can be driven awake. J4 needs no power without a pack. HIL
  2026-11-04 confirmed the transitions and a quiet `INT#`; the wake window
  still has to be proven against a sleeping pack.
- A4 (charge limits): while the J2 pack (`0x0B`) answers, the Power node
  writes REG04H/REG02H/REG00H from min(pack request, settings, 8.4 V ceiling)
  and enables CHG_EN only with `charge_enable=1`, a capped request of at least
  180 mA and no OCA/TCA/OTA alarm. REG03H pre-charge is held at the hardware
  minimum of 180 mA; lower caps/requests keep charging off across automatic
  phase changes. Debounce/wake windows also honor disable and the current cap.
  Every write is read back and re-applied each second; explicit stop requests
  precede input-limit writes, and failed limit/control writes trigger a stop
  attempt without hiding the error. The `0x0C` pack is output-only.
- A4 (input limit): the MP2762A input limit is min(setting, 90% of the
  HUSB238 PD contract, else Type-C advertisement, else 500 mA), applied at
  startup and every second. Telemetry tags `0x13`-`0x15` expose the charger
  read-back, the charge decision and the PD source to the CLI and GUI.
- A4 (remaining): thermal observation at 2 A with the junction reading, and a
  sleeping-pack wake test.
- A5: fault degradation, charge/discharge HIL and release evidence.

OTG, persistent telemetry, a voltage-derived SOC estimator, RTOS, DMA and new
firmware-update transports remain outside the current scope.
