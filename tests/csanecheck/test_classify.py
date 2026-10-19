#!/usr/bin/env python3
"""Test local convention-based classification without the AST parser."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ClassifierTests(unittest.TestCase):
    def test_classifier(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "classify"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-DDEBUGGING=0", "-I.", "-Icbase", "-o",
                            str(executable),
                            "tests/csanecheck/classify_constructs.c", "-lm"],
                           cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=120)
            subprocess.run([str(executable)], cwd=ROOT, check=True,
                           capture_output=True, text=True, timeout=120)


if __name__ == "__main__":
    unittest.main(verbosity=2)
