#!/usr/bin/env python3
"""Token-paste regressions for macro syntax projection."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import run_checker, write_file


class TokenPasteTests(unittest.TestCase):
    def scan(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "sample.c", source)
            return run_checker(["--file", path, "--coverage",
                                "--report-parse-issues"], root)

    def test_identifier_pasted_with_decimal_suffix(self):
        result = self.scan(
            "#define NAME(N) value_##N\n"
            "int32 NAME(12);\n")
        self.assertIn("projected_macros=1", result.stdout)
        self.assertIn("malformed_issues=0", result.stdout)
        self.assertIn("declarations=1", result.stdout)

    def test_identifier_pasted_with_identifier(self):
        result = self.scan(
            "#define NAME(N) value_##N\n"
            "int32 NAME(alpha);\n")
        self.assertIn("projected_macros=1", result.stdout)
        self.assertIn("declarations=1", result.stdout)

    def test_identifier_pasted_with_string_remains_unresolved(self):
        result = self.scan(
            "#define NAME(N) value_##N\n"
            "int32 NAME(\"bad\");\n")
        self.assertIn("projected_macros=0", result.stdout)

    def test_builtin_cat_with_decimal_suffix(self):
        result = self.scan("int32 CAT(value_, 12);\n")
        self.assertIn("projected_macros=1", result.stdout)
        self.assertIn("declarations=1", result.stdout)


if __name__ == "__main__":
    unittest.main()
