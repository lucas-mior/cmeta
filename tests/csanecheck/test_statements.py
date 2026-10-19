#!/usr/bin/env python3
"""Function-local structural recognition across source and conditional code."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StatementTests(unittest.TestCase):
    def test_local_structure(self):
        roots = (ROOT.glob("*.c"), ROOT.glob("*.h"),
                 (ROOT / "cbase").rglob("*.c"),
                 (ROOT / "cbase").rglob("*.h"))
        files = sorted({str(path.relative_to(ROOT))
                        for group in roots for path in group})
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "statements"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-DDEBUGGING=0", "-I.", "-Icbase", "-o",
                            str(executable),
                            "tests/csanecheck/statement_constructs.c", "-lm"],
                           cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=120)
            subprocess.run([str(executable), *files], cwd=ROOT,
                           check=True, capture_output=True, text=True,
                           timeout=120)


if __name__ == "__main__":
    unittest.main(verbosity=2)
