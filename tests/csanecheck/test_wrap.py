#!/usr/bin/env python3
"""Phase 6.3: source-preserving, group-aware wrapping regression tests."""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []
RULE = re.compile(r"error\[(FMT-(?:CALL|ASSIGN)-WRAP)\]")


class CsaneWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, source, expected=(), name="src/sample.c", flags=()):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run([str(EXECUTABLE), "--file", str(path), *flags],
                                cwd=self.root, capture_output=True,
                                text=True, check=False, timeout=30)
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stdout)
        self.assertEqual(sorted(RULE.findall(result.stdout)),
                         sorted(expected), result.stdout)

    def test_short_call(self):
        self.check("void f(void) {\n    run(first,\n        second);\n}\n",
                   ["FMT-CALL-WRAP"])

    def test_short_call_group(self):
        self.check("void f(void) {\n    run(first,\n        second);\n"
                   "    run(third, fourth);\n}\n", ["FMT-CALL-WRAP"])

    def test_long_call_justifies_short_neighbor(self):
        self.check("void f(void) {\n    run(first,\n        second);\n"
                   "    long_name_this_will_not_fit_on_one_line(" 
                   "a_very_long_identifier_that_is_definitely_far_too_long,"
                   " b, c);\n}\n")

    def test_long_call_before_short(self):
        self.check("void f(void) {\n"
                   "    long_name_this_will_not_fit_on_one_line(" 
                   "a_very_long_identifier_that_is_definitely_far_too_long,"
                   " b, c);\n    run(first,\n        second);\n}\n")

    def test_blank_line_separates_call_groups(self):
        self.check("void f(void) {\n    run(first,\n        second);\n\n"
                   "    long_name_this_will_not_fit_on_one_line(" 
                   "a_very_long_identifier_that_is_definitely_far_too_long,"
                   " b, c);\n}\n", ["FMT-CALL-WRAP"])

    def test_comment_separates_call_groups(self):
        self.check("void f(void) {\n    run(first,\n        second);\n"
                   "    // next operation\n"
                   "    long_name_this_will_not_fit_on_one_line(" 
                   "a_very_long_identifier_that_is_definitely_far_too_long,"
                   " b, c);\n}\n", ["FMT-CALL-WRAP"])

    def test_error_and_assert_error_exempt(self):
        self.check("void f(void) {\n    error_impl(\n        first, second);\n"
                   "    assert_error(first,\n                 second);\n}\n")

    def test_nested_call_is_not_independent(self):
        self.check("void f(void) {\n    run(nested(a,\n"
                   "               b), c);\n}\n", ["FMT-CALL-WRAP"])

    def test_comment_inside_call_prevents_rewrite(self):
        self.check("void f(void) {\n    run(first, /* important */\n"
                   "        second);\n}\n")

    def test_adjacent_split_string_literal_prevents_rewrite(self):
        self.check("void f(void) {\n    run(\"a\"\n"
                   "        \"b\");\n}\n")

    def test_return_call(self):
        self.check("int f(void) {\n    return calculate(first,\n"
                   "                     second);\n}\n", ["FMT-CALL-WRAP"])

    def test_initializer_call(self):
        self.check("int f(void) {\n    int result = calculate(first,\n"
                   "                           second);\n"
                   "    return result;\n}\n", ["FMT-CALL-WRAP"])

    def test_function_pointer_call_excluded(self):
        self.check("void f(void) {\n    (*callback)(first,\n"
                   "                second);\n}\n")

    def test_assignment_equals_leading_is_not_call_wrap(self):
        self.check("void f(void) {\n    result\n"
                   "        = compute();\n}\n")

    def test_macro_excluded(self):
        self.check("#define MACRO(x) run(first, \\\n"
                   "    x)\nvoid f(void) {\n    run(a, b);\n}\n")

    def test_generated_header_excluded(self):
        self.check("void f(void) {\n    run(first,\n"
                   "        second);\n}\n", name="src/generated.meta.h")

    def test_unknown_neighbor_does_not_prove_group_fits(self):
        self.check("void f(void) {\n"
                   "    AlienType unknown = "
                   "a_very_long_name_that_will_not_fit_on_one_line;\n"
                   "    result =\n        value;\n}\n")

    def test_configured_wrap_exemption(self):
        policy = self.root / "policy.conf"
        policy.write_text("allow-rule FMT-CALL-WRAP .*sample\\.c\n",
                          encoding="utf-8")
        self.check("void f(void) {\n    run(first,\n"
                   "        second);\n}\n", flags=("--config", str(policy)))

    def test_ryu_keeps_call_rule_but_exempts_assignments(self):
        self.check("void f(void) {\n    run(first,\n"
                   "        second);\n    result =\n"
                   "        value;\n}\n", ["FMT-CALL-WRAP"],
                   name="cbase/ryu/f2s.c")

    def test_macro_first_argument_line_break(self):
        self.check("void f(void) {\n    ASSERT_LE(\n"
                   "        value, 0);\n}\n", ["FMT-CALL-WRAP"])

    def test_macro_subsequent_argument_break(self):
        self.check("void f(void) {\n    ASSERT_EQ(value,\n"
                   "              expected);\n}\n")

    def test_call_after_unknown_declaration(self):
        self.check("void f(void) {\n    UnknownType value;\n"
                   "    run(first,\n        second);\n}\n",
                   ["FMT-CALL-WRAP"])

    def test_assignment_after_unknown_declaration(self):
        self.check("void f(void) {\n    AlienType value;\n"
                   "    result =\n        value;\n}\n",
                   ["FMT-ASSIGN-WRAP"])

    def test_assignment_with_nested_equal_comparison(self):
        self.check("void f(void) {\n    result =\n"
                   "        select(a == b, c);\n}\n",
                   ["FMT-ASSIGN-WRAP"])

    def test_short_assignment(self):
        self.check("void f(void) {\n    result =\n        value;\n}\n",
                   ["FMT-ASSIGN-WRAP"])

    def test_assignment_group_all_fit(self):
        self.check("void f(void) {\n    result =\n        value;\n"
                   "    second = other;\n}\n", ["FMT-ASSIGN-WRAP"])

    def test_long_assignment_justifies_short_neighbor(self):
        self.check("void f(void) {\n    result =\n        value;\n"
                   "    other = a_very_long_identifier_which_has_no_chance_"
                   "of_fitting_eighty_columns + extra;\n}\n")

    def test_assignment_comment_only_group(self):
        self.check("void f(void) {\n    result =\n        value;\n"
                   "    // related value\n"
                   "    other = a_very_long_identifier_which_has_no_chance_"
                   "of_fitting_eighty_columns + extra;\n}\n")

    def test_assignment_blank_line_separates_groups(self):
        self.check("void f(void) {\n    result =\n        value;\n\n"
                   "    other = a_very_long_identifier_which_has_no_chance_"
                   "of_fitting_eighty_columns + extra;\n}\n",
                   ["FMT-ASSIGN-WRAP"])

    def test_decl_initializer_wrap(self):
        self.check("void f(void) {\n    int value =\n"
                   "        other;\n}\n", ["FMT-ASSIGN-WRAP"])

    def test_nested_assignment_not_statement(self):
        self.check("void f(void) {\n    if (result =\n"
                   "        next()) {\n        run();\n    }\n}\n")

    def test_compound_assignment_not_plain_assignment(self):
        self.check("void f(void) {\n    result +=\n"
                   "        value;\n}\n")

    def test_comment_in_assignment(self):
        self.check("void f(void) {\n    result = /* note */\n"
                   "        value;\n}\n")

    def test_assignment_group_interrupted_by_statement(self):
        self.check("void f(void) {\n    result =\n        value;\n"
                   "    process();\n"
                   "    other = a_very_long_identifier_which_has_no_chance_"
                   "of_fitting_eighty_columns + extra;\n}\n",
                   ["FMT-ASSIGN-WRAP"])

    def test_wrapped_call_after_assignment_equals(self):
        self.check("void f(void) {\n    result =\n"
                   "        compute(first, second);\n}\n",
                   ["FMT-ASSIGN-WRAP"])


if __name__ == "__main__":
    unittest.main()
