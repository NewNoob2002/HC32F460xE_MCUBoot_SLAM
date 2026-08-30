# ADR-006: FlashDB Product Configuration

Status: Accepted

Date: 2026-08-28

## Context

Production may initially program only Boot. The updater must then report and
provision the board compatibility identity before installing an Application.
The identity must survive firmware replacement without exposing arbitrary Flash
addresses or weakening the signed-image compatibility check.

## Decision

- Reserved Flash `0x00076000-0x0007FFFF` is one FlashDB 2.2.0 KVDB partition:
  five 8 KiB erase sectors with 32-bit write granularity. Firmware image
  commands remain Secondary-only; only the product-configuration backend owns
  this Reserved region.
- FlashDB is integrated through its bundled FAL layer and a project-owned HC32
  port over the bounded Flash-map area `FLASH_AREA_ID_PRODUCT_CONFIG`. Vendored
  FlashDB sources are not modified.
- The first schema stores one atomic `product.identity` blob containing format
  magic/version, `hardware_id`, `board_id` and `board_revision`. The HC32 UQID
  USB serial remains hardware-derived and read-only.
- Before provisioning, reads return the build-time values from
  `Config/Product/ProductIdentity.env` with `provisioned=false`. A successful USB write
  persists the complete blob and changes the state to `provisioned=true`.
- USB provisioning is accepted only from Boot recovery (`cafe:0001`) and is
  write-once. Application mode remains read-only. A second write is rejected; changing or
  clearing an identity requires an explicitly preflighted Reserved-region erase
  or full-device recovery operation. This prevents the configuration command
  from becoming a general compatibility-bypass mechanism.
- Protocol V1 adds capability bit 3 and commands `PRODUCT_CONFIG_GET` (`0x03`)
  and `PRODUCT_CONFIG_SET` (`0x04`). No Host-provided key names, lengths or
  physical addresses are accepted.
- Device Info, BEGIN validation and MCUboot COMMIT validation all use the same
  effective FlashDB-backed identity. Host and device continue to validate the
  signed protected compatibility TLV independently.

## Consequences

- The Rust CLI exposes `config get` and a named-argument `config set`; GUI
  provisioning is deferred until a production workflow demonstrates that it is
  needed.
- Boot and Application initialize FlashDB when their updater path starts. First
  initialization may format Reserved Flash even before the identity is
  provisioned.
- All future updater HIL backup/restore procedures must include the full
  `0x00000000-0x0007FFFF` Flash range. Historical evidence that intentionally
  excluded Reserved remains historical and must not be reused as a recovery
  recipe for this node.
- Target HIL passed on 2026-08-29: HC32 erase/program behavior, Boot-only
  provisioning, physical power-cycle persistence, subsequent compatible
  installation, Application readback and exact full-Flash restoration are
  retained under `evidence/hil/2026-08-28-phase5-flashdb-final/`.

## Rejected alternatives

- Arbitrary Host KV read/write: rejected because it expands the USB trust
  boundary and exposes future secrets or policy keys.
- Rewritable board identity: rejected because changing identity could make a
  signed image for another board appear compatible.
- Store each identity field as a separate KV: rejected because interruption
  could leave a partially updated identity.
