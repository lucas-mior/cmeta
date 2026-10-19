#!/usr/bin/env python3
"""Regressions for the bounds, status, and file-scope wrapping fixes."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import rule_counts, run_checker, write_file


class CsanecheckStep2Tests(unittest.TestCase):
    def check_rule(self, code, rule, expected, name="sample.c"):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, name, code)
            result = run_checker(["--file", source], root)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            self.assertEqual(rule_counts(result)[rule], expected,
                             result.stdout + result.stderr)

    def test_bounds_with_inline_comments_and_member_index(self):
        self.check_rule(
            "static void\n"
            "check(int32 i, int32 len) {\n"
            "    ASSERT_GE(data.items[i], 0); /* lower */\n"
            "    ASSERT_LT(data.items[i], len);\n"
            "}\n",
            "STYLE-ASSERT-BOUNDS", 1)

    def test_bounds_in_macro_replacement(self):
        self.check_rule(
            "#define CHECK_INDEX(i) do { \\\n"
            "    ASSERT_GE(i, 0);         \\\n"
            "    ASSERT_LT(i, LENGTH(data)); \\\n"
            "} while (0)\n",
            "STYLE-ASSERT-BOUNDS", 1)

    def test_bounds_do_not_cross_statements_or_blank_lines(self):
        cases = (
            "ASSERT_GE(i, 0);\n\nASSERT_LT(i, n);\n",
            "ASSERT_GE(i, 0); other_call();\nASSERT_LT(i, n);\n",
            "ASSERT_GE(i, 0);\nASSERT_LT(j, n);\n",
            "ASSERT_GE(i, 1);\nASSERT_LT(i, n);\n",
            "const char *s = \"ASSERT_GE(i, 0); ASSERT_LT(i, n);\";\n",
            "// ASSERT_GE(i, 0);\n// ASSERT_LT(i, n);\n",
        )
        for code in cases:
            with self.subTest(code=code):
                self.check_rule(code, "STYLE-ASSERT-BOUNDS", 0)

    def test_status_if_use_and_multiple_pairs(self):
        self.check_rule(
            "static int32\n"
            "check(void) {\n"
            "    int32 status;\n"
            "    status = first();\n"
            "    if (status != 0) {\n"
            "        return 1;\n"
            "    }\n"
            "    status = second();\n"
            "    return status;\n"
            "}\n",
            "STYLE-STATUS-TEMP", 1)

    def test_status_nested_scope_and_member_exclusion(self):
        self.check_rule(
            "static int32\n"
            "check(void) {\n"
            "    {\n"
            "        int32 status;\n"
            "        record.status = 3;\n"
            "        status = first();\n"
            "        return status;\n"
            "    }\n"
            "}\n",
            "STYLE-STATUS-TEMP", 1)

    def test_status_rejects_nonadjacent_and_shadowed_cases(self):
        cases = (
            "static int32\ncheck(void) {\n"
            "    int32 status;\n    status = first();\n"
            "    do_something();\n    return status;\n}\n",
            "static int32\ncheck(void) {\n"
            "    int32 status;\n    status += first();\n"
            "    return status;\n}\n",
            "static int32\ncheck(void) {\n"
            "    int32 status;\n    status = first(); other_call();\n"
            "    return status;\n}\n",
            "static int32\ncheck(void) {\n"
            "    int32 status;\n    status = first();\n"
            "    if (status != 0) {\n        int32 status;\n"
            "        return status;\n    }\n    return 0;\n}\n",
        )
        for code in cases:
            with self.subTest(code=code):
                self.check_rule(code, "STYLE-STATUS-TEMP", 0)

    def test_file_scope_declaration_wrapping(self):
        self.check_rule(
            "static int32 value =\n"
            "    42;\n",
            "FMT-ASSIGN-WRAP", 1)
        self.check_rule(
            "static char *message =\n"
            "    \"http://example.com\";\n",
            "FMT-ASSIGN-WRAP", 1)

    def test_file_scope_assignment_group_with_long_declaration(self):
        self.check_rule(
            "static int32 first =\n"
            "    1;\n"
            "static int32 really_long_identifier_with_some_extra_length = "
            "very_long_identifier_with_some_extra_length;\n"
            "static int32 third =\n"
            "    3;\n",
            "FMT-ASSIGN-WRAP", 0)
        self.check_rule(
            "static int32 first =\n"
            "    1;\n"
            "\n"
            "static int32 really_long_identifier_with_some_extra_length = "
            "very_long_identifier_with_some_extra_length;\n",
            "FMT-ASSIGN-WRAP", 1)

    def test_file_scope_initializer_braces_are_not_flattened(self):
        self.check_rule(
            "static int32 values[] =\n"
            "    {1, 2};\n",
            "FMT-ASSIGN-WRAP", 0)


if __name__ == "__main__":
    unittest.main()
