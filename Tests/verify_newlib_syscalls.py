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


# Check linked output sections rather than discarded input sections. GNU maps
# contain both mangled names and demangled aliases depending on linker options.
CPP_RUNTIME_PATTERNS = (
    r"\b_Zn[aw]",  # operator new / new[] (including aligned/nothrow variants)
    r"\b_Zd[la]",  # operator delete / delete[]
    r"operator (?:new|delete)\b",
    r"\b__cxa_guard_",
    r"\b__gxx_personality",
    r"\b__cxa_(?:throw|rethrow|allocate_exception|free_exception|begin_catch|end_catch)",
    r"libstdc\+\+[^\s(]*\((?:eh_|pure\.|tinfo|class_type_info|si_class_type_info|vmi_class_type_info)",
    r"\b_ZT[IS]",  # typeinfo objects/names require RTTI support
    r"\b__dynamic_cast\b",
    r"\b_ZTVN10__cxxabiv1\d+__\w*type_info",
)


def cpp_runtime_failures(contents: str) -> list[str]:
    linked = contents.split("Linker script and memory map", 1)[-1]
    return [pattern for pattern in CPP_RUNTIME_PATTERNS if re.search(pattern, linked)]


def main() -> int:
    if len(sys.argv) == 1:
        print("usage: verify_newlib_syscalls.py <firmware.map>...", file=sys.stderr)
        return 2

    failures = []
    for argument in sys.argv[1:]:
        path = Path(argument)
        contents = path.read_text(encoding="utf-8", errors="replace")
        # The second map is the App in the existing verification target.
        if path.name.startswith("app-"):
            for pattern in cpp_runtime_failures(contents):
                failures.append(f"{path}: forbidden App C++ runtime linked ({pattern})")
        if "libnosys.a(" in contents:
            failures.append(f"{path}: libnosys fallback linked")
        for syscall in SYSCALLS:
            pattern = rf"^ \.text\.{re.escape(syscall)}\s+0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+.*syscalls\.c\.(?:o|obj)$"
            if re.search(pattern, contents, re.MULTILINE) is None:
                failures.append(f"{path}: {syscall} is not provided by hc32_newlib")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(sys.argv) - 1} firmware maps use hc32_newlib; App C++ runtime is bounded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
