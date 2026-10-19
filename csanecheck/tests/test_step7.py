#!/usr/bin/env python3
"""Step 7: unresolved translation-unit macro recovery regressions."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import run_checker, write_file


class CsanecheckStep7Tests(unittest.TestCase):
    def scan(self, source, *options):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "sample.c", source)
            return run_checker(["--file", path, "--coverage",
                                "--report-parse-issues", *options], root)

    def test_distinct_unexpanded_macro_invocations_are_not_merged(self):
        result = self.scan(
            "#define MAKE_DECL(X) bool a_##X(char *, int32);\n"
            "MAKE_DECL(alpha)\n"
            "MAKE_DECL(beta)\n"
            "int32 ordinary;\n")
        self.assertIn("unsupported_issues=2", result.stdout)
        self.assertIn("malformed_issues=0", result.stdout)
        self.assertIn("declarations=1", result.stdout)

    def test_unexpanded_macro_does_not_claim_complete_coverage(self):
        result = self.scan(
            "#define MAKE_DECL(X) bool a_##X(char *, int32);\n"
            "MAKE_DECL(alpha)\n"
            "int32 ordinary;\n", "--require-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_genuinely_malformed_declaration_remains_malformed(self):
        result = self.scan("int32 broken( ;\nint32 ordinary;\n")
        self.assertIn("malformed_issues=1", result.stdout)

    def test_ordinary_macro_call_inside_function_is_not_external(self):
        result = self.scan(
            "#define DO_IT(X) (X)\n"
            "int32 test(void) {\n"
            "    return DO_IT(1);\n"
            "}\n")
        self.assertIn("unsupported_externals=0", result.stdout)


if __name__ == "__main__":
    unittest.main()
