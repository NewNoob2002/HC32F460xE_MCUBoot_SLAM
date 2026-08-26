# Phase 2 Storage and Boot-Control HIL Evidence

This bundle preserves the HC32F460 HIL evidence for source revision
`5ebeae6686ba83bce6b6bb366796d5943caf36a3`. GitHub Actions run
`32928199086` passed before target Flash was modified.

## Result

- Storage backend: PASS. The target reported logical capacity `0x30000`, wrote/read
  `0x13579BDF` at logical offset 0 and `0x2468ACE0` at `0x2FFFC`, and left
  the reserved MCUboot trailer image range beginning at `0x00072000` erased.
- Boot-Control: PASS. A valid unpadded signed image remained byte-identical in
  Secondary while `boot_set_pending_multi(0, 0)` wrote TEST swap-info `0x02`
  at `0x00073FD8` and MCUboot magic at `0x00073FF0`.
- Range isolation: PASS. Scratch SHA-256 remained
  `427d370374035b1695a025b812fbf3716eb64ba98e610c566782fff3caaa122b`
  before, after Storage and after Pending. Reserved `0x00076000-0x0007FFFF`
  was neither read nor written.
- Restoration: PASS. The final `0x00000000-0x00075FFF` readback was byte-for-byte
  identical to the pre-HIL backup, SHA-256
  `3cadc91626a565bf3ee5454d21e29a58243f58f1ae1b118279d1c70e1f6a8dcf`.

The first Storage observation used a 5-second wait and caught the known Debug
Boot inside ECDSA verification. It was classified as an inconclusive harness
timing attempt, the target was restored, and the test was repeated with the
historically proven 30-second window.

## Debugger-visible result layout

| Symbol | Address | Storage result | Pending result |
| --- | --- | --- | --- |
| `g_phase2_hil_result` | `0x1FFF807C` | `0` | `0` |
| `g_phase2_hil_last_read` | `0x1FFF8080` | `0x2468ACE0` | `0` |
| `g_phase2_hil_first_read` | `0x1FFF8084` | `0x13579BDF` | `0` |
| `g_phase2_hil_capacity` | `0x1FFF8088` | `0x00030000` | `0` |
| `g_phase2_hil_stage` | `0x1FFF808C` | `0x600D0000` | `0x600D0000` |

## Retention policy

Exact programmed artifacts, raw J-Link command files and logs are tracked.
The full pre-HIL backup and full-range snapshots are excluded because they may
contain device-specific data; their sizes and hashes are preserved in
`manifest.yaml` and `region_hashes.yaml`.

Verify this bundle with:

```sh
python3 Tests/HIL/verify_evidence.py evidence/hil/2026-08-26-5ebeae6-phase2/SHA256SUMS
```
