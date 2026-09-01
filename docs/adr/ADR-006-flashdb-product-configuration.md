# ADR-006: FlashDB Product Configuration

Status: Accepted

Date: 2026-08-28

Amended: 2026-09-01

## Context

Production may initially program only Boot. The updater must then provision a
device serial, hardware-version string and Application USB PID before installing
an Application. These settings must survive firmware replacement without
exposing arbitrary Flash addresses or weakening the signed-image compatibility
check.

## Decision

- Reserved Flash `0x00076000-0x0007FFFF` is one FlashDB 2.2.0 KVDB partition:
  five 8 KiB erase sectors with 32-bit write granularity. Firmware image
  commands remain Secondary-only; only the product-configuration backend owns
  this Reserved region.
- FlashDB is integrated through its bundled FAL layer and a project-owned HC32
  port over the bounded Flash-map area `FLASH_AREA_ID_PRODUCT_CONFIG`. Vendored
  FlashDB sources are not modified.
- Schema v3 stores one atomic `product.identity` blob containing format
  magic/version, device serial, hardware-version string and Application PID.
  Earlier development schemas are intentionally unsupported.
- Before provisioning, reads return the build-time values from
  `Config/Product/ProductIdentity.env`, an empty device serial/hardware version
  and `provisioned=false`. A successful USB write persists the complete blob
  and changes the state to `provisioned=true`.
- USB provisioning is accepted only from Boot recovery (`cafe:0001`) and is
  write-once. Application mode remains read-only. A second write is rejected; changing or
  clearing an identity requires an explicitly preflighted Reserved-region erase
  or full-device recovery operation. This prevents the configuration command
  from becoming a general compatibility-bypass mechanism.
- Application PID must be non-zero, differ from Boot PID and fall inside the
  product-approved range. Application firmware patches the device descriptor
  from the effective FlashDB value before USB registration; unprovisioned
  configurations use `0002`. Provisioned Boot and Application descriptors use
  the configured serial; unprovisioned descriptors retain the UQID-derived
  serial.
- Protocol V1 adds capability bit 3 and commands `PRODUCT_CONFIG_GET` (`0x03`)
  and `PRODUCT_CONFIG_SET` (`0x04`). No Host-provided key names, lengths or
  physical addresses are accepted.
- `hardware_id`, `board_id` and `board_revision` remain build-time-only
  compatibility values. Device Info, BEGIN validation and MCUboot COMMIT
  validation use those fixed values; provisioning cannot change them. Host and
  device continue to validate the signed protected compatibility TLV
  independently.

## Consequences

- The Rust CLI and GUI expose the same write-once device serial, hardware
  version and Application PID configuration.
- Boot and Application initialize FlashDB when their updater path starts. First
  initialization may format Reserved Flash even before the identity is
  provisioned.
- All future updater HIL backup/restore procedures must include the full
  `0x00000000-0x0007FFFF` Flash range. Historical evidence that intentionally
  excluded Reserved remains historical and must not be reused as a recovery
  recipe for this node.
- Earlier FlashDB evidence remains historical. Product Config v3 physical
  persistence, USB serial/PID descriptors, compatible install, SN-bound
  reconnect and exact full-Flash restoration passed under
  `evidence/hil/2026-09-01-product-config-v3/`.

## Rejected alternatives

- Arbitrary Host KV read/write: rejected because it expands the USB trust
  boundary and exposes future secrets or policy keys.
- Rewritable signed-image compatibility identity: rejected because changing it
  could make a signed image for another board appear compatible.
- Store each identity field as a separate KV: rejected because interruption
  could leave a partially updated identity.
