"""Require the production boundary gate to reject concrete domain dependencies."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def populate(root):
    for path in list((ROOT / "App/HAL").glob("*.h")) + list((ROOT / "App/Core/Inc").glob("*.h")):
        dest = root / path.relative_to(ROOT)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, dest)
    for folder in ("App/Core/Inc", "App/DataProc/Src", "Tests/Architecture"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for name in ("arch_layers.json", "check_application_platform_boundary.py"):
        shutil.copyfile(ROOT / "Tests/Architecture" / name, root / "Tests/Architecture" / name)


def run_gate(root):
    command = [sys.executable, str(root / "Tests/Architecture/check_application_platform_boundary.py")]
    return subprocess.run(command, capture_output=True, text=True, timeout=10)


class LayerBoundaryTests(unittest.TestCase):
    def test_domain_dependencies(self):
        cases = (
            ('#include "PowerDevices.h"\n', True),
            ('#include "BQ40Z50.h"\n', False),
            ('#include "ChargerDevices.h"\n', False),
            ('#include "../../HAL/BQ40Z50.h"\n', False),
            ('#include "bsp_i2c2.h"\n', False),
            ('#include "Hc32I2cBus.h"\n', False),
            ('void poll() { HAL_Transfer(); }\n', False),
            ('void poll() { PERIPHERAL->CONTROL = 1; }\n', False),
            ('// HAL_Transfer(); PERIPHERAL->CONTROL = 1;\n', True),
            ('/* Example:\n#include "BQ40Z50.h"\n*/\n', True),
            ('const char* example = "HAL_Transfer(); PERIPHERAL->CONTROL";\n', True),
        )
        for source, accepted in cases:
            with self.subTest(source=source), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                populate(root)
                (root / "App/DataProc/Src/domain.cpp").write_text(source, encoding="utf-8")
                result = run_gate(root)
                self.assertEqual(result.returncode, 0 if accepted else 1, result.stdout + result.stderr)

    def test_contract_cannot_include_driver(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            populate(root)
            path = root / "App/HAL/PowerDevices.h"
            path.write_text(path.read_text() + '\n#include "BQ40Z50.h"\n', encoding="utf-8")
            result = run_gate(root)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("interface cannot include", result.stderr)

    def test_module_cycle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            populate(root)
            for module in ("first", "second"):
                (root / "App/DataProc" / module).mkdir()
            first = root / "App/DataProc/first/first.h"
            second = root / "App/DataProc/second/second.h"
            first.write_text('#include "../second/second.h"\n', encoding="utf-8")
            second.write_text('#include "../first/first.h"\n', encoding="utf-8")
            result = run_gate(root)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("module dependency cycle", result.stderr)
            second.write_text("", encoding="utf-8")
            result = run_gate(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
