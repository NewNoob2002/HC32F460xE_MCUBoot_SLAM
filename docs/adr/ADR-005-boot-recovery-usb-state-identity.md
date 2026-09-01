# ADR-005: Boot Recovery USB State Identity

Status: Accepted

Date: 2026-08-28

Amended: 2026-09-01

## Context

An updater that exists only in the Application cannot recover a board when both
MCUboot slots contain invalid images. The Host also needs to distinguish a
normal Application updater from a Boot-resident recovery updater without a new
protocol command or manual driver selection.

## Decision

- Boot recovery remains fixed at `cafe:0001`. Application defaults to
  `cafe:0002` and may use a write-once product PID inside the approved range in
  `Config/Product/ProductIdentity.env`. Per-device identity is always the
  UQID-derived USB serial, never the PID.
- Both states expose the same Protocol V1 vendor Bulk interface 0 and the same
  Microsoft OS 2.0 WinUSB interface GUID. The PID is the state discriminator.
- Boot runs MCUboot normally first. If `boot_go()` succeeds, Boot hands over and
  does not enumerate USB. If it fails, Boot initializes the shared updater and
  remains in recovery until a valid signed compatible image is installed.
- Recovery keeps the existing Secondary-only Storage contract. COMMIT validates
  the MCUboot signature and protected compatibility TLV before marking the
  Secondary candidate pending. Boot, Primary physical addresses and Reserved
  Flash are never accepted from the Host.
- `MCUBOOT_BOOTSTRAP` is enabled. After recovery resets, a valid Secondary image
  is copied into an invalid Primary slot and marked confirmed by MCUboot's
  bootstrap path. Normal valid-image upgrades retain scratch swap behavior.
- Host discovery enumerates matching VID/interface/serial candidates without
  opening them. GUI selection uses the current enumeration's `DeviceId`;
  Connect then performs HELLO plus DeviceInfo. Cross-enumeration matching uses
  the stable serial, and post-install waiting reconnects only the same serial in
  Application mode.

## Consequences

- A corrupted or empty Primary and Secondary no longer intentionally park in a
  WFI loop; they enter the Boot recovery USB path.
- Boot grows because it contains CherryUSB, Protocol V1, Manager and MCUboot
  update backends. It must remain within the fixed 64 KiB Boot region.
- Build and descriptor checks are local evidence only. Recovery enumeration,
  Secondary installation, bootstrap copy and final Application enumeration
  require an explicit destructive HIL preflight before they can be claimed.
- Before provisioning both modes derive the same stable per-chip serial from the
  HC32F460 96-bit UQID; after provisioning both use the configured serial.
- A connected GUI session sends DeviceInfo every 2000 ms. HELLO is sent once
  and cached; configuration and upgrade requests serialize on the same worker.
- Physical Product Config v3 evidence verifies `cafe:0001` recovery, configured
  serial persistence, `cafe:0020` Application enumeration and exact serial
  reconnect under `evidence/hil/2026-09-01-product-config-v3/`.

## Rejected alternatives

- Always enumerate Boot recovery even when a valid Application exists: rejected
  because it delays normal boot and creates two simultaneously valid update
  entry policies.
- Let recovery write Primary directly: rejected because it bypasses the existing
  Secondary-only trust boundary and MCUboot validation/swap ownership.
- Use one PID plus a protocol state field: rejected because Windows binding and
  Host discovery can distinguish state before opening the protocol.
