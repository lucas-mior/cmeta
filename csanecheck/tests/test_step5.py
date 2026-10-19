#!/usr/bin/env python3
"""Coverage-enforcement regressions for Step 5."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import run_checker, write_file


class CsanecheckStep5Tests(unittest.TestCase):
    def test_clean_translation_unit_passes_required_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "clean.c", "int32 value;\n")
            result = run_checker(["--file", path, "--require-coverage",
                                  "--rule-coverage"], root)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_unsupported_syntax_fails_even_without_rule_violations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "unknown.c", "int32 f( {\n")
            baseline = run_checker(["--file", path], root)
            strict = run_checker(["--file", path, "--require-coverage",
                                  "--rule-coverage"], root)
            self.assertEqual(baseline.returncode, 0, baseline.stdout)
            self.assertEqual(strict.returncode, 1, strict.stdout)
            self.assertIn("error[COVERAGE-INCOMPLETE]", strict.stdout)
            self.assertIn("status=not_checked", strict.stdout)

    def test_invalid_preprocessor_structure_fails_required_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "invalid.c", "#if 1\nint32 x;\n")
            result = run_checker(["--file", path, "--require-coverage",
                                  "--rule-coverage"], root)
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_coverage_report_does_not_change_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "unknown.c", "int32 f( {\n")
            result = run_checker(["--file", path, "--rule-coverage"], root)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("rule_coverage", result.stdout)
            self.assertNotIn("error[COVERAGE-INCOMPLETE]", result.stdout)


if __name__ == "__main__":
    unittest.main()
