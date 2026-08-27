# Phase 4 USB Loopback HIL Evidence

This bundle preserves the HC32F460 CherryUSB Vendor Bulk loopback evidence for
source revision `93c9c048380becbda1d655537bdbc095bf743ac5`. GitHub Actions
source run `33026124061` passed.

## Result

- Enumeration: PASS as `fffe:ffff`, product
  `HC32 Phase 4 Lab CherryUSB Vendor Bulk Loopback`.
- Host access: PASS at `/dev/bus/usb/001/011`, owner `gtc`, mode `0660`.
- Loopback: PASS, 10,000 transfers over lengths
  `0,1,63,64,65,512,1024` in 6.786 seconds.
- Target state: PASS, init result `0`, stage `0x400`, error count `0`, packet
  count `0x2710` (10,000).
- Restoration: PASS. The final `0x00000000-0x00075FFF` readback was
  byte-identical to the pre-HIL backup, SHA-256
  `e608d3eae3ea4a8c0010f3ee6de8fc361c9618f98733a2c769d72264c1667c98`.
  The loopback VID/PID no longer enumerated after restoration.

Attempts 1-6 were bounded bring-up or host-access diagnostics. Every attempt
restored the exact pre-HIL image before the next run. Attempt 7 is the accepted
10,000-transfer result after the host udev permission gate was configured.

## Gate status

This evidence closes the current enumeration/loopback node but does not pass
G4. Phase 4 is `READY_FOR_REVIEW`; G4 still requires a 30-minute continuous
run and at least 10 manual unplug/re-enumeration cycles.

## Retention policy

Exact programmed artifacts, raw J-Link command files/logs, host observations
and safety preflights are tracked. The full pre-HIL and post-restore images are
excluded because they may contain device-specific data; their sizes and hashes
are retained in `manifest.yaml`.

Verify this bundle with:

```sh
python3 Tests/HIL/verify_evidence.py evidence/hil/2026-08-26-b90ba7b-phase4-usb/SHA256SUMS
```
