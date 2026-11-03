#!/usr/bin/env python3
"""Ordered ASSERT comparisons should use the matching comparison macro."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-ASSERT-REL\]: (.*)"
)


class AssertRelTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "assert_rel.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column), message)
                 for line, column, message in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_all_four_operators_with_variable_and_constant_rhs(self):
        self.check("""void f(int32 a, int32 b) {
    ASSERT(a > b);
    ASSERT(a < b);
    ASSERT(a <= b);
    ASSERT(a >= b);
    ASSERT(a > 0);
    ASSERT(a < 1);
    ASSERT(a <= 2);
    ASSERT(a >= 3);
}
""", [
            (2, 5, "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GT_VAR(lhs, rhs)"),
            (3, 5, "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LT_VAR(lhs, rhs)"),
            (4, 5, "use ASSERT_LE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LE_VAR(lhs, rhs)"),
            (5, 5, "use ASSERT_GE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GE_VAR(lhs, rhs)"),
            (6, 5, "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GT_VAR(lhs, rhs)"),
            (7, 5, "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LT_VAR(lhs, rhs)"),
            (8, 5, "use ASSERT_LE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LE_VAR(lhs, rhs)"),
            (9, 5, "use ASSERT_GE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GE_VAR(lhs, rhs)"),
        ])

    def test_parentheses_comments_and_operand_expressions(self):
        self.check("""void f(int32 a, int32 b, int32 c) {
    ASSERT((a > b));
    ASSERT(((a <= b)));
    ASSERT(a /* lhs */ >= /* rhs */ b);
    ASSERT(a + 1 < b*2);
    ASSERT(a > (b + c));
    ASSERT(a < call(b, c));
}
""", [
            (2, 5, "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GT_VAR(lhs, rhs)"),
            (3, 5, "use ASSERT_LE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LE_VAR(lhs, rhs)"),
            (4, 5, "use ASSERT_GE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GE_VAR(lhs, rhs)"),
            (5, 5, "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LT_VAR(lhs, rhs)"),
            (6, 5, "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GT_VAR(lhs, rhs)"),
            (7, 5, "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LT_VAR(lhs, rhs)"),
        ])

    def test_nonstandalone_conditions_are_not_matched(self):
        self.check("""void f(int32 a, int32 b, int32 c, bool flag) {
    ASSERT(a < b && flag);
    ASSERT(a < b || flag);
    ASSERT(a < b < c);
    ASSERT((a < b) == flag);
    ASSERT(a < b ? a : b);
    ASSERT(a == b);
    ASSERT(a != b);
    ASSERT_GT_VAR(a, b);
    ASSERT_LE(a, 2);
    if (a > b) { use(a); }
    // ASSERT(a > b);
    char *message = "ASSERT(a > b)";
}
""", [])

    def test_macro_replacement_and_preprocessor_branches(self):
        self.check("""#define BAD(a, b) ASSERT(a > b)
#define GOOD(a, b) ASSERT_GT_VAR(a, b)
#if defined(ONE)
void f(int32 a, int32 b) {
    ASSERT(a < b);
}
#else
void g(int32 a, int32 b) {
    ASSERT(a >= b);
}
#endif
""", [
            (1, 19, "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GT_VAR(lhs, rhs)"),
            (5, 5, "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_LT_VAR(lhs, rhs)"),
            (9, 5, "use ASSERT_GE(lhs, rhs) for constant rhs, otherwise "
             "ASSERT_GE_VAR(lhs, rhs)"),
        ])


if __name__ == "__main__":
    unittest.main()
