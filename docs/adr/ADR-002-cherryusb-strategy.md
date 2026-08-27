# ADR-002: CherryUSB Strategy

Status: Accepted and implemented for Phase 4; G4 PASSED

Date: 2026-08-26

## Context

The first planned upgrade transport is USB Vendor Bulk. A USB device stack and an HC32F460 USB Device Controller Driver are required, but neither the USB stack nor its callback model may become the firmware-update architecture.

The upstream reference reviewed on 2026-08-26 was CherryUSB release `v1.6.1`, tag commit `c9625ff`, under Apache-2.0. Upstream documents a controller-port boundary including `usb_dc_init()`/`usb_dc_deinit()` and chip-specific implementations under `port/`. The reviewed upstream port list does not identify HC32F460 support.

Sources reviewed:

- https://github.com/cherry-embedded/CherryUSB/releases/tag/v1.6.1
- https://github.com/cherry-embedded/CherryUSB/blob/master/LICENSE
- https://github.com/cherry-embedded/CherryUSB/blob/master/docs/en/api/api_port.rst
- https://github.com/cherry-embedded/CherryUSB/blob/master/docs/en/quick_start/transplant.rst
- https://github.com/cherry-embedded/CherryUSB/tree/master/port

The reviewed release was subsequently pinned for Phase 4; `master` remains
unauthorized.

## Decision

- CherryUSB is the selected Device Stack backend below the project transport boundary.
- Phase 4 re-checked and pinned release `v1.6.1` at full commit
  `c9625ffa773ad10b8824d1b5361bca2ccc1f3d1e`.
- Import a reproducible source snapshot under `components/` with its upstream license and a small `UPSTREAM.md` containing repository URL, tag, commit and import date. A submodule is not the default because normal CI/fresh-clone use should not depend on recursive checkout.
- Do not track an upstream moving branch.
- Do not modify CherryUSB core for HC32. HC32 clock/pin/IRQ/controller code lives in an HC32-specific platform/backend directory and implements the documented DCD boundary.
- `fw_update` core and Protocol cannot include CherryUSB headers. Only the USB transport backend may include the project USB backend adapter; CherryUSB-facing code remains one level lower.
- The first integration milestone is Vendor Bulk loopback/echo. Flash and MCUboot APIs are prohibited until G4 passes.
- Upstream fixes should be contributed upstream when generally applicable. Any unavoidable local patch must be isolated, documented, minimized and covered by a reproducible test.

Target dependency chain:

```text
Protocol -> Transport contract -> USB Bulk transport
         -> USB backend -> CherryUSB -> HC32F460 DCD -> HC32 USB peripheral
```

## Alternatives

### Write a complete USB stack

Rejected because it creates large protocol/compliance risk unrelated to the product updater.

### Couple Vendor Bulk callbacks directly to the updater or Flash

Rejected because endpoint chunking is not protocol framing and callback/ISR context is unsuitable for Flash/session logic.

### Fork CherryUSB core for HC32

Rejected because upgrades and upstream comparison become expensive and platform code contaminates the common stack.

### Git submodule

Deferred. It keeps upstream history but increases clone/CI/user setup failure modes. It may be reconsidered if repository-size or license-update policy makes snapshots impractical.

### Vendor-specific HC32 USB stack

Remains a fallback if CherryUSB DCD feasibility fails G4 preconditions. The Transport/Protocol/Manager contracts must not change when switching stacks.

## Consequences

Positive:

- USB stack selection is replaceable below the Transport boundary.
- HC32 DCD risk is isolated and tested with echo before Flash integration.
- Dependency revision/license are reproducible in a fresh clone.

Costs and open work:

- The project-owned HC32F460 DCD must be maintained against the pinned boundary.
- G4 validated endpoint, IRQ and reset behavior through enumeration, stall
  recovery, 10,000 mixed transfers, ten re-enumeration recoveries and a
  30-minute run.
- USB VID/PID ownership and production descriptor values remain a Phase 5
  packaging decision; G4 intentionally used `fffe:ffff` test values.

## Phase 4 outcome

The unchanged required CherryUSB subset is retained under `components/cherryusb/`
with its license and `UPSTREAM.md`. HC32 DCD/platform code remains outside the
upstream tree. G4 passed on 2026-08-27 at source revision `fd703cd`, with retained
evidence under `evidence/hil/2026-08-27-fd703cd-phase4-g4/`. Phase 5 may bind the
Vendor Bulk backend to the existing Manager, but direct USB-to-Storage or
USB-to-MCUboot calls remain prohibited.
