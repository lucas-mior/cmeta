#!/usr/bin/env python3
"""Legacy checker oracle fixtures and differential-harness tests."""

from collections import Counter
import tempfile
import unittest
from pathlib import Path

from differential import (Comparison, CSANE, Finding, load_rules,
                          normalize_csane, normalize_perl, run_checkers)


# These nine categories had no dedicated Perl tests in the Step 1 inventory.
# The positive case must elicit the indicated legacy diagnostic; the negative
# case must not. They are deliberately not considered parity-verified merely
# because a matching csanecheck diagnostic already exists.
UNTESTED_CASES = {
    "P12": (
        "void f(void) {\n    hello(\n        a, b);\n}\n",
        "void f(void) {\n    hello(a, b);\n}\n",
    ),
    "P13": (
        "int32\nfoo(int32 a,\n  int32 b);\n",
        "int32\nfoo(int32 a,\n    int32 b);\n",
    ),
    "P16": (
        'void f(void) {\n    printf("%d %d", a,\n           b);\n}\n',
        'void f(void) {\n    printf("%d %d",\n           a, b);\n}\n',
    ),
    "P17": (
        "void f(void) {\n    draw(x,\n         y, z);\n}\n",
        "void f(void) {\n    draw(x, y, z);\n}\n",
    ),
    "P18": (
        "void f(void) {\n    hello(a,\n          b);\n}\n",
        "void f(void) {\n    hello(a, b);\n}\n",
    ),
    "P22": (
        "void f(void) {\n    ASSERT(x >= 0);\n}\n",
        "void f(void) {\n    ASSERT_GE(x, 0);\n}\n",
    ),
    "P23": (
        'void f(void) {\n    x = STRLIT_LEN("hello");\n}\n',
        "void f(void) {\n    x = STRLIT_LEN(name);\n}\n",
    ),
    "P24": (
        'void f(void) {\n    ok = STREQUAL(STRLIT("hello"), other);\n}\n',
        "void f(void) {\n    ok = STREQUAL(name, other);\n}\n",
    ),
    "P25": (
        'void f(void) {\n    str_printf(&out, "%d", value);\n}\n',
        'void f(void) {\n    str_printf(&out, "value=%d", value);\n}\n',
    ),
}

# Already covered by Perl's own tests. The low-level source/preprocessor
# checks need no complete C parse; compare their records strictly now.
BASIC_CASES = {
    "P01": ("src/sample.c", "// " + "x" * 80 + "\n", "// fine\n"),
    "P02": ("src/sample.c", "\tint32 sample;\n", "int32 sample;\n"),
    "P03": ("src/sample.c", "int32 sample;  \n", "int32 sample;\n"),
    "P04": ("cbase/ryu/sample.h", "#endif // RYU_SAMPLE_H\n",
            "#endif /* RYU_SAMPLE_H */\n"),
    "P05": ("cbase/ryu/sample.c", "int32 sample;", "int32 sample;\n"),
    "P06": ("src/sample.c", "#ifdef FEATURE\n#endif\n",
            "#if defined(FEATURE)\n#endif\n"),
}


class DifferentialTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.assertTrue(CSANE.is_file(),
                        "compile csanecheck first: ./build.sh build")

    def source(self, path, contents):
        name = self.root / path
        name.parent.mkdir(parents=True, exist_ok=True)
        name.write_text(contents, encoding="utf-8")
        return name

    def compare(self, path, source, *, file_list=False):
        return run_checkers([self.source(path, source)], self.root,
                            file_list=file_list, rules=self.rules)

    def test_inventory_is_complete_and_unique(self):
        self.assertEqual(set(self.rules), {f"P{i:02d}" for i in range(1, 26)})
        self.assertEqual(len({r.csane_id for r in self.rules.values()}), 25)
        self.assertEqual(len(UNTESTED_CASES), 9)

    def test_legacy_messages_are_classified(self):
        lines = ("/tmp/space dir/name.c:12:use SIZEOF instead of sizeof\n"
                 "/tmp/space dir/name.c:12:trailing whitespace\n")
        normalized, unknown = normalize_perl(lines, self.root, self.rules)
        self.assertEqual(unknown, [])
        self.assertEqual([x.category for x in normalized],
                         [self.rules["P07"].category,
                          self.rules["P03"].category])
        self.assertEqual([x.line for x in normalized], [12, 12])

    def test_unknown_legacy_diagnostics_are_not_dropped(self):
        _, unknown = normalize_perl("src/x.c:1:brand new Perl rule\n",
                                    self.root, self.rules)
        self.assertEqual(len(unknown), 1)

    def test_csane_only_rules_are_not_false_parity_additions(self):
        records, extras, unknown = normalize_csane(
            "src/a.c:2:9: error[FMT-MULTIPLY]: multiplication spaces\n"
            "src/a.c:2:8: error[STYLE-PRINTF]: format\n",
            self.root, self.rules)
        self.assertEqual(unknown, [])
        self.assertEqual(len(extras), 1)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0].category, self.rules["P25"].category)

    def test_csane_prose_is_not_a_diagnostic(self):
        records, extras, unknown = normalize_csane(
            "Guidance for the user may change independently.\n",
            self.root, self.rules)
        self.assertEqual(records, [])
        self.assertEqual(extras, [])
        self.assertEqual(unknown, [])

    def test_comparison_preserves_duplicate_count(self):
        problem = Finding("src/a.c", 4, "sample", "example")
        comparison = Comparison([problem], [problem, problem], [], [], [])
        self.assertEqual(comparison.missing, Counter())
        self.assertEqual(comparison.added, Counter({problem.location: 1}))
        self.assertFalse(comparison.matches)

    def test_strict_simple_rule_parity(self):
        for key, (path, positive, negative) in BASIC_CASES.items():
            category = self.rules[key].category
            with self.subTest(rule=key, kind="positive"):
                comparison = self.compare(path, positive)
                expected = Counter(x.location for x in comparison.legacy
                                   if x.category == category)
                actual = Counter(x.location for x in comparison.csane
                                 if x.category == category)
                self.assertTrue(expected, f"Perl emitted no {key} finding")
                self.assertEqual(actual, expected, comparison.summary())
                self.assertFalse(comparison.legacy_unknown)
                self.assertFalse(comparison.csane_unknown)
            with self.subTest(rule=key, kind="negative"):
                comparison = self.compare(path, negative)
                self.assertFalse([x for x in comparison.legacy
                                  if x.category == category])
                self.assertFalse([x for x in comparison.csane
                                  if x.category == category])

    def test_exact_column_override_in_file_list(self):
        path = "src/column override.c"
        contents = "// " + "x" * 81 + "\n"
        source = self.source(path, contents)
        comparison = run_checkers([source], self.root, file_list=True,
                                  no_column_limit=source, rules=self.rules)
        self.assertTrue(comparison.matches, comparison.summary())
        self.assertFalse(comparison.legacy)
        self.assertFalse(comparison.csane)

    def test_new_oracle_categories_positive_and_negative(self):
        for key, (positive, negative) in UNTESTED_CASES.items():
            category = self.rules[key].category
            with self.subTest(rule=key, kind="positive"):
                comparison = self.compare(f"src/{key}_positive.c", positive)
                count = sum(x.category == category for x in comparison.legacy)
                self.assertEqual(count, 1, comparison.summary())
                self.assertFalse(comparison.legacy_unknown)
                self.assertFalse(comparison.csane_unknown)
                # The csanecheck side is collected and compared, but the
                # category's parity remains 'partial' until later steps.
                self.assertIsInstance(comparison.missing, Counter)
                self.assertIsInstance(comparison.added, Counter)
            with self.subTest(rule=key, kind="negative"):
                comparison = self.compare(f"src/{key}_negative.c", negative)
                count = sum(x.category == category for x in comparison.legacy)
                self.assertEqual(count, 0, comparison.summary())
                self.assertFalse(comparison.legacy_unknown)
                self.assertFalse(comparison.csane_unknown)

    def test_file_list_preserves_spaces_and_line_positions(self):
        comparison = self.compare("src/with spaces.c",
                                  "#ifdef FEATURE\n#endif\n",
                                  file_list=True)
        self.assertTrue(comparison.matches, comparison.summary())
        self.assertEqual(comparison.legacy[0].path, "src/with spaces.c")
        self.assertEqual(comparison.legacy[0].line, 1)

    def test_ignored_generated_header_has_no_parity_findings(self):
        comparison = self.compare("src/model.meta.h",
                                  "#ifdef BAD\n\tint32 x;\n#endif\n")
        self.assertFalse(comparison.legacy)
        self.assertFalse(comparison.csane)


if __name__ == "__main__":
    unittest.main()
