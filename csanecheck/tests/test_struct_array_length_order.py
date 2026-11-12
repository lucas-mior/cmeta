#!/usr/bin/env python3
"""Pointer-array fields precede their associated signed length fields."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-STRUCT-ARRAY-LENGTH-ORDER\]"
)


class StructArrayLengthOrderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "member_order.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config"]
        if config is not None:
            command.extend(["--config", str(config)])
        command.extend(["--file", str(self.path)])
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=10)
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(sorted(found), sorted(expected),
                         result.stdout + result.stderr)

    def test_interleaved_pairs_in_original_example(self):
        self.check("""typedef struct MyStruct {
    double *values;
    int16 nvalues;
    char **values2;
    int64 values2_len;
} MyStruct;
""", [(3, 11)])

    def test_all_lengths_follow_all_pointers(self):
        self.check("""typedef struct MyStruct {
    double *values;
    char **values2;
    char ***values3;
    int16 nvalues;
    int32 values2_len;
    int64 nvalues3;
} MyStruct;
""", [])

    def test_repeated_interleaving_reports_each_early_length(self):
        self.check("""typedef struct MyStruct {
    int32 *one;
    int16 none;
    char **two;
    int32 two_len;
    double *three;
    int64 nthree;
} MyStruct;
""", [(3, 11), (5, 11)])

    def test_unpaired_fields_and_unrelated_lengths_are_not_flagged(self):
        self.check("""typedef struct Example {
    char *first;
    int32 nfirst;
    char *unpaired;
    double x;
    int64 nmissing;
    float *second;
    uint32 second_len;
    int32 *third;
    int32_t third_len;
    int64 unrelated;
} Example;
""", [])

    def test_pointer_qualifiers_and_other_fields(self):
        self.check("""typedef struct Example {
    struct Item *items;
    volatile int32 nitems;
    int64 flag;
    char *restrict names;
    int32 names_len;
} Example;
""", [(3, 20)])

    def test_nested_structs_are_independent(self):
        self.check("""typedef struct Outer {
    struct {
        char *inner;
        int32 ninner;
        double **other;
        int64 other_len;
    } nested;
    int32 *left;
    int32 nleft;
    double *right;
    int32 nright;
} Outer;
""", [(4, 15), (9, 11)])

    def test_function_pointer_and_fixed_array_not_counted(self):
        self.check("""typedef struct Example {
    char *words;
    int32 nwords;
    int32 (*handler)(int32);
    char chars[20];
    int32 chars_len;
    char *next;
} Example;
""", [])

    def test_comments_and_directive_tokens(self):
        self.check("""// typedef struct Fake { char *a; int32 na; char *b; int32 nb; };
#define BAD "struct Fake { char *a; int32 na; char *b; int32 nb; }"
typedef struct Example {
    char *a;
    int32 na;
#if EXTRA
    char **b;
    int32 nb;
#else
    double *other;
    int64 other_len;
#endif
} Example;
""", [(5, 11)])

    def test_different_conditional_alternatives_not_combined(self):
        self.check("""typedef struct Example {
#if FIRST
    char *a;
    int32 na;
#else
    char *b;
    int32 nb;
#endif
} Example;
""", [])

    def test_independent_conditional_groups_can_coexist(self):
        self.check("""typedef struct Example {
#if FIRST
    char *first;
    int32 nfirst;
#endif
#if SECOND
    double **second;
    int64 second_len;
#endif
} Example;
""", [(4, 11)])

    def test_rule_can_be_suppressed(self):
        policy = Path(self.temporary.name) / "policy.conf"
        policy.write_text("allow-rule STYLE-STRUCT-ARRAY-LENGTH-ORDER .*\n",
                          encoding="utf-8")
        self.check("""typedef struct Example {
    char *a;
    int32 na;
    char **b;
    int64 b_len;
} Example;
""", [], config=policy)


if __name__ == "__main__":
    unittest.main()
