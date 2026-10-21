#!/usr/bin/env python3
"""Step 15: diagnostics and coverage never require an entire C AST."""

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"


class AstIndependenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "sample.c"

    def check(self, contents, *args):
        self.source.write_text(contents, encoding="utf-8")
        return subprocess.run([str(CHECKER), "--file", str(self.source),
                               *args], cwd=ROOT, capture_output=True,
                              text=True, check=False)

    def test_incompatible_conditional_branches_need_no_configuration(self):
        result = self.check(
            "#define TOKEN_JOIN(a, b) a ## b\n"
            "#if SOME_UNKNOWN_CONDITION\n"
            "int32 first(void) {\n    return 1;\n}\n"
            "#else\nint32 second(void) {\n    return 2;\n}\n"
            "#endif\n",
            "--strict-parse", "--coverage", "--stats")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("function_definitions=2", result.stdout)
        self.assertIn("structural_issues=0", result.stdout)
        self.assertIn("preprocessor_issues=0", result.stdout)
        self.assertNotIn("projected_tokens=", result.stdout)
        self.assertNotIn("parse_issues=", result.stdout)

    def test_local_opaque_statement_does_not_hide_later_rules(self):
        result = self.check(
            "int32 first(void) {\n    return 0;\n}\n"
            "int32 bad(void) {\n    int32 x = (1 +;\n"
            "    return sizeof(x);\n}\n"
            "int32 last(void) {\n    return 1;\n}\n",
            "--strict-parse", "--report-parse-issues", "--coverage",
            "--rule-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("note[STRUCTURE-OPAQUE]", result.stdout)
        self.assertIn("error[PP-SIZEOF]", result.stdout)
        self.assertIn("function_definitions=3", result.stdout)
        self.assertIn("structural_issues=1", result.stdout)
        self.assertRegex(result.stdout, r"group=SRC status=checked")

    def test_preprocessor_errors_remain_local(self):
        result = self.check("#endif\nint32 good(void) {\n"
                            "    return 0;\n}\n",
                            "--strict-parse", "--report-parse-issues",
                            "--stats")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("note[PP-STRUCTURE]", result.stdout)
        self.assertIn("function_definitions=1", result.stdout)
        self.assertIn("structural_issues=0", result.stdout)
        self.assertRegex(result.stdout, r"preprocessor_issues=[1-9]")

    def test_driver_contains_no_ast_or_projection_entrypoints(self):
        sources = ["main.c", "csck.c", "csck_rules.c", "csck_coverage.c",
                   "csck_structure.c", "csck.h"]
        for name in sources:
            text = (ROOT / "csanecheck" / name).read_text(encoding="utf-8")
            with self.subTest(source=name):
                self.assertNotRegex(text, r"\bc_ast_\w+\s*\(")
                self.assertNotRegex(text, r"\bcsane_macro_view_(?:init|free)\b")
                self.assertNotRegex(text, r"\bcsane_types_prepare\b")
                self.assertNotRegex(text, r"\bCSANE_PASS_(?:SYNTAX|CONTEXT)\b")
                self.assertNotRegex(text, r"\bCSANE_TARGET_PROJECTED_CALLS\b")
        main = (ROOT / "csanecheck" / "main.c").read_text(encoding="utf-8")
        for filename in ("csck_context.c", "csck_types.c", "csck_macros.c"):
            self.assertNotIn('#include "' + filename + '"', main)


if __name__ == "__main__":
    unittest.main()
