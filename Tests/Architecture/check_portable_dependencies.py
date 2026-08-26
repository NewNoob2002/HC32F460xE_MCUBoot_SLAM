#!/usr/bin/env python3
"""Reject platform, MCUboot, transport, and hardware dependencies in portable fw_update code."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
PORTABLE_DIRS = (
    ROOT / "components/fw_update/include",
    ROOT / "components/fw_update/src",
)
FORBIDDEN = (
    "hc32f460",
    "hc32_ll",
    "bsp_",
    "bootutil/",
    "flash_map_backend",
    "cherryusb",
    "usbd_",
)

violations = []
for directory in PORTABLE_DIRS:
    for path in sorted(directory.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8").lower()
        for token in FORBIDDEN:
            if token in text:
                violations.append(f"{path.relative_to(ROOT)}: forbidden dependency '{token}'")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Portable fw_update dependency check passed")
