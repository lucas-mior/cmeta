#!/usr/bin/env python3
"""Source-preserving macro annotations and C11 static assertion coverage."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class AnnotationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = pathlib.Path(self.temp.name) / "sample.c"

    def scan(self, code):
        self.path.write_text(code, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(self.path), "--coverage",
             "--report-parse-issues"], text=True, capture_output=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stdout)
        return result.stdout

    def test_isolated_annotation_before_function(self):
        result = self.scan(
            "CY_AD_HELPER\nstatic double\n"
            "boiler_sat_temperature(double p_bar) {\n"
            "    return STRLIT_LEN(\"aaa\");\n}\n"
        )
        self.assertIn("function_definitions=1", result)
        self.assertIn("parse_issues=0", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_inline_unused_and_noreturn(self):
        result = self.scan(
            "INLINE int32 helper(void) {\n"
            "    return STRLIT_LEN(\"bbb\");\n}\n"
            "static int32 UNUSED\n"
            "another(void) {\n"
            "    return STRLIT_LEN(\"ccc\");\n}\n"
            "noreturn void fatal_error(int32 status);\n"
        )
        self.assertIn("function_definitions=2", result)
        self.assertIn("parse_issues=0", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 2)

    def test_trailing_function_attributes(self):
        result = self.scan(
            "void log_message(char *fmt, ...) ATTR_PRINTF(1, 2);\n"
            "int32 write_message(char *, ...) ATTR_PRINTF(1, 2);\n"
            "void f(void) __attribute__((unused));\n"
            "void g(void) __declspec(noreturn);\n"
        )
        self.assertIn("parse_issues=0", result)
        self.assertIn("function_declarations=4", result)

    def test_prefix_attributes_and_local_declaration(self):
        result = self.scan(
            "__attribute__((unused)) static int32\n"
            "check(void) {\n"
            "    int32 UNUSED value = STRLIT_LEN(\"abc\");\n"
            "    return value;\n}\n"
        )
        self.assertIn("function_definitions=1", result)
        self.assertIn("parse_issues=0", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_uppercase_variables_are_not_discarded(self):
        result = self.scan(
            "int32 LIMIT = 8;\n"
            "int32 f(void) {\n"
            "    int32 COUNT = LIMIT;\n"
            "    return COUNT;\n}\n"
        )
        self.assertIn("parse_issues=0", result)
        self.assertIn("function_definitions=1", result)
        self.assertNotIn("error[", result)

    def test_uppercase_type_macros_are_not_annotations(self):
        result = self.scan(
            "#define HASH_KEY_TYPE char\n"
            "#define HASH_VALUE_TYPE int32\n"
            "typedef struct Bucket {\n"
            "    HASH_KEY_TYPE *key;\n"
            "    HASH_VALUE_TYPE value;\n"
            "} Bucket;\n"
        )
        self.assertIn("parse_issues=0", result)
        self.assertNotIn("error[", result)

    def test_macro_expression_is_not_annotation(self):
        result = self.scan(
            "int32 f(int32 n) {\n"
            "    return MACRO_CALL(n) + STRLIT_LEN(\"abc\");\n}\n"
        )
        self.assertIn("parse_issues=0", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_file_and_block_static_assertions(self):
        result = self.scan(
            "_Static_assert(SIZEOF(int32) >= 4, \"size\");\n"
            "void f(void) {\n"
            "    _Static_assert(SIZEOF(int32) >= 4, \"size\");\n"
            "    STRLIT_LEN(\"abc\");\n}\n"
        )
        self.assertIn("parse_issues=0", result)
        self.assertIn("function_definitions=1", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_malformed_annotation_still_reports_issue(self):
        result = self.scan("void f(void) ATTR_PRINTF(1, 2;\n")
        self.assertNotIn("parse_issues=0", result)
        self.assertIn("PARSE-", result)

    def test_malformed_static_assert_still_reports_issue(self):
        result = self.scan("_Static_assert(1);\n")
        self.assertNotIn("parse_issues=0", result)
        result = self.scan("_Static_assert(1, \"ok\")\n")
        self.assertNotIn("parse_issues=0", result)


if __name__ == "__main__":
    unittest.main(verbosity=2)
