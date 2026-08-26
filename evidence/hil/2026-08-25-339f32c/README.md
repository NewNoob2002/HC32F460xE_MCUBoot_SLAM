# HC32F460 MCUboot Baseline HIL Evidence

Status: PRESERVED

Test date: 2026-08-25

Firmware source revision: 339f32cc0145ecdc71878c68a8cb4af543be2920

Firmware implementation baseline: ac8c03844d706937ec068357395e3bdf8bd9b676

Evidence freeze prepared from repository revision:
a004c63bd34745bdbd23b7d1888e8153d32f0fab

The bundle retains the exact four binaries programmed during the successful
rollback HIL, raw J-Link logs, historical command files, normalized manifests,
and checksums. Historical command files intentionally preserve the original
workstation paths and are evidence only. Use Tests/HIL/templates for a new run.

The pre-test Flash backup is deliberately excluded because a device dump may
contain device-specific or sensitive data. Its retained metadata is:

- Original size: 483328 bytes.
- SHA-256: 60e089f7542b41156d24f1e65bbb14332574a7b1bd3118ea882bf47211f80f7f.
- Original range: 0x00000000-0x00075FFF.

Verify the retained files from the repository root:

~~~sh
python3 Tests/HIL/verify_evidence.py evidence/hil/2026-08-25-339f32c/SHA256SUMS
~~~
