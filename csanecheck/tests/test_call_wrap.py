#!/usr/bin/env python3
"""Perl-oracle fixtures for complete-call wrapping (Step 11)."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest

from differential import load_rules, run_checkers


CASES = {
    "short_call": (
        "void f(void) {\n    work(a,\n         b);\n}\n", (2,)),
    "single_line": (
        "void f(void) {\n    work(a, b);\n}\n", ()),
    "two_short_calls": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    second(x,\n           y);\n}\n", (2, 4)),
    "long_neighbor": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    second(this_argument_will_make_the_line_too_long_to_fit,\n"
        "           and_this_argument_makes_it_even_more_than_eighty);\n}\n",
        ()),
    "blank_line_breaks_group": (
        "void f(void) {\n    first(a,\n          b);\n\n"
        "    second(x, y);\n}\n", (2,)),
    "comment_line_breaks_group": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    // Independent calls\n    second(x, y);\n}\n", (2,)),
    "inline_comment": (
        "void f(void) {\n    first(a, /* comment */\n          b);\n}\n",
        ()),
    "trailing_comment": (
        "void f(void) {\n    first(a,\n          b); // comment\n}\n",
        ()),
    "previous_comment": (
        "void f(void) {\n    /* comment */\n    first(a,\n"
        "          b);\n}\n", (3,)),
    "nested_call": (
        "void f(void) {\n    first(other(a,\n                b));\n}\n",
        (2,)),
    "assignment": (
        "void f(void) {\n    result = work(a,\n                  b);\n}\n",
        (2,)),
    "initializer": (
        "void f(void) {\n    int32 result = work(a,\n"
        "                        b);\n}\n", (2,)),
    "return_call": (
        "int32 f(void) {\n    return work(a,\n                b);\n}\n",
        (2,)),
    "return_on_previous_line": (
        "int32 f(void) {\n    return\n        work(a, b);\n}\n", ()),
    "uppercase_macro": (
        "void f(void) {\n    MACRO(a,\n          b);\n}\n", (2,)),
    "uppercase_first_argument": (
        "void f(void) {\n    MACRO(\n        a, b);\n}\n", (2,)),
    "call_before_first_argument": (
        "void f(void) {\n    work(\n        a, b);\n}\n", (2,)),
    "closing_parenthesis": (
        "void f(void) {\n    work(a, b\n        );\n}\n", (2,)),
    "semicolon_on_next_line": (
        "void f(void) {\n    work(a, b)\n    ;\n}\n", ()),
    "conditional_body": (
        "void f(void) {\n    if (ok) {\n        work(a,\n"
        "             b);\n    }\n}\n", (3,)),
    "preprocessor_branches": (
        "void f(void) {\n#if defined(YES)\n    work(a,\n"
        "         b);\n#else\n    work(c,\n"
        "         d);\n#endif\n}\n", (3, 6)),
    "two_statements_on_line": (
        "void f(void) {\n    work(a,\n         b); other(x, y);\n}\n",
        ()),
    "second_statement_same_line": (
        "void f(void) {\n    first(x, y); second(a,\n"
        "                        b);\n}\n", ()),
    "exempt_error_calls": (
        "void f(void) {\n    error_impl(\"warning: %s\",\n"
        "               message);\n    assert_error(\"problem: %s\",\n"
        "                 reason);\n}\n", ()),
    "ordinary_adjacent_string_literals": (
        'void f(void) {\n    work("alpha"\n'
        '         "beta");\n}\n', (2,)),
    "newline_preserving_strings": (
        'void f(void) {\n    work("alpha\\n"\n'
        '         "beta\\n");\n}\n', ()),
    "mixed_newline_strings": (
        'void f(void) {\n    work("alpha\\n"\n'
        '         "beta");\n}\n', (2,)),
    "same_line_strings": (
        'void f(void) {\n    work("alpha" "beta",\n'
        '         c);\n}\n', (2,)),
    "macro_continuation": (
        "void f(void) {\n    work(a,\\\n         b);\n}\n", ()),
    "pointer_call": (
        "void f(void) {\n    (*pointer)(a,\n               b);\n}\n",
        ()),
    "member_call": (
        "void f(void) {\n    object.work(a,\n                b);\n}\n", ()),
    "local_prototype": (
        "void f(void) {\n    int32 work(a,\n               b);\n}\n",
        ()),
    "no_arguments": (
        "void f(void) {\n    work(\n    );\n}\n", (2,)),
    "intervening_statement": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    int32 unrelated = 42;\n    second(x,\n"
        "           y);\n}\n", (2, 5)),
    "long_group_suppresses_short_wrap": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    second(a, b);\n"
        "    third(xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,\n"
        "          yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy);\n}\n",
        ()),
    "nested_block": (
        "void f(void) {\n    first(a,\n          b);\n"
        "    {\n        second(x, y);\n    }\n}\n", (2,)),
    "capitalized_function": (
        "void f(void) {\n"
        "    ThisIsAVeryVeryVeryVeryVeryVeryLongFunctionName(a,\n"
        "                                                   b);\n}\n",
        (2,)),
    "capitalized_return": (
        "int32 f(void) {\n    return MakeThing(a,\n"
        "                     b);\n}\n", (2,)),
    "capitalized_assignment": (
        "int32 f(void) {\n    answer = MakeThing(a,\n"
        "                       b);\n}\n", (2,)),
    "toplevel_indented_call": (
        "    call(a,\n         b);\n", (1,)),
    "toplevel_unindented_call": (
        "call(a,\n     b);\n", ()),
    "three_newline_literal_fragments": (
        'void f(void) {\n    call("a\\n"\n'
        '         "b\\n"\n         "c\\n");\n}\n', ()),
    "three_mixed_literal_fragments": (
        'void f(void) {\n    call("a\\n"\n'
        '         "b\\n"\n         "c");\n}\n', (2,)),
    "trailing_spaces_break_limit": (
        "void f(void) {\n    call(" + "x" * 64
        + ",\n         b);   \n}\n", ()),
    "trailing_spaces_within_limit": (
        "void f(void) {\n    call(" + "x" * 59
        + ",\n         b);   \n}\n", (2,)),
    "generated_header": (
        "void f(void) {\n    work(a,\n         b);\n}\n", ()),
}


# These two fixtures intentionally diverge from Perl: a long unrelated
# neighbor no longer hides the short wrapped call.
CSANE_EXPECTED = {
    "long_neighbor": (2,),
    "long_group_suppresses_short_wrap": (2,),
}

RELATED_CASES = {
    "same_function": (
        "void f(void) {\n    work(a,\n         b);\n"
        "    work(this_argument_makes_the_call_long,\n"
        "         a_second_argument_that_cannot_fit_on_the_first_line);\n}\n",
        ()),
    "same_first_argument": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(context, argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
    "different_first_argument": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(other, argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        (2,)),
    "nested_same_first_argument": (
        "void f(void) {\n    first(get(a, b),\n          b);\n"
        "    second(get(a, b), argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
    "nested_different_first_argument": (
        "void f(void) {\n    first(get(a, b),\n          b);\n"
        "    second(get(a, c), argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        (2,)),
    "assert_different_inner_calls": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(&registry, &args.options,\n"
        "                                    &configured, &generic));\n"
        "    ASSERT(!cy_fit_resolve_options(fit, &registry, &generic,\n"
        "                                   &resolved));\n}\n",
        (4,)),
    "assert_same_inner_function": (
        "void f(void) {\n"
        "    ASSERT(!cy_fit_resolve_options(fit,\n"
        "                                   &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(other, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        ()),
    "assert_same_inner_first_argument": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(fit,\n"
        "                                    &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(fit, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        ()),
    "assert_different_inner_first_argument": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(fit,\n"
        "                                    &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(other, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        (2,)),
    "related_chain": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(context, b);\n"
        "    second(argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
}


class CallWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.category = self.rules["P18"].category

    def test_perl_call_wrap_parity(self):
        for name, (source, lines) in CASES.items():
            with self.subTest(case=name):
                extension = "meta.h" if name == "generated_header" else "c"
                path = self.root / "src" / f"{name}.{extension}"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
                comparison = run_checkers([path], self.root,
                                          rules=self.rules)
                legacy = Counter(item.line for item in comparison.legacy
                                 if item.category == self.category)
                newer = Counter(item.line for item in comparison.csane
                                if item.category == self.category)
                expected = Counter(lines)
                self.assertEqual(legacy, expected, comparison.summary())
                self.assertEqual(newer,
                                 Counter(CSANE_EXPECTED.get(name, lines)),
                                 comparison.summary())
                self.assertFalse(comparison.legacy_unknown,
                                 comparison.summary())
                self.assertFalse(comparison.csane_unknown,
                                 comparison.summary())


    def test_related_neighbor_exception(self):
        for name, (source, lines) in RELATED_CASES.items():
            with self.subTest(case=name):
                path = self.root / "src" / f"related_{name}.c"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
                comparison = run_checkers([path], self.root,
                                          rules=self.rules)
                newer = Counter(item.line for item in comparison.csane
                                if item.category == self.category)
                self.assertEqual(newer, Counter(lines),
                                 comparison.summary())
                self.assertFalse(comparison.csane_unknown,
                                 comparison.summary())


if __name__ == "__main__":
    unittest.main()
