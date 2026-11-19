#!/usr/bin/env python3
"""Keep synchronous App C++ portable and orchestration above the C BSP."""

from pathlib import Path
from fnmatch import fnmatchcase
from graphlib import CycleError, TopologicalSorter
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


def read_source(path):
    text = path.read_text(encoding="utf-8")
    tokens = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*'
    return re.sub(tokens, lambda match: match[0] if match[0][0] in {'"', "'"}
                  else "\n" * match[0].count("\n"), text, flags=re.DOTALL)


def code_only(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', "", text, flags=re.DOTALL)


def submodule(path, layer):
    relative = path.relative_to(ROOT).as_posix()
    for pattern in layer["globs"]:
        if pattern.endswith("/**") and relative.startswith(pattern[:-3] + "/"):
            prefix = pattern[:-3]
            rest = relative[len(prefix) + 1:]
            return prefix + "/" + (rest.split("/", 1)[0] if "/" in rest else Path(rest).stem)
    return relative


violations = []
try:
    layer_config = json.loads(ARCH_CONFIG.read_text(encoding="utf-8"))
except (OSError, json.JSONDecodeError) as exc:
    raise SystemExit(f"cannot load architecture config: {exc}") from exc

required_layers = {"app", "module", "interface", "driver_device", "driver_bus", "bsp", "platform", "middleware"}
configured_layers = {layer["name"] for layer in layer_config.get("layers", []) if "name" in layer}
if required_layers - configured_layers:
    violations.append(f"architecture config is missing layers: {', '.join(sorted(required_layers - configured_layers))}")

# Execute the checked-in include rules in CI without requiring the external skill installation.
layers = {layer["name"]: layer for layer in layer_config["layers"]}
mapped = {}
headers = {}
for layer in layer_config["layers"]:
    for pattern in layer["globs"]:
        for path in ROOT.glob(pattern):
            candidates = path.rglob("*") if path.is_dir() else (path,)
            for candidate in candidates:
                if not candidate.is_file() or candidate.suffix not in EXTENSIONS:
                    continue
                relative = candidate.relative_to(ROOT).as_posix()
                if any(fnmatchcase(relative, excluded) for excluded in layer_config["exclude"]):
                    continue
                # First match owns an overlapping glob, as in the skill's arch_lint.
                mapped.setdefault(candidate.resolve(), layer["name"])
for path in mapped:
    if path.suffix in {".h", ".hpp"}:
        headers.setdefault(path.name, []).append(path)

dependencies = {}
for path, layer_name in mapped.items():
    layer = layers[layer_name]
    allowed = set(layer["allow"])
    text = read_source(path)
    for include in INCLUDE.findall(text):
        local = (path.parent / include).resolve()
        candidates = [local] if local in mapped else headers.get(Path(include).name, [])
        if candidates and not all(mapped[candidate] in allowed for candidate in candidates):
            violations.append(f"{path.relative_to(ROOT)}: {layer_name} cannot include '{include}'")
        if layer_name in layer_config["cycle_check_layers"]:
            source = submodule(path, layer)
            for candidate in candidates:
                if mapped[candidate] == layer_name:
                    target = submodule(candidate, layer)
                    if source != target:
                        dependencies.setdefault(source, set()).add(target)
        # An owned, allowed contract may share a name with a vendor pattern (usb_*.h).
        if candidates and all(mapped[candidate] in allowed for candidate in candidates):
            continue
        forbidden = []
        for pattern in layer["forbid_includes"]:
            forbidden.extend(layer_config["include_sets"][pattern[1:]] if pattern.startswith("@") else [pattern])
        if any(fnmatchcase(include, pattern) or fnmatchcase(Path(include).name, pattern) for pattern in forbidden):
            violations.append(f"{path.relative_to(ROOT)}: forbidden layer include '{include}'")
    for rule in layer["forbid_code"]:
        if re.search(layer_config["code_patterns"][rule], code_only(text)):
            violations.append(f"{path.relative_to(ROOT)}: forbidden layer code '{rule}'")

try:
    TopologicalSorter(dependencies).prepare()
except CycleError as exc:
    violations.append("module dependency cycle: " + " <-> ".join(exc.args[1]))

for directory in SOURCE_DIRS:
    for path in sources(directory):
        for include in INCLUDE.findall(read_source(path)):
            name = include.replace("\\", "/").rsplit("/", 1)[-1].lower()
            if name.startswith(("hc32f460", "hc32_ll")):
                violations.append(f"{path.relative_to(ROOT)}: forbidden platform include '{include}'")

for directory in PLATFORM_DIRS:
    for path in sources(directory):
        for include in INCLUDE.findall(read_source(path)):
            if include.lower().rsplit("/", 1)[-1] in {"elog.h", "segger_rtt.h"}:
                violations.append(f"{path.relative_to(ROOT)}: forbidden logging dependency '{include}'")

# Framework includes resolve locally or name a C/C++ standard library header.
FRAMEWORK = ROOT / "App/Framework"
STANDARD = {"algorithm", "array", "cstddef", "cstdint", "cstring", "limits", "utility",
            "stdbool.h", "stddef.h", "stdint.h", "string.h"}
for path in sources(FRAMEWORK):
    for include in INCLUDE.findall(read_source(path)):
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
    for include in INCLUDE.findall(read_source(path)):
        parts = include.replace("\\", "/").lower().split("/")
        if "diagnostics" in parts or "debug" in parts or parts[-1] in SERVICE_HEADERS:
            violations.append(f"{path.relative_to(ROOT)}: DataProc must not depend on Diagnostics/Debug")

# Business modules consume semantic HAL contracts, never board headers or device adapters.
for path in sources(ROOT / "App/DataProc"):
    for include in INCLUDE.findall(read_source(path)):
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
)
for item in PUBLIC_CONTRACTS:
    paths = sources(item) if item.is_dir() else (item,)
    for path in paths:
        for include in INCLUDE.findall(read_source(path)):
            name = include.replace("\\", "/").rsplit("/", 1)[-1].lower()
            if name.startswith(("bsp_", "hc32", "usbd_", "bootutil", "fal.h", "flashdb")):
                violations.append(f"{path.relative_to(ROOT)}: public contract leaks implementation '{include}'")

# App ownership is static/caller-owned; '= delete' only forbids copying.
for path in sources(ROOT / "App"):
    text = code_only(read_source(path))
    text = re.sub(r'=\s*delete\b', "", text)
    if re.search(r'\b(new|delete)\b|\b(malloc|calloc|realloc|free)\s*\(', text):
        violations.append(f"{path.relative_to(ROOT)}: App must not allocate/free heap memory")

if violations:
    print("\n".join(violations), file=sys.stderr)
    raise SystemExit(1)

print("Application/platform boundary check passed")
