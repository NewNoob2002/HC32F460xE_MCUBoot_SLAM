# Boot Freeze v3 HIL

Boot recovery, MCUboot upgrade/revert and confirmation-persistence regression
ran on 2026-09-03 using J-Link 63728769 and the HC32F460xE target.

- With Boot only and no valid image, the target enumerated as `cafe:0001` and
  answered updater `info` in recovery mode.
- A signed, confirmed v1 Primary booted as Application `1.0.0`.
- An unconfirmed v2 Secondary booted once as `2.0.0`, then reverted to `1.0.0`
  on the next reset.
- An automatically confirmed v2 Secondary booted as `2.0.0` and remained
  `2.0.0` after another reset.
- `g_app_confirm_result` was zero and CFSR/HFSR were zero in the final state.

The board's Product Config remained intentionally unprovisioned. This run did
not write one-time manufacturing fields, so USB recovery was validated through
enumeration and read-only protocol commands while MCUboot upgrade behavior used
the approved slot-padded Secondary artifacts. Earlier retained evidence covers
provisioned USB install and exact-SN reconnect.

The pre-HIL `0x00000000-0x00075FFF` backup is retained only under the ignored
`build/local-evidence-backup/2026-09-03-boot-freeze-v3/` directory. Reserved
Flash and OTP were not accessed. The target was left running confirmed v2.
