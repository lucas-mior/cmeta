#!/usr/bin/env python3
"""AST-based expression and contextual checks for Phase 5.3."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []


class CsaneContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, source, *options, filename="src/check.c"):
        path = self.root / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(path), *options],
            cwd=self.root, capture_output=True, text=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stdout)
        self.assertNotEqual(result.returncode, 2, result.stderr)
        return result

    def expect(self, source, expected, *options, filename="src/check.c"):
        result = self.check(source, *options, filename=filename)
        found = [part.split("]", 1)[0]
                 for line in result.stdout.splitlines()
                 if "error[" in line
                 for part in [line.split("error[", 1)[1]]]
        self.assertEqual(sorted(found), sorted(expected), result.stdout)
        self.assertEqual(result.returncode, int(bool(expected)), result.stdout)
        return result

    def test_relational_assertions(self):
        self.expect("void f(int x) {\n"
                    "    ASSERT(x >= 3);\n"
                    "    ASSERT(x > 3);\n"
                    "    ASSERT(x <= 3);\n"
                    "    ASSERT(x < 3);\n"
                    "}\n", ["STYLE-ASSERT-REL"] * 4)

    def test_float_comparisons_are_not_integer_assertions(self):
        self.expect("void f(double x) {\n"
                    "    ASSERT(x > 0.0);\n"
                    "    ASSERT(x <= 1.25);\n"
                    "    ASSERT(x >= 1e6);\n"
                    "}\n", [])

    def test_expression_shaped_macro_assertion(self):
        self.expect("#define CHECK(x) ASSERT(x >= 1)\n",
                    ["STYLE-ASSERT-REL"])

    def test_macro_replacement_isolated_from_incomplete_branch(self):
        self.expect('#if ENABLED\n'
                    'void unfinished(void) {\n'
                    '#else\n'
                    '#define CHECK() STRLIT_LEN("bad")\n'
                    '#endif\n', ["STYLE-STRLIT-LEN"])

    def test_independent_macro_replacements(self):
        self.expect('#define ONE() STRLIT_LEN("a")\n'
                    '#define TWO() STREQUAL(x, STRLIT("b"))\n',
                    ["STYLE-STRLIT-LEN", "STYLE-STREQUAL"])

    def test_relational_assertions_safe_cases(self):
        self.expect("void f(int x) {\n"
                    "    ASSERT(x == 3);\n"
                    "    ASSERT(x > limit);\n"
                    "    ASSERT_GE(x, 3);\n"
                    "    ASSERT(call() < 5);\n"
                    "}\n", [])

    def test_style_calls_after_unrecognized_local_syntax(self):
        self.expect('void f(void) {\n'
                    '    bool active = true;\n'
                    '    ASSERT(active >= 1);\n'
                    '    STRLIT_LEN("a");\n'
                    '    STREQUAL(active, STRLIT("b"));\n'
                    '    str_printf(out, "%d", active);\n'
                    '}\n', ["STYLE-ASSERT-REL", "STYLE-STRLIT-LEN",
                              "STYLE-STREQUAL", "STYLE-PRINTF"])

    def test_style_calls_nested_and_parenthesized(self):
        self.expect('void f(void) {\n'
                    '    call(STRLIT_LEN(("abc")));\n'
                    '    STREQUAL(x, (STRLIT("abc")));\n'
                    '}\n', ["STYLE-STRLIT-LEN", "STYLE-STREQUAL"])

    def test_consecutive_bounds(self):
        self.expect("void f(int x, int n) {\n"
                    "    ASSERT_GE(x, 0);\n"
                    "    ASSERT_LT(x, n);\n"
                    "}\n", ["STYLE-ASSERT-BOUNDS"])

    def test_bounds_member_and_subscript(self):
        self.expect("void f(void) {\n"
                    "    ASSERT_GE(obj->data[i], 0);\n"
                    "    ASSERT_LT(obj->data[i], limit);\n"
                    "}\n", ["STYLE-ASSERT-BOUNDS"])

    def test_bounds_do_not_merge_side_effects(self):
        self.expect("void f(int x, int n) {\n"
                    "    ASSERT_GE(next(), 0);\n"
                    "    ASSERT_LT(next(), n);\n"
                    "}\n", [])

    def test_bounds_require_adjacent_line_and_equal_operand(self):
        self.expect("void f(int x, int y, int n) {\n"
                    "    ASSERT_GE(x, 0);\n"
                    "\n"
                    "    ASSERT_LT(x, n);\n"
                    "    ASSERT_GE(x, 0);\n"
                    "    ASSERT_LT(y, n);\n"
                    "}\n", [])

    def test_multiline_bounds_first_call(self):
        self.expect("void f(int x, int n) {\n"
                    "    ASSERT_GE(\n"
                    "        x, 0);\n"
                    "    ASSERT_LT(x, n);\n"
                    "}\n", ["STYLE-ASSERT-BOUNDS", "FMT-CALL-FIRST",
                          "FMT-CALL-WRAP"])

    def test_bounds_nested_control(self):
        self.expect("void f(int x, int n) {\n"
                    "    if (n) {\n"
                    "        ASSERT_GE(x, 0);\n"
                    "        ASSERT_LT(x, n);\n"
                    "    }\n"
                    "}\n", ["STYLE-ASSERT-BOUNDS"])

    def test_bounds_after_unrecognized_syntax(self):
        self.expect("void f(int x, int n) {\n"
                    "    int (*handler)(int) = 0;\n"
                    "    ASSERT_GE(x, 0);\n"
                    "    ASSERT_LT(x, n);\n"
                    "}\n", ["STYLE-ASSERT-BOUNDS"])

    def test_bounds_intervening_statement(self):
        self.expect("void f(int x, int n) {\n"
                    "    ASSERT_GE(x, 0); call(); ASSERT_LT(x, n);\n"
                    "}\n", [])

    def test_bounds_same_line_not_combined(self):
        self.expect("void f(int x, int n) {\n"
                    "    ASSERT_GE(x, 0); ASSERT_LT(x, n);\n"
                    "}\n", [])

    def test_strlit_len(self):
        self.expect('void f(void) { STRLIT_LEN("abc"); }\n',
                    ["STYLE-STRLIT-LEN"])
        self.expect('void f(void) { STRLIT_LEN(name); }\n', [])
        self.expect('void STRLIT_LEN(char *name);\n', [])

    def test_strequal(self):
        self.expect('void f(void) { STREQUAL(name, STRLIT("abc")); }\n',
                    ["STYLE-STREQUAL"])
        self.expect('void f(void) { STREQUAL(name, STRLIT(value)); }\n', [])
        self.expect('void f(void) { obj.STREQUAL(STRLIT("abc"), x); }\n', [])

    def test_printf_format(self):
        self.expect('void f(void) { str_printf(out, "%lld", number); }\n',
                    ["STYLE-PRINTF"])
        self.expect('void f(void) { str_printf(out, "%d%%", n); }\n', [])
        self.expect('void f(void) { str_printf(out, "value=%d", n); }\n', [])
        self.expect('void f(void) { str_printf(out, "%d"); }\n', [])

    def test_status_after_unrecognized_local_declaration(self):
        self.expect("void f(void) {\n"
                    "    ExternalType unknown = (ExternalType){0};\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "    (void)unknown;\n"
                    "}\n", ["STYLE-STATUS-TEMP"])

    def test_status_nested_reference_prevents_recommendation(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "    if (flag()) {\n"
                    "        log_status(status);\n"
                    "    }\n"
                    "}\n", [])

    def test_status_one_pair(self):
        self.expect("static void f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "}\n", ["STYLE-STATUS-TEMP"])

    def test_status_multiple_pairs(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    status = first();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "    status = second();\n"
                    "    return_if_error(status);\n"
                    "}\n", ["STYLE-STATUS-TEMP"])

    def test_status_return_pair(self):
        self.expect("int32 f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    return status;\n"
                    "}\n", ["STYLE-STATUS-TEMP"])

    def test_status_not_immediate(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n\n"
                    "    ASSERT_LE(status, 0);\n"
                    "}\n", [])

    def test_status_reference_not_on_next_line(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    ASSERT_LE(\n"
                    "        status, 0);\n"
                    "}\n", ["FMT-CALL-FIRST", "FMT-CALL-WRAP"])

    def test_status_extra_references(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "    log_status(status);\n"
                    "}\n", [])

    def test_status_non_assignment_and_initialized(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    "    operation(&status);\n"
                    "    ASSERT_LE(status, 0);\n"
                    "}\n", [])
        self.expect("void f(void) {\n"
                    "    int32 status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "}\n", [])

    def test_status_member_and_literal_ignored(self):
        self.expect("void f(void) {\n"
                    "    int32 status;\n"
                    '    char *text = "status";\n'
                    "    status = operation();\n"
                    "    ASSERT_LE(status, 0);\n"
                    "    (void)text;\n"
                    "}\n", ["STYLE-STATUS-TEMP"])

    def test_fixture_exemption(self):
        self.expect("void f(void) { ASSERT(x > 1); }\n", [],
                    filename="tests/fixtures/example.c")

    def test_policy_exemption(self):
        config = self.root / "policy.conf"
        config.write_text("allow-rule STYLE-ASSERT-REL .*check\\.c\n",
                          encoding="utf-8")
        self.expect("void f(void) { ASSERT(x > 1); }\n", [],
                    "--config", str(config))

    def test_typedef_binding_improves_coverage(self):
        self.expect("int32 f(uint32 x) { return (int32)x; }\n", [],
                    "--coverage")


if __name__ == "__main__":
    unittest.main(verbosity=2)
