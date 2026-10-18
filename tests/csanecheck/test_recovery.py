#!/usr/bin/env python3
"""Balanced recovery and partial-body checks without macro expansion."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.file = pathlib.Path(self.temp.name) / "sample.c"

    def scan(self, source):
        self.file.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(self.file), "--coverage",
             "--report-parse-issues"], capture_output=True, text=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stdout)
        return result.stdout

    def test_macro_named_function_with_conditional_parameters(self):
        result = self.scan(
            "#define CAT(a, b) a##b\n"
            "static int32\nCAT(helper_, NAME)(int32 n\n"
            "#if MORE\n, int32 x\n#else\n, int32 y\n#endif\n) {\n"
            "    int32 a = STRLIT_LEN(\"bad\");\n"
            "    if (n) { return STRLIT_LEN(\"also bad\"); }\n"
            "    return a;\n}\n"
            "int32 following(void) { return STRLIT_LEN(\"bad3\"); }\n"
        )
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 3)
        self.assertIn("unsupported_externals=1", result)
        self.assertIn("malformed_issues=0", result)
        self.assertIn("function_definitions=1", result)

    def test_recover_after_invalid_header(self):
        result = self.scan(
            "int wrong( {\n    unknown();\n}\n"
            "int32 valid(void) { return STRLIT_LEN(\"bad\"); }\n"
        )
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)
        self.assertIn("parse_issues=1", result)
        self.assertIn("function_definitions=1", result)

    def test_conditional_control_condition_keeps_body(self):
        result = self.scan(
            "void f(int32 n) {\n"
            "    if (\n#if FLAG\n        n\n#else\n"
            "        n + 1\n#endif\n    ) {\n"
            "        STRLIT_LEN(\"bad\");\n    }\n}\n"
        )
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)
        self.assertIn("malformed_issues=0", result)
        self.assertIn("unsupported_issues=1", result)
        self.assertNotIn("error[AST-ARTIFICIAL-BLOCK]", result)

    def test_conditional_for_header_keeps_body(self):
        result = self.scan(
            "void f(int32 n) {\n    for (\n"
            "#if FLAG\n        int32 i = 0; i < n; i += 1\n"
            "#else\n        int32 j = 0; j < n; j += 1\n"
            "#endif\n    ) {\n"
            "        STRLIT_LEN(\"bad\");\n    }\n}\n"
        )
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)
        self.assertIn("malformed_issues=0", result)
        self.assertIn("unsupported_issues=1", result)

    def test_shared_body_after_conditional_if(self):
        result = self.scan(
            "void f(int32 n) {\n    if (n) {\n"
            "#if FLAG\n        if (n)\n#else\n"
            "        if (n + 1)\n#endif\n        {\n"
            "            STRLIT_LEN(\"bad\");\n"
            "        }\n    }\n}\n"
        )
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)
        self.assertIn("malformed_issues=0", result)
        self.assertIn("unsupported_issues=2", result)
        self.assertNotIn("error[AST-ARTIFICIAL-BLOCK]", result)

    def test_non_conditional_artificial_block_still_fails(self):
        result = self.scan(
            "void f(void) {\n    if (1) {\n        if (1) {\n"
            "            { STRLIT_LEN(\"bad\"); }\n"
            "        }\n    }\n}\n"
        )
        self.assertIn("error[AST-ARTIFICIAL-BLOCK]", result)
        self.assertEqual(result.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_opaque_header_preserves_pointer_vs_multiplication(self):
        result = self.scan(
            "static int32\nCAT(func_, NAME)(int32 n\n"
            "#if MORE\n, int32 x\n#endif\n) {\n"
            "    int32 *ptr = 0;\n    int32 a = n * n;\n"
            "    return a;\n}\n"
        )
        self.assertEqual(result.count("error[FMT-MULTIPLY]"), 1)
        self.assertNotIn("error[AST-BRACES]", result)

    def test_unbalanced_body_is_not_fabricated_as_function(self):
        result = self.scan(
            "int32 CAT(bad_, NAME)(int32 n) {\n"
            "    STRLIT_LEN(\"bad\");\n"
        )
        self.assertNotIn("error[STYLE-STRLIT-LEN]", result)
        self.assertRegex(result, r"parse_issues=[1-9]")


if __name__ == "__main__":
    unittest.main(verbosity=2)
