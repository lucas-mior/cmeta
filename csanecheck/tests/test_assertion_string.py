#!/usr/bin/env python3
"""Step 13: strict Perl parity for bound assertions and string idioms."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest

from differential import load_rules, run_checkers


# A single violating construct is isolated in each case. In particular,
# negative cases demonstrate that neighboring syntax is not over-matched.
BOUNDS = [
    ("adjacent", "ASSERT_GE(x, 0);\n    ASSERT_LT(x, n);", 1),
    ("member", "ASSERT_GE(s.array[0].field, 0);\n"
     "    ASSERT_LT(s.array[0].field, count);", 1),
    ("comment", "ASSERT_GE(x, 0); /* comment */\n"
     "    ASSERT_LT(x, n);", 1),
    ("upper-expression", "ASSERT_GE(x, 0);\n"
     "    ASSERT_LT(x, size(a, b));", 1),
    ("paren-upper", "ASSERT_GE(x, 0);\n    ASSERT_LT(x, (n));", 1),
    ("other-variable", "ASSERT_GE(x, 0);\n    ASSERT_LT(y, n);", 0),
    ("nonzero-lower", "ASSERT_GE(x, 1);\n    ASSERT_LT(x, n);", 0),
    ("empty-upper", "ASSERT_GE(x, 0);\n    ASSERT_LT(x, );", 0),
    ("same-line", "ASSERT_GE(x, 0); ASSERT_LT(x, n);", 0),
    ("blank-line", "ASSERT_GE(x, 0);\n\n    ASSERT_LT(x, n);", 0),
    ("intervening-statement", "ASSERT_GE(x, 0);\n    work();\n"
     "    ASSERT_LT(x, n);", 0),
    ("semicolon-next-line", "ASSERT_GE(x, 0)\n    ;\n"
     "    ASSERT_LT(x, n);", 0),
    ("upper-semicolon-next-line", "ASSERT_GE(x, 0);\n"
     "    ASSERT_LT(x, n)\n    ;", 0),
]
RELATIONAL = [
    ("greater-equal", "ASSERT(foo >= 0);", 1),
    ("greater", "ASSERT(foo > 123);", 1),
    ("less", "ASSERT(foo < 1);", 1),
    ("less-equal", "ASSERT(foo <= 17);", 1),
    ("underscore", "ASSERT(_foo_ < 11);", 1),
    ("uppercase", "ASSERT(Foo > 0);", 0),
    ("digit-in-name", "ASSERT(foo2 > 0);", 0),
    ("suffixed-integer", "ASSERT(foo > 0U);", 0),
    ("parenthesized-name", "ASSERT((foo) >= 0);", 0),
    ("parenthesized-integer", "ASSERT(foo >= (0));", 0),
    ("complex-condition", "ASSERT(foo >= 0 || other);", 0),
    ("no-semicolon", "ASSERT(foo >= 0)", 0),
    ("comment", "ASSERT(foo >= 0 /* comment */);", 1),
]
STRING_LENGTH = [
    ("simple", 'n = STRLIT_LEN("text");', 1),
    ("adjacent-literals", 'n = STRLIT_LEN("hello" "world");', 1),
    ("initial-literal", 'n = STRLIT_LEN("hello" + suffix);', 1),
    ("comment", 'n = STRLIT_LEN(/* comment */ "hi");', 1),
    ("variable", 'n = STRLIT_LEN(name);', 0),
    ("parenthesized", 'n = STRLIT_LEN(("hi"));', 0),
    ("prefixed", 'n = STRLIT_LEN(u8"hi");', 0),
    ("identifier-prefix", 'n = MY_STRLIT_LEN("hi");', 0),
    ("literal-comment", 'char *s = "STRLIT_LEN(\\\"hi\\\")";', 0),
]
STRING_EQUAL = [
    ("first", 'ok = STREQUAL(STRLIT("hi"), other);', 1),
    ("second", 'ok = STREQUAL(other, STRLIT("hi"));', 1),
    ("initial-literal", 'ok = STREQUAL(STRLIT("hi" + tail), other);', 1),
    ("comment", 'ok = STREQUAL(other, STRLIT(/* c */ "hi"));', 1),
    ("parenthesized", 'ok = STREQUAL((STRLIT("hi")), other);', 0),
    ("parenthesized-inner", 'ok = STREQUAL(STRLIT(("hi")), other);', 0),
    ("prefixed", 'ok = STREQUAL(STRLIT(u8"hi"), other);', 0),
    ("variable", 'ok = STREQUAL(STRLIT(name), other);', 0),
    ("nested-other-call", 'ok = STREQUAL(work(STRLIT("hi")), other);', 0),
    ("other-name", 'ok = NOT_STREQUAL(STRLIT("hi"), other);', 0),
]
PRINTF = [
    ("decimal", 'str_printf(&out, "%d", number);', 1),
    ("zero-padded", 'str_printf(&out, "%04d", number);', 1),
    ("float", 'str_printf(&out, "%-+#0.3f", number);', 1),
    ("long-long", 'str_printf(&out, "%lld", number);', 1),
    ("precision-star", 'str_printf(&out, "%.*f", precision, n);', 1),
    ("four-arguments", 'str_printf(&out, "%d", a, b);', 1),
    ("prefix", 'str_printf(&out, "value=%d", number);', 0),
    ("multiple", 'str_printf(&out, "%d %d", a, b);', 0),
    ("escaped-percent", 'str_printf(&out, "%%", number);', 0),
    ("invalid-precision", 'str_printf(&out, "%.f", number);', 0),
    ("too-many-modifiers", 'str_printf(&out, "%hhhhd", number);', 0),
    ("no-space-before", 'str_printf(&out,"%d", number);', 0),
    ("space-before-comma", 'str_printf(&out, "%d" , number);', 0),
    ("literal-fragments", 'str_printf(&out, "%d" "tail", number);', 0),
    ("no-third-argument", 'str_printf(&out, "%d");', 0),
    ("parenthesized", 'str_printf(&out, ("%d"), number);', 0),
]


class AssertionAndStringParityTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.rules = load_rules()

    def check(self, key, name, source, count):
        path = self.root / "src" / f"{name}.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = run_checkers([path], self.root, rules=self.rules)
        category = self.rules[key].category
        legacy = Counter((item.line, item.category) for item in result.legacy
                         if item.category == category)
        newer = Counter((item.line, item.category) for item in result.csane
                        if item.category == category)
        self.assertEqual(sum(legacy.values()), count, result.summary())
        self.assertEqual(legacy, newer, result.summary())
        self.assertFalse(result.legacy_unknown, result.summary())
        self.assertFalse(result.csane_unknown, result.summary())

    def test_bounds(self):
        for name, statement, count in BOUNDS:
            with self.subTest(case=name):
                self.check("P21", name, "void f(void) {\n    " + statement
                           + "\n}\n", count)

    def test_relational(self):
        for name, statement, count in RELATIONAL:
            with self.subTest(case=name):
                self.check("P22", name, "void f(void) {\n    " + statement
                           + "\n}\n", count)

    def test_string_length(self):
        for name, statement, count in STRING_LENGTH:
            with self.subTest(case=name):
                self.check("P23", name, "void f(void) {\n    " + statement
                           + "\n}\n", count)

    def test_string_equal(self):
        for name, statement, count in STRING_EQUAL:
            with self.subTest(case=name):
                self.check("P24", name, "void f(void) {\n    " + statement
                           + "\n}\n", count)

    def test_printf(self):
        for name, statement, count in PRINTF:
            with self.subTest(case=name):
                self.check("P25", name, "void f(void) {\n    " + statement
                           + "\n}\n", count)

    def test_macro_replacement(self):
        cases = [
            ("P21", "#define BOUNDS(x) do { \\\n"
             "    ASSERT_GE(x, 0); \\\n"
             "    ASSERT_LT(x, limit); \\\n"
             "} while (0)\n"),
            ("P22", "#define CHECK(x) ASSERT(x >= 0);\n"),
            ("P23", '#define STRLEN STRLIT_LEN("hello")\n'),
            ("P24", '#define EQUAL(x) STREQUAL(STRLIT("hi"), x)\n'),
            ("P25", '#define PRINT(x) str_printf(&out, "%d", x)\n'),
        ]
        for key, source in cases:
            with self.subTest(rule=key):
                self.check(key, "macro_" + key, source, 1)

    def test_conditional_alternatives(self):
        samples = [
            ("P21", "ASSERT_GE(x, 0);\nASSERT_LT(x, n);"),
            ("P22", "ASSERT(x >= 0);"),
            ("P23", 'STRLIT_LEN("hi");'),
            ("P24", 'STREQUAL(STRLIT("hi"), value);'),
            ("P25", 'str_printf(&out, "%d", value);'),
        ]
        for key, statement in samples:
            source = ("#if defined(FEATURE)\n" + statement + "\n"
                      "#else\n" + statement + "\n#endif\n")
            with self.subTest(rule=key):
                self.check(key, "branches_" + key, source, 2)

    def test_multiline_locations_and_no_duplicates(self):
        source = ("void f(void) {\n"
                  "    ASSERT(foo >= 0);\n"
                  "    STREQUAL(other,\n"
                  "             STRLIT(\"literal\"));\n"
                  "    str_printf(&out,\n"
                  "               \"%d\", value);\n"
                  "}\n")
        path = self.root / "src/multiline.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        comparison = run_checkers([path], self.root, rules=self.rules)
        categories = {self.rules[key].category for key in
                      ("P22", "P24", "P25")}
        legacy = Counter((item.line, item.category) for item in
                         comparison.legacy if item.category in categories)
        newer = Counter((item.line, item.category) for item in
                        comparison.csane if item.category in categories)
        self.assertEqual(legacy, newer, comparison.summary())
        self.assertEqual(sum(legacy.values()), 3, comparison.summary())
        self.assertEqual(len(legacy), 3, comparison.summary())


if __name__ == "__main__":
    unittest.main()
