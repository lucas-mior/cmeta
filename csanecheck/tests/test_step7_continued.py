#!/usr/bin/env python3
"""Additional translation-unit macro recovery tests."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import run_checker, write_file


class CsanecheckStep7ContinuedTests(unittest.TestCase):
    def scan(self, source, *options):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "sample.c", source)
            return run_checker(["--file", path, "--coverage",
                                "--report-parse-issues", *options], root)

    def test_semicolon_terminated_macro_is_isolated(self):
        result = self.scan(
            "#define MAKE(X) int32 a_##X;\n"
            "MAKE(alpha);\n"
            "int32 ordinary;\n")
        self.assertIn("unsupported_externals=1", result.stdout)
        self.assertIn("malformed_issues=0", result.stdout)
        self.assertIn("declarations=2", result.stdout)

    def test_same_line_after_macro_is_preserved(self):
        result = self.scan(
            "#define MAKE(X) int32 a_##X;\n"
            "MAKE(alpha); int32 ordinary;\n")
        self.assertIn("unsupported_externals=1", result.stdout)
        self.assertIn("malformed_issues=0", result.stdout)

    def test_unresolved_macro_still_fails_required_coverage(self):
        result = self.scan(
            "#define MAKE(X) int32 a_##X;\n"
            "MAKE(alpha);\n", "--require-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_undef_does_not_presume_macro_still_defined(self):
        result = self.scan(
            "#define MAKE(X) int32 a_##X;\n"
            "#undef MAKE\n"
            "MAKE(alpha)\n"
            "int32 ordinary;\n")
        self.assertNotIn("unsupported_issues=1", result.stdout)
        self.assertIn("malformed_issues=1", result.stdout)

    def test_redefinition_after_undef_is_recognized(self):
        result = self.scan(
            "#define MAKE(X) int32 a_##X;\n"
            "#undef MAKE\n"
            "#define MAKE(X) int32 b_##X;\n"
            "MAKE(alpha)\n"
            "int32 ordinary;\n")
        self.assertIn("malformed_issues=0", result.stdout)
        self.assertIn("declarations=2", result.stdout)


if __name__ == "__main__":
    unittest.main()
