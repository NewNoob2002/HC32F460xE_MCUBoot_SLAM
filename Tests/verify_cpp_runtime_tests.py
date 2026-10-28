#!/usr/bin/env python3
"""Regression checks for the App map gate, including discarded input sections."""
import unittest
from verify_newlib_syscalls import cpp_runtime_failures


class CppRuntimeGateTests(unittest.TestCase):
    def test_rejects_all_forbidden_runtime_families(self):
        symbols = (
            "_Znwj", "_Znaj", "_Znwm", "_Znam", "_ZdlPv", "_ZdaPvj",
            "operator new(unsigned int)", "operator delete[](void*)",
            "__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort",
            "__gxx_personality_v0", "__cxa_throw", "__cxa_rethrow",
            "__cxa_allocate_exception", "__cxa_begin_catch", "_ZTI7Example",
            "_ZTS7Example", "__dynamic_cast", "_ZTVN10__cxxabiv117__class_type_infoE",
            "libstdc++.a(eh_alloc.o)", "libstdc++_nano.a(tinfo.o)",
        )
        for symbol in symbols:
            with self.subTest(symbol=symbol):
                self.assertTrue(cpp_runtime_failures("Linker script and memory map\n" + symbol))

    def test_allows_plain_cpp_and_discarded_runtime(self):
        self.assertFalse(cpp_runtime_failures(
            "Discarded input sections\n.text._Znwj\n"
            "Linker script and memory map\n.text._ZN10DataBroker12handleTimersEj\n"
            "DataNode::onEvent\nmemcpy\n"))


if __name__ == "__main__":
    unittest.main()
