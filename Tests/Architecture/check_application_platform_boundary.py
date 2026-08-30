#!/usr/bin/env python3
"""Keep application and boot orchestration behind the HC32 BSP boundary."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = (ROOT / "App/Core", ROOT / "Boot/Core")
FORBIDDEN = ('#include "hc32f460', '#include "hc32_ll')

violations = []
for directory in SOURCE_DIRS:
    for path in sorted(directory.rglob("*")):
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8").lower()
        for token in FORBIDDEN:
            if token in text:
                violations.append(f"{path.relative_to(ROOT)}: forbidden platform include '{token}'")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Application/platform boundary check passed")
