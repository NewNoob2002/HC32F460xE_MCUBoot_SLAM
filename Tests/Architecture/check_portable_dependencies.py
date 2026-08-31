#!/usr/bin/env python3
"""Reject platform, MCUboot, transport, and hardware dependencies in portable fw_update code."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS_DIR = ROOT / "components"
ALLOWED_COMPONENT_ENTRIES = {
    "CMakeLists.txt",
    "FlashDB-2.2.0",
    "cherryusb",
    "fw_update",
    "mcuboot-2.4.0",
}
REQUIRED_PLATFORM_PORTS = (
    ROOT / "Platform/HC32F460/Ports/mcuboot",
    ROOT / "Platform/HC32F460/Ports/flashdb",
    ROOT / "Platform/HC32F460/Ports/easylogger",
)
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
unexpected_entries = sorted(path.name for path in COMPONENTS_DIR.iterdir() if path.name not in ALLOWED_COMPONENT_ENTRIES)
if unexpected_entries:
    violations.append(f"components/: unexpected platform or unclassified entries: {', '.join(unexpected_entries)}")
components_cmake = (COMPONENTS_DIR / "CMakeLists.txt").read_text(encoding="utf-8").lower()
if "platform/" in components_cmake:
    violations.append("components/CMakeLists.txt: physical platform paths are forbidden; depend on port targets")
for directory in REQUIRED_PLATFORM_PORTS:
    if not directory.is_dir():
        violations.append(f"{directory.relative_to(ROOT)}: required platform port is missing")

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

print("Component layout and portable fw_update dependency checks passed")
