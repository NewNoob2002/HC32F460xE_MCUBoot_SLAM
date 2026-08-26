# Verification Evidence

This directory contains small, immutable evidence bundles that must survive a
fresh clone. Build trees remain under ignored build directories.

HIL bundles keep exact programmed firmware, raw probe logs, normalized result
manifests and SHA-256 checksums. Host bundles keep exact commands, raw build/
test/analyzer logs, normalized results and source/spec/vector hashes. Full-device
Flash backups are excluded because they may contain device-specific or sensitive
data; their size and hash remain in the bundle manifest.

Verify every retained bundle with:

~~~sh
python3 Tests/HIL/verify_evidence.py
~~~

New runs must use a new directory below `evidence/hil/` or `evidence/host/`. Do
not overwrite historical evidence.
