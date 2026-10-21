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
    def test_conditional_body_diagnostics_in_all_branches(self):
        source = ("int32 guarded(int32 x) {\n"
                  "#if ALTERNATIVE\n"
                  "    if (x)\n        return sizeof(x);\n"
                  "#else\n"
                  "    if (x)\n        return sizeof(x);\n"
                  "#endif\n    return 0;\n}\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "guarded.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--require-coverage",
                                     "--report-parse-issues"],
                                    cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(result.stdout.count("error[AST-BRACES]"), 2,
                         result.stdout)
        self.assertEqual(result.stdout.count("error[PP-SIZEOF]"), 2,
                         result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertIn("unresolved=0", result.stdout)

    def test_time_source_guards_have_no_artificial_externals(self):
        result = subprocess.run([str(ROOT / "bin/csanecheck"), "--file",
                                 "cbase/time.c", "--coverage",
                                 "--report-parse-issues", "--strict-parse"],
                                cwd=ROOT, capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertIn("structural_issues=0", result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)

    def test_construct_spanning_branches_stays_unresolved(self):
        # A brace that opens in either alternative closes after #endif.
        # This needs contextual token streams, not silent exclusion.
        source = ("void guarded(void) {\n"
                  "#if A\n    if (a) {\n"
                  "#else\n    if (b) {\n"
                  "#endif\n        work();\n    }\n}\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "split.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--report-parse-issues",
                                     "--require-coverage"], cwd=ROOT,
                                    capture_output=True, text=True,
                                    check=False)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

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
