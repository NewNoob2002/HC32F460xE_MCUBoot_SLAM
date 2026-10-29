# ADR-008: Separate Application settings from Product Config

Status: Accepted (2026-10-28)

`cmake/MemoryMap.cmake` divides Reserved (0x76000/0xA000) into
`product_kv` at offset 0/0x4000 and `app_kv` at offset 0x4000/0x6000.
Both partitions use 8 KiB erase sectors. The physical FAL device and MCUboot
Reserved area still cover all five sectors; the partitions are disjoint.
Boot opens only the `product` KVDB on `product_kv`. Application alone opens
`app` on `app_kv`, storing one versioned, CRC-checked settings blob.

The product partition keeps its start address. Existing Product Config remains
readable only when its live data is in the first two sectors. Live records in
old Reserved sectors 2–4 become inaccessible and may be erased by Application
settings initialization. This is not an automatic migration. Preserve device
identity before upgrading and re-provision via Boot Recovery if necessary.
Rollback to older firmware that treats all Reserved as product_kv can also
overwrite app_kv; settings are recoverable defaults, not safety authorization.

Charger settings are validated and published, but never applied to MP2762A
registers. CONFIG_LIST's applied-to-hardware flag remains clear. FlashDB writes
and all debug service work run in the cooperative poll context.
