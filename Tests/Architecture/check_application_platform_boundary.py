#!/usr/bin/env python3
"""Keep orchestration above the BSP and logging frameworks out of the BSP."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = (ROOT / "App", ROOT / "Boot/Core")
FORBIDDEN = ('#include "hc32f460', '#include "hc32_ll')
PLATFORM_DIRS = (ROOT / "Platform/HC32F460/Inc", ROOT / "Platform/HC32F460/Src")
PLATFORM_FORBIDDEN = ('#include "elog.h"', '#include <elog.h>', '#include "segger_rtt.h"')

violations = []
for directory in SOURCE_DIRS:
    for path in sorted(directory.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8").lower()
        for token in FORBIDDEN:
            if token in text:
                violations.append(f"{path.relative_to(ROOT)}: forbidden platform include '{token}'")

for directory in PLATFORM_DIRS:
    for path in sorted(directory.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8").lower()
        for token in PLATFORM_FORBIDDEN:
            if token in text:
                violations.append(f"{path.relative_to(ROOT)}: forbidden logging dependency '{token}'")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Application/platform boundary check passed")
