#!/usr/bin/env python3
"""Compile and exercise the independent checker token-navigation layer."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TokenNavigationTests(unittest.TestCase):
    def test_navigation_and_legacy_tokenizer_compatibility(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "navigation"
            compiler = os.environ.get("CC", "cc")
            subprocess.run([compiler, "-std=c11", "-DDEBUGGING=0", "-I.",
                            "-Icbase", "-o", str(executable),
                            "tests/csanecheck/token_navigation.c", "-lm"],
                           cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=120)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=30)


if __name__ == "__main__":
    unittest.main(verbosity=2)
