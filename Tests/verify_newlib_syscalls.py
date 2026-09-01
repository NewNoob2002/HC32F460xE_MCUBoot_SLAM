#!/usr/bin/env python3
import re
import sys
from pathlib import Path

SYSCALLS = (
    "_close",
    "_exit",
    "_fstat",
    "_getpid",
    "_isatty",
    "_kill",
    "_lseek",
    "_read",
    "_sbrk",
    "_write",
)


def main() -> int:
    if len(sys.argv) == 1:
        print("usage: verify_newlib_syscalls.py <firmware.map>...", file=sys.stderr)
        return 2

    failures = []
    for argument in sys.argv[1:]:
        path = Path(argument)
        contents = path.read_text(encoding="utf-8", errors="replace")
        if "libnosys.a(" in contents:
            failures.append(f"{path}: libnosys fallback linked")
        for syscall in SYSCALLS:
            pattern = rf"^ \.text\.{re.escape(syscall)}\s+0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+.*syscalls\.c\.(?:o|obj)$"
            if re.search(pattern, contents, re.MULTILINE) is None:
                failures.append(f"{path}: {syscall} is not provided by hc32_newlib")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(sys.argv) - 1} firmware maps use hc32_newlib")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
