#!/usr/bin/env python3
"""Run owned firmware formatting and clang-tidy using the real target compile database."""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OWNED = ("App/", "Boot/", "Config/", "Platform/HC32F460/", "components/fw_update/")
VENDORED = tuple("Platform/HC32F460/Ports/easylogger/" + name for name in ("src/", "inc/elog.h", "plugins/"))


def owned(path):
    try:
        relative = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return False
    return relative.startswith(OWNED) and not relative.startswith(VENDORED)


def gcc_system_paths(arguments):
    machine = [flag for flag in arguments[1:]
               if flag.startswith(("-mcpu=", "-march=", "-mfpu=", "-mfloat-abi=", "--sysroot="))
               or flag in {"-mthumb", "-marm"}]
    result = subprocess.run([arguments[0], *machine, "-E", "-x", "c++", "-v", "-"], input="", text=True,
                            capture_output=True, check=True, env={**os.environ, "LC_ALL": "C"})
    lines = result.stderr.splitlines()
    start = lines.index("#include <...> search starts here:") + 1
    end = lines.index("End of search list.", start)
    paths = [line.strip() for line in lines[start:end] if Path(line.strip()).is_dir()]
    if not paths:
        raise RuntimeError("GCC did not report any C++ system include paths")
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/Debug")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    sources = {}
    for entry in entries:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        path = path.resolve()
        if owned(path) and path.suffix in {".c", ".cpp"}:
            sources[path] = entry
    if not sources:
        raise RuntimeError("No owned firmware sources found in the compilation database")
    headers = {path for prefix in OWNED for path in (ROOT / prefix).rglob("*.h") if owned(path)}
    files = sorted(set(sources) | headers)
    subprocess.run(["clang-format", "--dry-run", "--Werror", *map(str, files)], check=True)
    for extension in (".c", ".cpp"):
        group = sorted(path for path in sources if path.suffix == extension)
        if not group:
            continue
        command = ["clang-tidy", *map(str, group), "-p", str(build), "--quiet"]
        if extension == ".cpp":
            entry = sources[group[0]]
            arguments = entry.get("arguments") or shlex.split(entry["command"])
            # clang-tidy does not discover the cross-GCC C++ headers itself.
            for directory in gcc_system_paths(arguments):
                command += ["--extra-arg=-isystem", "--extra-arg=" + directory]
        print(f"clang-tidy: {len(group)} owned {extension} sources", flush=True)
        subprocess.run(command, check=True)
    print(f"Firmware quality gates passed ({len(files)} formatted files)")


if __name__ == "__main__":
    main()
