#!/usr/bin/env python3
"""Bounded single-token macro projections and source provenance."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class MacroProjectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = pathlib.Path(self.temp.name) / "sample.c"

    def scan(self, source):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(self.path), "--coverage",
             "--report-parse-issues"], capture_output=True, text=True,
            timeout=30, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result)
        self.assertEqual(result.stderr, "")
        return result.stdout

    def test_function_paste_and_object_alias(self):
        report = self.scan(
            "#define NAME hello\n#define RET int32\n"
            "#define PASTE(a, b) a##b\n"
            "static RET\nPASTE(test_, NAME)(void) {\n"
            '    return STRLIT_LEN("bad");\n}\n'
        )
        self.assertIn("function_definitions=1", report)
        self.assertIn("parse_issues=0", report)
        self.assertRegex(report, r"projected_macros=[1-9]")
        self.assertEqual(report.count("error[STYLE-STRLIT-LEN]"), 1)

    def test_chained_aliases_and_cbase_cat(self):
        report = self.scan(
            "#define END final\n#define NAME END\n"
            "#define HEAD test_\n"
            "int32 CAT(HEAD, NAME)(void) {\n"
            '    return STRLIT_LEN("bad");\n}\n'
        )
        self.assertIn("function_definitions=1", report)
        self.assertIn("parse_issues=0", report)
        self.assertIn("error[STYLE-STRLIT-LEN]", report)

    def test_stringification_and_one_token_forwarding(self):
        report = self.scan(
            "#define QUOTE(x) #x\n#define FORWARD(x) x\n"
            "void function(void) {\n"
            '    STRLIT_LEN(QUOTE(word));\n'
            '    STRLIT_LEN(FORWARD("bad"));\n}\n'
        )
        self.assertIn("parse_issues=0", report)
        self.assertEqual(report.count("error[STYLE-STRLIT-LEN]"), 2)

    def test_alias_to_checked_function(self):
        report = self.scan(
            "#define CHECKER STRLIT_LEN\n"
            "void function(void) {\n"
            '    CHECKER("bad");\n}\n'
        )
        self.assertIn("projected_macros=1", report)
        self.assertIn("error[STYLE-STRLIT-LEN]", report)
        self.assertRegex(report, r"sample.c:3:5: error\[STYLE-STRLIT-LEN\]")

    def test_macro_defined_later_is_not_used_early(self):
        report = self.scan(
            "void function(void) { UNKNOWN(1); }\n"
            "#define UNKNOWN STRLIT_LEN\n"
        )
        self.assertIn("projected_macros=0", report)
        self.assertNotIn("error[STYLE-STRLIT-LEN]", report)

    def test_too_many_cat_arguments_are_not_truncated(self):
        report = self.scan(
            "int32 CAT(a,b,c,d,e,f,g,h,i)(void) { return 1; }\n"
        )
        self.assertIn("projected_macros=0", report)

    def test_undef_does_not_expand_old_definition(self):
        report = self.scan(
            "#define NAME before\n#undef NAME\n"
            "static int32 CAT(test_, NAME)(void) { return 1; }\n"
        )
        # CAT can paste the literal identifier NAME, but the removed
        # definition must not be substituted as 'before'.
        self.assertIn("parse_issues=0", report)
        self.assertIn("function_definitions=1", report)

    def test_conditional_definition_is_not_guessed(self):
        report = self.scan(
            "#if ENABLED\n#define NAME chosen\n#endif\n"
            "static int32 CAT(test_, NAME)(void) { return 1; }\n"
        )
        self.assertIn("projected_macros=0", report)

    def test_recursive_alias_does_not_expand(self):
        report = self.scan(
            "#define ONE TWO\n#define TWO ONE\n"
            "int32 CAT(test_, ONE)(void) { return 1; }\n"
        )
        self.assertIn("projected_macros=0", report)

    def test_multitoken_macro_kept_original(self):
        report = self.scan(
            "#define BODY return 1\n"
            "void function(void) {\n    BODY;\n}\n"
        )
        self.assertIn("function_definitions=1", report)
        self.assertNotIn("error[", report)

    def test_original_layout_not_generated_layout(self):
        report = self.scan(
            "#define NAME some_very_very_very_very_very_long_function_name\n"
            "int32 CAT(f_, NAME)(void) { return 1; }\n"
        )
        self.assertIn("function_definitions=1", report)
        self.assertNotIn("error[", report)

    def test_source_diagnostics_still_run_with_expansion(self):
        report = self.scan(
            "#define RET int32\n"
            "RET function(void) {   \n"
            '    return STRLIT_LEN("bad");\n}\n'
        )
        self.assertIn("error[SRC-TRAILING]", report)
        self.assertIn("error[STYLE-STRLIT-LEN]", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
