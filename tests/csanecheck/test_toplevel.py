#!/usr/bin/env python3
"""Ensure independent top-level decomposition preserves all code tokens."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def project_files():
    roots = (ROOT.glob("*.c"), ROOT.glob("*.h"),
             (ROOT / "cbase").rglob("*.c"),
             (ROOT / "cbase").rglob("*.h"))
    return sorted({str(path.relative_to(ROOT))
                   for group in roots for path in group})


class TopLevelTests(unittest.TestCase):
    def test_structural_decomposition(self):
        files = project_files()
        self.assertIn("cbase/hash.c", files)
        self.assertIn("cbase/assertions.h", files)
        self.assertIn("cbase/ryu/ryu.h", files)
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "toplevel"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-DDEBUGGING=0", "-I.", "-Icbase",
                            "-o", str(executable),
                            "tests/csanecheck/toplevel_constructs.c", "-lm"],
                           cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=120)
            subprocess.run([str(executable), *files], cwd=ROOT,
                           check=True, capture_output=True,
                           text=True, timeout=90)


if __name__ == "__main__":
    unittest.main(verbosity=2)
