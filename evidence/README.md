# Verification Evidence

This directory contains small, immutable evidence bundles that must survive a
fresh clone. Build trees remain under ignored build directories.

Each HIL bundle keeps the exact programmed firmware, raw probe logs, normalized
result manifests, and SHA-256 checksums. Full-device Flash backups are excluded
because they may contain device-specific or sensitive data; their size and hash
remain in the bundle manifest.

Verify every retained bundle with:

~~~sh
python3 Tests/HIL/verify_evidence.py
~~~

New HIL runs must use a new run directory. Do not overwrite historical evidence.
