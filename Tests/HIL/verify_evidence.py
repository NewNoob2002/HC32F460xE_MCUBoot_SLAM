#!/usr/bin/env python3

import hashlib
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_manifest(manifest: Path) -> list[str]:
    errors = []
    base = manifest.parent.resolve()
    listed = set()
    for line_number, raw_line in enumerate(manifest.read_text().splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            expected, relative = line.split(maxsplit=1)
        except ValueError:
            errors.append(f"{manifest}:{line_number}: invalid checksum line")
            continue
        relative = relative.lstrip("*")
        path = (base / relative).resolve()
        if path in listed:
            errors.append(f"{manifest}:{line_number}: duplicate file: {relative}")
            continue
        listed.add(path)
        if path != base and base not in path.parents:
            errors.append(f"{manifest}:{line_number}: path escapes bundle: {relative}")
        elif not path.is_file():
            errors.append(f"{manifest}:{line_number}: missing file: {relative}")
        elif sha256(path) != expected.lower():
            errors.append(f"{manifest}:{line_number}: checksum mismatch: {relative}")

    actual = {
        path.resolve()
        for path in base.rglob("*")
        if path.is_file() and path.resolve() != manifest.resolve()
    }
    for path in sorted(actual - listed):
        errors.append(f"{manifest}: file missing from checksum manifest: {path.relative_to(base)}")
    return errors


def main() -> int:
    manifests = [Path(arg) for arg in sys.argv[1:]]
    if not manifests:
        manifests = sorted(Path("evidence/hil").glob("*/SHA256SUMS"))
    if not manifests:
        print("No HIL evidence manifests found", file=sys.stderr)
        return 1

    errors = [error for manifest in manifests for error in verify_manifest(manifest)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1

    print(f"Verified {len(manifests)} HIL evidence bundle(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
