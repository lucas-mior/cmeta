#!/usr/bin/env python3
"""Phase 5.1 declaration rules and parser-coverage integration tests."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []


class CsaneStructureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, source, *options, filename="src/check.c"):
        file = self.root / filename
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(file), *options],
            cwd=self.root, text=True, capture_output=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stdout)
        self.assertNotEqual(result.returncode, 2, result.stderr)
        return result

    def expect(self, source, count, *options, filename="src/check.c"):
        result = self.check(source, *options, filename=filename)
        actual = result.stdout.count("error[AST-DECL-ONE]")
        self.assertEqual(actual, count, result.stdout)
        self.assertEqual(result.returncode, int(count > 0), result.stdout)
        return result

    def test_comma_declarators_are_not_initializer_commas(self):
        self.expect("int a, b;\n", 1)
        self.expect("int a[] = {1, 2};\n", 0)
        self.expect("int a = (1, 2);\n", 0)
        self.expect("int (*handler)(int), *other;\n", 1)

    def test_two_declarations_on_one_line(self):
        result = self.expect("void f(void) {\n"
                             "    int a; int b;\n"
                             "}\n", 1)
        self.assertIn("check.c:2:12: error[AST-DECL-ONE]", result.stdout)
        self.expect("int a; int b;\n", 1)
        self.expect("int a;\nint b;\n", 0)

    def test_function_headers_are_not_variable_declarations(self):
        self.expect("int f(void); int g(void);\n", 0)
        self.expect("int (*fp)(int); int other;\n", 1)
        self.expect("int f(void) { return 0; }\n", 0)

    def test_parameter_commas_and_pointer_declarators(self):
        self.expect("void f(int a, int b);\n", 0)
        self.expect("void f(int a, int b) {\n"
                    "    int (*cb)(int, int);\n"
                    "    return;\n}\n", 0)

    def test_typedef_and_record_members(self):
        self.expect("typedef int A, B;\n"
                    "struct Pair { int x, y; };\n", 2)
        self.expect("struct Pair {\n"
                    "    int x; int y;\n"
                    "};\n", 1)
        self.expect("struct Pair {\n"
                    "    int x;\n    int y;\n};\n", 0)

    def test_for_initializer(self):
        self.expect("void f(void) {\n"
                    "    for (int i = 0, j = 1; i < 3; i += 1) {\n"
                    "        int n;\n"
                    "    }\n}\n", 1)
        self.expect("void f(void) {\n"
                    "    for (int i = 0; i < 3; i += 1) {\n"
                    "        int n;\n"
                    "    }\n}\n", 0)

    def test_independent_scopes_and_conditional_branches(self):
        self.expect("void f(void) { int a; { int b; } }\n", 0)
        self.expect("#if defined(A)\nint x;\n#else\nint y;\n#endif\n", 0)

    def test_declarations_follow_unrecognized_expressions(self):
        self.expect("void f(void) {\n"
                    "    bool accepted = true;\n"
                    "    int x, y;\n"
                    "}\n", 1)

    def test_unrelated_nested_commas_are_not_declarators(self):
        self.expect("void f(void) {\n"
                    "    int values[] = {1, 2};\n"
                    "    int x = run(1, 2);\n"
                    "}\n", 0)

    def test_fixture_exclusion(self):
        self.expect("void f(void) { int a, b; }\n", 0,
                    filename="tests/fixtures/example.c")

    def test_ryu_exemption(self):
        self.expect("int a, b;\n", 0, filename="cbase/ryu/s2d.c")

    def test_policy_exemption(self):
        config = self.root / "policy.conf"
        config.write_text("allow-rule AST-DECL-ONE .*check\\.c\n",
                          encoding="utf-8")
        self.expect("int a, b;\n", 0, "--config", str(config))

    def test_coverage_counts_and_no_rule_side_effects(self):
        source = "void f(int x);\nvoid g(void) { int n = myfunc(0); }\n"
        result = self.expect(source, 0, "--coverage", "--stats")
        self.assertIn("coverage file=", result.stdout)
        self.assertIn("function_declarations=1", result.stdout)
        self.assertIn("function_definitions=1", result.stdout)
        self.assertIn("unsupported_externals=0", result.stdout)
        self.assertIn("parse_issues=0", result.stdout)
        self.assertIn("files=1", result.stdout)

    def test_coverage_of_malformed_regions(self):
        result = self.check("int broken( {\nint valid;\n", "--coverage")
        self.assertIn("coverage file=", result.stdout)
        self.assertRegex(result.stdout, r"unsupported_externals=[1-9]")
        self.assertRegex(result.stdout, r"parse_issues=[1-9]")

    def test_parser_failure_keeps_lexical_checks(self):
        result = self.check("int bad( {\n#ifdef X\n")
        self.assertIn("error[PP-DEFINED]", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
