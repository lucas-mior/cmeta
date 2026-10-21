#!/usr/bin/env python3
"""Step 17: high-confidence token-local C style rules."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r"error\[([A-Z][A-Z0-9-]+)\]")


class LocalStyleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "style.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        found = [rule for rule in DIAGNOSTIC.findall(result.stdout)
                 if rule in expected or rule.startswith("FMT-")
                 or rule.startswith("STYLE-")]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_comma_spacing_and_trailing_comma_exception(self):
        self.check("""int32 f(int32 a,int32 b) {
    int32 array[] = {a, b,};
    return a;
}
""", ["FMT-COMMA-SPACING"])
        self.check("""int32 f(int32 a, int32 b) {
    int32 array[] = {a, b,};
    return array[0] + b;
}
""", [])

    def test_member_access_and_designator_spacing(self):
        self.check("""typedef struct S { int32 x; } S;
int32 f(S *s) {
    S local = { . x = 1 };
    return s -> x + local .x;
}
""", ["FMT-MEMBER-SPACING", "FMT-MEMBER-SPACING",
       "FMT-MEMBER-SPACING"])
        self.check("""typedef struct S { int32 x; } S;
int32 f(S *s) {
    S local = { .x = 1 };
    return s->x + local.x;
}
""", [])

    def test_unary_and_cast_spacing(self):
        self.check("""int32 f(int32 value) {
    int32 *pointer = & value;
    return ! value + (int32) value + * pointer;
}
""", ["FMT-UNARY-SPACING", "FMT-UNARY-SPACING",
       "FMT-CAST-SPACING", "FMT-UNARY-SPACING"])
        self.check("""int32 f(int32 value) {
    int32 *pointer = &value;
    return !value + (int32)value + *pointer;
}
""", [])

    def test_pointer_declarator_spacing(self):
        self.check("""void f(int32* a, int32 * b) {
    int32 (* function)(int32) = 0;
}
""", ["FMT-POINTER-SPACING", "FMT-POINTER-SPACING",
       "FMT-POINTER-SPACING"])
        self.check("""void f(int32 *a, int32 *b) {
    int32 (*function)(int32) = 0;
}
""", [])

    def test_function_call_spacing(self):
        self.check("""int32 g(int32 x) { return x; }
int32 f(int32 x) {
    return g (x);
}
""", ["FMT-CALL-SPACING"])
        self.check("""int32 g(int32 x) { return x; }
int32 f(int32 x) {
    return g(x);
}
""", [])

    def test_increment_decrement_policy_and_spacing(self):
        self.check("""int32 f(int32 value) {
    ++ value;
    value --;
    return value;
}
""", ["FMT-UNARY-SPACING", "STYLE-INCDEC",
       "FMT-UNARY-SPACING", "STYLE-INCDEC"])
        self.check("""int32 f(int32 value) {
    value += 1;
    value -= 1;
    return value;
}
""", [])

    def test_ternary_operator_is_not_checked(self):
        self.check("""int32 f(bool condition, int32 a, int32 b) {
    return condition ? a : b;
}
""", [])

    def test_initializer_trailing_comma_rules(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
int32 array[] = {1, 2};
S horizontal = {1, 2};
S vertical = {
    1,
    2
};
S zero = {0};
""", ["FMT-TRAILING-COMMA", "FMT-TRAILING-COMMA"])
        self.check("""typedef struct S { int32 x; int32 y; } S;
int32 array[] = {1, 2,};
S horizontal = {1, 2};
S vertical = {
    1,
    2,
};
S zero = {0};
""", [])


if __name__ == "__main__":
    unittest.main()
