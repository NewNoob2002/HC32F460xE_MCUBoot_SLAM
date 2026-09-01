# Product Config v3 HIL

Real HC32F460xE verification performed on 2026-09-01 with J-Link 63728710.

- Boot recovery enumerated as `cafe:0001`.
- One-time configuration persisted across a physical power cycle: `SN20260901A`, `A1.2`, `0x0020`.
- `updater_signed.bin` installed successfully and the App enumerated as `cafe:0020` with serial `SN20260901A`.
- `NusbTransport::open_by_serial("SN20260901A", DeviceMode::Application)` opened the exact re-enumerated device.
- Primary Flash matched the signed image and MCUboot recorded a confirmed permanent swap.
- The full 512 KiB pre-HIL Flash image was restored and matched byte-for-byte.
- A J-Link software reset did not restore USB enumeration. After a physical power cycle, the restored target returned to its exact pre-HIL `cafe:0001` Boot Recovery identity and original UQID serial.

The four full-Flash readbacks are intentionally excluded from the repository because they may contain device data. Their filenames and SHA-256 values remain in `manifest.yaml`; logs retain the backup, restore, and byte-for-byte verification results. Local copies are kept under the ignored `build/local-evidence-backup/2026-09-01-product-config-v3/` directory.

See `hil_test_result.yaml`, `deployment_result.yaml`, and `logs/`.
