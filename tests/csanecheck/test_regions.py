#!/usr/bin/env python3
"""Check preprocessor-aware source regions without parsing C or macros."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
def project_files():
    """Include all root and cbase C sources, including nested Ryu headers."""
    roots = (ROOT.glob("*.c"), ROOT.glob("*.h"),
             (ROOT / "cbase").rglob("*.c"),
             (ROOT / "cbase").rglob("*.h"))
    return sorted({str(path.relative_to(ROOT))
                   for group in roots for path in group})



class PreprocessorRegionsTests(unittest.TestCase):
    def test_region_partition_and_original_source_ownership(self):
        files = project_files()
        self.assertIn("cbase/assertions.h", files)
        self.assertIn("cbase/ryu/ryu.h", files)
        self.assertIn("meta.h", files)
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "regions"
            compiler = os.environ.get("CC", "cc")
            subprocess.run([compiler, "-std=c11", "-DDEBUGGING=0", "-I.",
                            "-Icbase", "-o", str(executable),
                            "tests/csanecheck/preprocessor_regions.c", "-lm"],
                           cwd=ROOT, check=True, capture_output=True,
                           text=True, timeout=120)
            subprocess.run([str(executable), *project_files()], cwd=ROOT,
                           check=True, capture_output=True,
                           text=True, timeout=60)


if __name__ == "__main__":
    unittest.main(verbosity=2)
