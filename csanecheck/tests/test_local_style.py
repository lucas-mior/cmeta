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
        self.check("""int64 f(int64 count) {
    return SIZEOF(TestFlex) + (count*SIZEOF(int64));
}
""", [])

    def test_capitalized_if_conditions_are_not_casts(self):
        self.check("""void f(int32 B_ptr, int32 C_ptr, int32 D_ptr) {
    if (B_ptr) {
        use(B_ptr);
    }
    if (C_ptr) {
        use(C_ptr);
    }
    if (D_ptr) {
        use(D_ptr);
    }
    int32 x = (int32) B_ptr;
}
""", ["FMT-CAST-SPACING"])

    def test_multiline_pointer_return_is_not_unary(self):
        self.check("""char **
make_pointer(char **pointer) {
    return pointer;
}
char dereference(char **pointer) {
    return ** pointer;
}
""", ["FMT-UNARY-SPACING"])

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
        self.check("""int32 f(int32 *pointer, int32 n) {
    int32 value = 0;
    while (n--) {}
    value = *pointer++;
    int32 cast_value = (int32)*pointer++;
    return value + cast_value;
}
""", [])
        self.check("""int32 f(int32 *pointer, int32 n) {
    int32 value = 0;
    while (n-- > 0) {}
    value = *pointer++ + 1;
    value = (int32)*pointer++ + 1;
    return value;
}
""", ["STYLE-INCDEC", "STYLE-INCDEC", "STYLE-INCDEC"])

    def test_increment_decrement_allowed_in_array_index(self):
        self.check("""void f(double *array, int32 n) {
    int32 i = 0;
    array[i++] = 1.0;
    array[--i] = 2.0;
    array[i ++] = 3.0;
    for (int32 j = 0; j < n; j++);
}
""", ["FMT-UNARY-SPACING", "STYLE-INCDEC"])

    def test_ternary_operator_is_not_checked(self):
        self.check("""int32 f(bool condition, int32 a, int32 b) {
    return condition ? a : b;
}
""", [])

    def test_zero_tolerance_uses_equal_assertion(self):
        self.check("""void f(double a, double b) {
    ASSERT_CLOSE(a, b, 0.0);
    ASSERT_CLOSE(a, b, /* exact */ 0.0);
}
""", ["STYLE-ASSERT-CLOSE-ZERO", "STYLE-ASSERT-CLOSE-ZERO"])
        self.check("""void f(double a, double b, double tolerance) {
    ASSERT_CLOSE(a, b);
    ASSERT_CLOSE(a, b, 0.01);
    ASSERT_CLOSE(a, b, tolerance);
    ASSERT_EQUAL(a, b);
}
""", [])

    def test_initializer_trailing_comma_rules(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
int32 short_array[] = {1, 2, 3};
int32 nested_short[][2] = {{1, 2}, {3, 4}, {5, 6}};
int32 multiline_short[] = {
    1,
    2,
    3
};
int32 long_array[] = {1, 2, 3, 4};
S horizontal = {1, 2};
S vertical = {
    1,
    2
};
S zero = {0};
""", ["FMT-TRAILING-COMMA", "FMT-TRAILING-COMMA"])
        self.check("""typedef struct S { int32 x; int32 y; } S;
int32 short_array[] = {1, 2, 3};
int32 long_array[] = {1, 2, 3, 4,};
S horizontal = {1, 2};
S vertical = {
    1,
    2,
};
S zero = {0};
""", [])


if __name__ == "__main__":
    unittest.main()
