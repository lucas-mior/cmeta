#!/usr/bin/env python3
"""Native tests for independent preprocessor source regions."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "csanecheck/tests/regions_test.c"


class SourceRegionTests(unittest.TestCase):
    def test_native_source_regions(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "regions_test"
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
