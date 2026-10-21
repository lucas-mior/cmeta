#!/usr/bin/env python3
"""Coverage is accounted for by rule target, not by translation-unit AST."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
RULE = re.compile(r"^rule_coverage file=.*? group=(\S+) status=(\S+) "
                  r"pass=(\d+) eligible=(\d+) checked=(\d+) "
                  r"excluded=(\d+) unresolved=(\d+)$")


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, text, *, name="fixture.c", coverage=False,
              require=False):
        source = self.root / name
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(text, encoding="utf-8")
        command = [str(CHECKER), "--file", str(source), "--rule-coverage"]
        if coverage:
            command.append("--coverage")
        if require:
            command.append("--require-coverage")
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False)
        groups = {}
        for line in result.stdout.splitlines():
            match = RULE.match(line)
            if match:
                group, status, pass_number, *values = match.groups()
                groups[group] = dict(zip(("eligible", "checked",
                                          "excluded", "unresolved"),
                                         map(int, values)))
                groups[group]["status"] = status
                groups[group]["pass"] = int(pass_number)
        self.assertEqual(len(groups), 15, result.stdout + result.stderr)
        self.assertNotIn("STYLE", groups)
        for group in groups.values():
            self.assertEqual(group["eligible"], group["checked"]
                             + group["excluded"] + group["unresolved"])
        return result, groups

    def test_clean_file_is_fully_checked_without_ast_operator_targets(self):
        result, groups = self.check("int32 good(void) {\n    return 0;\n}\n",
                                    coverage=True, require=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertTrue(all(group["status"] == "checked"
                            for group in groups.values()))
        self.assertIn("coverage_layers file=", result.stdout)
        self.assertIn("unresolved=0", result.stdout)

    def test_local_opaque_statement_affects_only_structural_rules(self):
        text = ("int32 first(void) {\n    return 0;\n}\n"
                "int32 bad(void) {\n    int32 x = (1 +;\n    return x;\n}\n"
                "int32 last(void) {\n    return 1;\n}\n")
        result, groups = self.check(text, require=True)
        self.assertEqual(result.returncode, 1, result.stdout)
        for name in ("SRC", "PP", "PP-SIZEOF", "API", "ASSIGN-EQ",
                     "FUNC-HEADERS"):
            self.assertEqual(groups[name]["status"], "checked", name)
        for name in ("CONTROL", "STYLE-STATUS", "AST-DECL-ONE",
                     "CALL-WRAP"):
            self.assertEqual(groups[name]["status"], "checked", name)
            self.assertEqual(groups[name]["unresolved"], 0, name)
        self.assertEqual(groups["ASSIGN-WRAP"]["status"], "not_checked")
        self.assertGreater(groups["ASSIGN-WRAP"]["unresolved"], 0)
        self.assertIn("reason=assignment-statement-boundary-unrecognized",
                      result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_broken_preprocessor_does_not_invalidate_token_rules(self):
        text = ("#if 0\nint32 first;\n#else\nint32 second;\n"
                "#endif\n#endif\nint32 last = 0;\n")
        _, groups = self.check(text, require=True)
        self.assertEqual(groups["SRC"]["status"], "checked")
        self.assertEqual(groups["API"]["status"], "checked")
        self.assertEqual(groups["ASSIGN-EQ"]["status"], "checked")
        self.assertIn(groups["PP"]["status"], ("partial", "checked"))

    def test_operator_coverage_is_not_claimed_without_classification(self):
        text = ("int32 first(void) {\n    int32 x = (1 +;\n"
                "    return 0;\n}\n")
        result, groups = self.check(text, require=True)
        self.assertEqual(result.returncode, 1)
        self.assertGreater(groups["LAYOUT"]["unresolved"], 0)
        self.assertEqual(groups["SRC"]["unresolved"], 0)


    def test_rule_local_unresolved_reasons(self):
        fixtures = {
            "control.c": (
                "int32 f(int32 x) {\n    if (x + {\n        return 1;\n    }\n}\n",
                "CONTROL", "control-structure-unrecognized"),
            "call.c": (
                "void f(void) {\n    work(1 +;\n}\n",
                "CALL-WRAP", "call-statement-boundary-unrecognized"),
            "assign.c": (
                "void f(void) {\n    value = (1 +;\n}\n",
                "ASSIGN-WRAP",
                "assignment-statement-boundary-unrecognized"),
        }
        for name, (text, group, reason) in fixtures.items():
            with self.subTest(group=group):
                result, groups = self.check(text, name=name, require=True)
                self.assertGreater(groups[group]["unresolved"], 0,
                                   result.stdout)
                self.assertIn("rule_coverage_unresolved", result.stdout)
                self.assertIn(f"group={group}", result.stdout)
                self.assertIn(f"reason={reason}", result.stdout)

    def test_declaration_and_status_unresolved_reasons(self):
        result, groups = self.check(
            "int32 broken(void {\n"
            "    int32 status;\n"
            "    return status;\n"
            "}\n", require=True)
        self.assertGreater(groups["AST-DECL-ONE"]["unresolved"], 0)
        self.assertGreater(groups["FUNC-HEADERS"]["unresolved"], 0)
        self.assertGreater(groups["STYLE-STATUS"]["unresolved"], 0)
        self.assertIn("reason=declaration-structure-unrecognized",
                      result.stdout)
        self.assertIn("reason=function-header-unrecognized", result.stdout)
        self.assertIn("reason=status-function-unrecognized", result.stdout)

    def test_layout_unresolved_has_rule_reason(self):
        result, groups = self.check(
            "void f(void) {\n    mystery_type *pointer;\n}\n", require=True)
        self.assertGreater(groups["LAYOUT"]["unresolved"], 0)
        self.assertIn("group=LAYOUT", result.stdout)
        self.assertIn("reason=ambiguous-operator-context", result.stdout)

    def test_legacy_fixture_exclusion_is_explicit(self):
        _, groups = self.check("void good(void) {\n    return;\n}\n",
                               name="tests/fixtures/fixture.c")
        self.assertEqual(groups["SRC"]["status"], "excluded")
        self.assertEqual(groups["API"]["status"], "checked")
        self.assertGreater(groups["SRC"]["excluded"], 0)
        self.assertEqual(groups["SRC"]["checked"], 0)

    def test_generated_header_is_excluded_before_rule_dispatch(self):
        source = self.root / "generated.meta.h"
        source.write_text("int32 generated(void);\n", encoding="utf-8")
        result = subprocess.run([str(CHECKER), "--file", str(source),
                                 "--rule-coverage", "--require-coverage"],
                                cwd=ROOT, capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("rule_coverage file=", result.stdout)

    def test_unrelated_ast_issue_does_not_poison_source_coverage(self):
        text = ("int32 valid(void) {\n    return 1;\n}\n"
                "int32 invalid(void) {\n    int32 x = (1 +;\n"
                "    return x;\n}\n")
        _, groups = self.check(text)
        self.assertEqual(groups["SRC"]["status"], "checked")
        self.assertEqual(groups["ASSIGN-EQ"]["status"], "checked")
        self.assertGreater(groups["LAYOUT"]["unresolved"], 0)

    def test_api_coverage_uses_direct_call_classification(self):
        text = ("void f(void) {\n"
                "    strcpy(dst, src);\n"
                "    object.strcpy(dst, src);\n"
                "    int32 strcpy(int32 value);\n"
                "    void (*strcpy)(void);\n"
                "}\n"
                "#define PASS(strcpy) strcpy\n")
        result, groups = self.check(text, require=True)
        self.assertEqual(groups["API"]["unresolved"], 0, result.stdout)
        self.assertEqual(groups["API"]["eligible"], 6, result.stdout)
        self.assertEqual(groups["API"]["checked"], 6, result.stdout)

    def test_api_coverage_reports_ambiguous_call_context(self):
        text = ("void f(void) {\n"
                "    mystery strcpy(int32 value);\n"
                "}\n")
        result, groups = self.check(text, require=True)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(groups["API"]["eligible"], 1, result.stdout)
        self.assertEqual(groups["API"]["unresolved"], 1, result.stdout)
        self.assertIn("group=API", result.stdout)
        self.assertIn("reason=api-call-context-unrecognized", result.stdout)


if __name__ == "__main__":
    unittest.main()
