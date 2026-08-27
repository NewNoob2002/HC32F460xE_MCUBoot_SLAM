#!/usr/bin/env python3
"""Keep the Phase 4 USB loopback outside update, storage, and MCUboot code."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
USB_FILES = (
    ROOT / "App/Core/Src/usb_vendor_bulk.c",
    ROOT / "Platform/HC32F460/Src/usb_dc_hc32f460.c",
)
FORBIDDEN = ("fw_update", "bootutil", "flash_map", "storage_", "mcuboot")

violations = []
for path in USB_FILES:
    text = path.read_text(encoding="utf-8").lower()
    for token in FORBIDDEN:
        if token in text:
            violations.append(f"{path.relative_to(ROOT)}: forbidden dependency '{token}'")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("USB evaluation boundary check passed")
