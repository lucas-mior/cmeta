#!/usr/bin/env python3
"""Phase 6.1 operator and localized layout regression tests."""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []
RULE = re.compile(r"error\[([^]]+)\]")


class CsaneLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, source, expected=(), *, name="src/sample.c", flags=()):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run([str(EXECUTABLE), "--file", str(path),
                                 *flags], cwd=self.root, capture_output=True,
                                text=True, check=False, timeout=30)
        self.assertEqual(result.stderr, "", result.stderr)
        found = sorted(RULE.findall(result.stdout))
        self.assertEqual(found, sorted(expected), result.stdout)
        self.assertEqual(result.returncode, int(bool(found)), result.stdout)
        return result

    def test_multiplication_is_binary_only(self):
        self.check("void f(int a, int b) {\n"
                   "    int *pointer;\n"
                   "    int product = a * b;\n"
                   "    *pointer = a*b;\n"
                   "}\n", ["FMT-MULTIPLY"])

    def test_multiplication_each_side_and_parentheses(self):
        for expression in ("a* b", "a *b", "a * b", "(a) * (b)"):
            with self.subTest(expression=expression):
                self.check("void f(int a, int b) {\n"
                           f"    int value = {expression};\n"
                           "}\n", ["FMT-MULTIPLY"])
        self.check("void f(int a, int b) { int n = (a)*(b); }\n")

    def test_multiplication_does_not_confuse_other_star_roles(self):
        self.check("int (*callback)(int);\n"
                   "void f(int *p, int a) {\n"
                   "    int b = *p;\n"
                   "    int c = a**&p;\n"
                   "    (void)b; (void)c;\n"
                   "}\n")

    def test_optional_division_modulo_or_and_xor_spaces(self):
        self.check("void f(int a, int b) {\n"
                   "    int n = a/b;\n"
                   "    n = a % b;\n"
                   "    n = a|b;\n"
                   "    n = a^b;\n"
                   "}\n")

    def test_required_binary_spaces(self):
        expressions = ("a+b", "a -b", "a&b", "a<<1", "a>>1",
                       "a&&b", "a||b", "a==b", "a!=b", "a<b",
                       "a>=b")
        for expression in expressions:
            with self.subTest(expression=expression):
                self.check("void f(int a, int b) {\n"
                           f"    int x = {expression};\n"
                           "}\n", ["FMT-BINARY-SPACING"])

    def test_binary_spaced_and_unary_exemptions(self):
        self.check("void f(int a, int b) {\n"
                   "    int x = -a + +b;\n"
                   "    int y = a && !b;\n"
                   "    int z = a << b;\n"
                   "    (void)x; (void)y; (void)z;\n"
                   "}\n")

    def test_assignment_operators_and_declaration_initializers(self):
        self.check("void f(int a) {\n"
                   "    int b=2;\n"
                   "    b+=a;\n"
                   "    b =a;\n"
                   "}\n", ["FMT-ASSIGN-SPACING"] * 3)
        self.check("int a = 1;\n"
                   "int b[] = {1, 2};\n"
                   "int c[a];\n")

    def test_assignment_in_for_initializer(self):
        self.check("void f(void) {\n"
                   "    for (int i=0; i < 2; i += 1) {}\n"
                   "}\n", ["FMT-ASSIGN-SPACING"])

    def test_assignment_at_beginning_of_continuation(self):
        result = self.check("void f(int x) {\n"
                            "    x\n"
                            "        = 2;\n"
                            "}\n", ["FMT-ASSIGN-EQ"])
        self.assertIn("sample.c:3:9", result.stdout)

    def test_comparison_and_leading_comment_not_assignment(self):
        self.check("void f(int x, int y) {\n"
                   "    int equal = (x\n"
                   "        == y);\n"
                   "    // = 10;\n"
                   "    char *str = \"= 20\";\n"
                   "    (void)equal; (void)str;\n"
                   "}\n")

    def test_preprocessor_directives_and_macros_are_not_rewritten(self):
        self.check("#define MUL(a, b) ((a) * (b))\n"
                   "#define ASSIGN(x) x \\\n"
                   "    = 2\n"
                   "#if defined(MACRO)\n"
                   "void f(int a) { int b = a * a; }\n"
                   "#else\n"
                   "void g(int a) { int b = a * a; }\n"
                   "#endif\n", ["FMT-MULTIPLY"] * 2)

    def test_comments_between_operands_are_respected(self):
        self.check("void f(int a, int b) {\n"
                   "    int x = a /* why */ *b;\n"
                   "    int y = a/* comment */+b;\n"
                   "}\n")

    def test_newlines_are_valid_spacing_for_binary_operators(self):
        self.check("void f(int a, int b) {\n"
                   "    int x = a\n"
                   "            + b;\n"
                   "    int y = a\n"
                   "            *b;\n"
                   "}\n")

    def test_generated_and_ryu_exemptions(self):
        source = "void f(int a, int b) { int x = a * b; }\n"
        self.check(source, name="src/generated.meta.h")
        self.check(source, name="cbase/ryu/s2d.c")

    def test_fixtures_are_not_checked_for_layout(self):
        self.check("void f(int a, int b) { int x = a * b; }\n",
                   name="tests/fixtures/model.c")

    def test_policy_exemption_is_rule_specific(self):
        policy = self.root / "policy.conf"
        policy.write_text("allow-rule FMT-MULTIPLY .*sample\\.c\n",
                          encoding="utf-8")
        self.check("void f(int a, int b) { int x = a * b; a+=b; }\n",
                   ["FMT-ASSIGN-SPACING"], flags=("--config", str(policy)))

    def test_unsupported_source_still_checks_leading_equals(self):
        self.check("unknown_type blah;\n"
                   "blah\n"
                   "    = 2;\n", ["FMT-ASSIGN-EQ"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
