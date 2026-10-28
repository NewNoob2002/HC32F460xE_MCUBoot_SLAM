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

# The DataBroker framework stays portable: only its own header and the C library.
FRAMEWORK_DIR = ROOT / "App/Framework"
FRAMEWORK_ALLOWED = ('"data_broker.h"', "<stdbool.h>", "<stddef.h>", "<stdint.h>", "<string.h>")
for path in sorted(FRAMEWORK_DIR.rglob("*")):
    if not path.is_file():
        continue
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("#include") and not stripped.endswith(FRAMEWORK_ALLOWED):
            violations.append(f"{path.relative_to(ROOT)}: framework include '{stripped}'")

# Domain nodes publish; consumers such as Diagnostics subscribe. Never the reverse.
for path in sorted((ROOT / "App/DataProc").rglob("*")):
    if path.is_file() and '#include "app_diagnostics.h"' in path.read_text(encoding="utf-8"):
        violations.append(f"{path.relative_to(ROOT)}: DataProc must not depend on Diagnostics")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Application/platform boundary check passed")
