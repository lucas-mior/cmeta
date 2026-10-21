#!/usr/bin/env python3
"""Native regression tests for fault-tolerant function/statement scanners."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "csanecheck/tests/structure_test.c"


class StructureTests(unittest.TestCase):
    def test_native_structure_scanning(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "structure_test"
            command = [*compiler, "-std=c11", "-I.", "-Icbase", "-o",
                       str(binary), str(SOURCE), "-lm"]
            build = subprocess.run(command, cwd=ROOT, capture_output=True,
                                   text=True, check=False)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], cwd=ROOT,
                                    capture_output=True, text=True,
                                    check=False)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
