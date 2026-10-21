#!/usr/bin/env python3
"""Perl-oracle fixtures for assignment positioning and grouping (Step 12)."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest

from differential import load_rules, run_checkers


CASES = {
    "leading_equals": (
        "void f(void) {\n    value\n        = alpha;\n"
        "    foo\n        == bar;\n}\n", {"P19": (3,)}),
    "leading_equals_comments": (
        "void f(void) {\n    value\n    /* c */ = 1;\n"
        "    value\n    /* more */\n    = 2;\n}\n",
        {"P19": (3, 6)}),
    "leading_equals_in_macro": (
        "#define MACRO(x) \\\n    = x\nvoid f(void) {\n"
        "    x\n        = foo;\n}\n", {"P19": (5,)}),
    "leading_equals_pointer": (
        "void f(void) {\n    *pointer\n    = other;\n}\n",
        {"P19": (3,)}),
    "comparison_is_not_assignment": (
        "void f(void) {\n    a = b\n        == c;\n"
        "    a = b\n        <= c;\n}\n", {}),
    "short_assignment": (
        "void f(void) {\n    result =\n        compute();\n}\n",
        {"P20": (2,)}),
    "two_short_assignments": (
        "void f(void) {\n    a =\n        x;\n    b =\n"
        "        y;\n}\n", {"P20": (2, 4)}),
    "two_global_assignments": (
        "int32 x =\n    42;\nint32 y =\n    21;\n",
        {"P20": (1, 3)}),
    "nested_call": (
        "void f(void) {\n    a =\n        compute(x, nested(y));\n}\n",
        {"P20": (2,)}),
    "nested_assignment": (
        "void f(void) {\n    a =\n        (b = 2);\n}\n",
        {"P20": (2,)}),
    "comma_expression": (
        "void f(void) {\n    a =\n        (x, y);\n}\n",
        {"P20": (2,)}),
    "cast": (
        "void f(void) {\n    a =\n        (int32)(b + c);\n}\n",
        {"P20": (2,)}),
    "pointer_declaration": (
        "void f(void) {\n    int32 *p =\n        other;\n}\n",
        {"P20": (2,)}),
    "member_assignment": (
        "void f(void) {\n    item->value =\n        compute();\n}\n",
        {"P20": (2,)}),
    "macro_invocation_lhs": (
        "void f(void) {\n    X(a) =\n        b;\n}\n",
        {"P20": (2,)}),
    "newline_literal": (
        'void f(void) {\n    text =\n        "{value}";\n}\n',
        {"P20": (2,)}),
    "long_neighbor": (
        "void f(void) {\n    a =\n        f();\n"
        "    b = " + "a_very_long_identifier_" * 4 + ";\n"
        "    c =\n        f();\n}\n", {}),
    "blank_line_splits_group": (
        "void f(void) {\n    a =\n        x;\n\n"
        "    b =\n        y;\n}\n", {"P20": (2, 5)}),
    "commented_long_group": (
        "void f(void) {\n    a =\n        f();\n"
        "    /* related */\n    b =\n        "
        + "this_identifier_is_too_long_to_fit_in_a_single_line_" * 2
        + ";\n    c =\n        f();\n}\n", {}),
    "trailing_comment_suppresses_group": (
        "void f(void) {\n    a =\n        f(); // comment\n"
        "    b =\n        f();\n}\n", {}),
    "inline_initializer_suppresses_neighbor": (
        "void f(void) {\n    struct X item = {1, 2};\n"
        "    b =\n        f();\n}\n", {}),
    "multiline_initializer_does_not_suppress": (
        "void f(void) {\n    item =\n        (int32){1};\n"
        "    b =\n        x;\n}\n", {"P20": (4,)}),
    "braced_initializer_not_checked": (
        "void f(void) {\n    struct Item x =\n"
        "        {1, 2};\n}\n", {}),
    "condition_and_compound_assignment": (
        "void f(void) {\n    if (x ==\n        y) {}\n"
        "    x +=\n        y;\n}\n", {}),
    "not_broken_after_equals": (
        "void f(void) {\n    value = add(\n"
        "        a, b);\n    total = x +\n        y;\n}\n", {}),
    "comments_in_assignment": (
        "void f(void) {\n    value =\n"
        "        /* reason */ value2;\n}\n", {}),
    "macro_continuation": (
        "#define M() do { \\\n    value = \\\n"
        "        get(); \\\n} while (0)\n", {}),
    "conditional_branches": (
        "void f(void) {\n#if defined(A)\n    a =\n"
        "        x;\n#else\n    b =\n        y;\n#endif\n}\n",
        {"P20": (3, 6)}),
    "after_interrupted_branch": (
        "void f(void) {\n    b =\n#if defined(A)\n"
        "        x;\n#else\n        y;\n#endif\n"
        "    c =\n        z;\n}\n", {"P20": (8,)}),
    "separate_if_blocks": (
        "void f(void) {\n    if (ok) {\n        a =\n"
        "            x;\n    } else {\n        b =\n"
        "            y;\n    }\n}\n", {"P20": (3, 6)}),
    "two_functions": (
        "void f(void) {\n    a =\n        x;\n}\n"
        "void g(void) {\n    b =\n        y;\n}\n", {"P20": (2, 6)}),
    "multiple_statements_at_end": (
        "void f(void) {\n    a =\n"
        "        f(); g();\n    b =\n        f();\n}\n",
        {"P20": (4,)}),
    "multiple_statements_on_start_line": (
        "void f(void) {\n    a = x; b =\n        y;\n}\n", {}),
    "missing_semicolon": (
        "void f(void) {\n    a =\n        x\n}\n", {}),
    "semicolon_next_line": (
        "void f(void) {\n    a =\n        f()\n        ;\n}\n",
        {"P20": (2,)}),
    "generated_header": (
        "void f(void) {\n    a =\n        x;\n}\n", {}),
    "ryu_exemption": (
        "void f(void) {\n    a =\n        x;\n}\n", {}),
}


class AssignmentWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.categories = {self.rules[key].category for key in ("P19", "P20")}

    def check(self, name, source, expected, path=None):
        if path is None:
            path = f"src/{name}.c"
        path = self.root / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        comparison = run_checkers([path], self.root, rules=self.rules)
        legacy = Counter((finding.line, finding.category)
                         for finding in comparison.legacy
                         if finding.category in self.categories)
        newer = Counter((finding.line, finding.category)
                        for finding in comparison.csane
                        if finding.category in self.categories)
        target = Counter((line, self.rules[key].category)
                         for key, lines in expected.items() for line in lines)
        self.assertEqual(legacy, target, comparison.summary())
        self.assertEqual(newer, target, comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())

    def test_perl_assignment_parity(self):
        for name, (source, expected) in CASES.items():
            with self.subTest(case=name):
                path = None
                if name == "generated_header":
                    path = "src/example.meta.h"
                elif name == "ryu_exemption":
                    path = "cbase/ryu/vendor.c"
                self.check(name, source, expected, path)

    def test_column_override(self):
        source = ("    shorter =\n        small();\n"
                  "    longer =\n        " + "x" * 75 + ";\n")
        self.check("ordinary_limit", source, {})
        self.check("cecup_limit", source, {"P20": (1, 3)},
                   "cecup/src/cecup_limit.c")


if __name__ == "__main__":
    unittest.main()
