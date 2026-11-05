#!/usr/bin/env python3
"""Keep synchronous App C++ portable and orchestration above the C BSP."""

from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
ARCH_CONFIG = ROOT / "Tests/Architecture/arch_layers.json"
SOURCE_DIRS = (ROOT / "App", ROOT / "Boot/Core")
PLATFORM_DIRS = (ROOT / "Platform/HC32F460/Inc", ROOT / "Platform/HC32F460/Src")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)
EXTENSIONS = {".h", ".hpp", ".c", ".cpp"}


def sources(directory):
    return sorted(path for path in directory.rglob("*") if path.suffix in EXTENSIONS)


violations = []
try:
    layer_config = json.loads(ARCH_CONFIG.read_text(encoding="utf-8"))
except (OSError, json.JSONDecodeError) as exc:
    raise SystemExit(f"cannot load architecture config: {exc}") from exc

required_layers = {"app", "module", "interface", "driver_device", "driver_bus", "bsp", "platform", "middleware"}
configured_layers = {layer["name"] for layer in layer_config.get("layers", []) if "name" in layer}
if required_layers - configured_layers:
    violations.append(f"architecture config is missing layers: {', '.join(sorted(required_layers - configured_layers))}")

for directory in SOURCE_DIRS:
    for path in sources(directory):
        for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
            name = include.replace("\\", "/").rsplit("/", 1)[-1].lower()
            if name.startswith(("hc32f460", "hc32_ll")):
                violations.append(f"{path.relative_to(ROOT)}: forbidden platform include '{include}'")

for directory in PLATFORM_DIRS:
    for path in sources(directory):
        for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
            if include.lower().rsplit("/", 1)[-1] in {"elog.h", "segger_rtt.h"}:
                violations.append(f"{path.relative_to(ROOT)}: forbidden logging dependency '{include}'")

# Framework includes resolve locally or name a C/C++ standard library header.
FRAMEWORK = ROOT / "App/Framework"
STANDARD = {"algorithm", "array", "cstddef", "cstdint", "cstring", "limits", "utility",
            "stdbool.h", "stddef.h", "stdint.h", "string.h"}
for path in sources(FRAMEWORK):
    for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
        local = (path.parent / include).resolve()
        if include not in STANDARD and not (local.is_relative_to(FRAMEWORK) and local.is_file()):
            violations.append(f"{path.relative_to(ROOT)}: framework include '{include}'")

# Catch full, relative and bare presentation/service headers in domain nodes.
SERVICE_HEADERS = {
    p.name.lower()
    for directory in ("App/Diagnostics", "App/Debug")
    for p in sources(ROOT / directory)
}
for path in sources(ROOT / "App/DataProc"):
    for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
        parts = include.replace("\\", "/").lower().split("/")
        if "diagnostics" in parts or "debug" in parts or parts[-1] in SERVICE_HEADERS:
            violations.append(f"{path.relative_to(ROOT)}: DataProc must not depend on Diagnostics/Debug")

# Business modules consume semantic HAL contracts, never board headers or device adapters.
for path in sources(ROOT / "App/DataProc"):
    for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
        name = include.replace("\\", "/").rsplit("/", 1)[-1].lower()
        if name.startswith(("bsp_", "hc32", "usbd_", "usb_dc_")):
            violations.append(f"{path.relative_to(ROOT)}: DataProc must not depend on platform implementation '{include}'")

# Public C/C++ contracts must not expose BSP, transport SDK or bootloader types.
PUBLIC_CONTRACTS = (
    ROOT / "App/Core/Inc",
    ROOT / "App/HAL/Status.h",
    ROOT / "App/HAL/I2cBus.h",
    ROOT / "App/HAL/PowerDevices.h",
    ROOT / "App/HAL/ExternalWatchdog.h",
    ROOT / "App/HAL/ChargerSignals.h",
    ROOT / "App/HAL/SettingsStore.h",
    ROOT / "App/HAL/BQ40Z50.h",
    ROOT / "App/HAL/ChargerDevices.h",
)
for item in PUBLIC_CONTRACTS:
    paths = sources(item) if item.is_dir() else (item,)
    for path in paths:
        for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
            name = include.replace("\\", "/").rsplit("/", 1)[-1].lower()
            if name.startswith(("bsp_", "hc32", "usbd_", "bootutil", "fal.h", "flashdb")):
                violations.append(f"{path.relative_to(ROOT)}: public contract leaks implementation '{include}'")

# App ownership is static/caller-owned; '= delete' only forbids copying.
for path in sources(ROOT / "App"):
    text = path.read_text(encoding="utf-8")
    text = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', "", text, flags=re.DOTALL)
    text = re.sub(r'=\s*delete\b', "", text)
    if re.search(r'\b(new|delete)\b|\b(malloc|calloc|realloc|free)\s*\(', text):
        violations.append(f"{path.relative_to(ROOT)}: App must not allocate/free heap memory")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Application/platform boundary check passed")
