#!/usr/bin/env python3
"""Token-only operator layout and local rule coverage tests."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r"error\[(FMT-[A-Z-]+)\]")
LAYOUT = re.compile(r"group=LAYOUT status=(\w+) pass=(\d+) "
                    r"eligible=(\d+) checked=(\d+) excluded=(\d+) "
                    r"unresolved=(\d+)")


class OperatorLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "operators.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, *, unresolved=0, require=False):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--file", str(self.path),
                   "--rule-coverage"]
        if require:
            command.append("--require-coverage")
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False)
        self.assertEqual(DIAGNOSTIC.findall(result.stdout), expected,
                         result.stdout + result.stderr)
        match = LAYOUT.search(result.stdout)
        self.assertIsNotNone(match, result.stdout)
        status, pass_number, eligible, checked, excluded, missing = (
            match.groups())
        self.assertEqual(pass_number, "1")  # Before any AST parsing.
        self.assertEqual(int(missing), unresolved, result.stdout)
        self.assertEqual(int(eligible), int(checked) + int(excluded)
                         + int(missing))
        self.assertEqual(status, "partial" if unresolved else "checked")
        return result

    def test_multiplication_and_pointer_declarators(self):
        self.check("""typedef int32 MyType;
int32 *f(int32 *p, MyType *q) {
    int32 (*function)(int32) = 0;
    MyType *result = q;
    return *p + *result;
}
""", [])
        self.check("""int32 f(int32 a, int32 b) {
    int32 x = a * b;
    return a* b;
}
""", ["FMT-MULTIPLY", "FMT-MULTIPLY"])

    def test_lowercase_typedef_pointer_and_same_named_variable(self):
        self.check("""typedef int32 custom_t;
custom_t *pointer;
int32 f(int32 custom_t, int32 p) {
    return custom_t *p;
}
""", ["FMT-MULTIPLY"])

    def test_unary_cast_and_binary_ops(self):
        self.check("""int32 f(int32 x, int32 y) {
    int32 *p = &x;
    int32 r = (int32)-x + (int32)*p;
    return r + y;
}
""", [])
        self.check("""int32 f(int32 x, int32 y) {
    return x+y + (x<<y) + (x&&y);
}
""", ["FMT-BINARY-SPACING"]*3)

    def test_assignment_and_initializers(self):
        self.check("""int32 f(void) {
    int32 a=1;
    a+=2;
    a *=3;
    return a;
}
""", ["FMT-ASSIGN-SPACING"]*3)
        self.check("""int32 f(void) {
    int32 a = 1;
    a += 2;
    return a;
}
""", [])

    def test_preprocessor_alternatives_and_macro_replacements(self):
        self.check("""#define ADD(a, b) ((a)+(b))
#if 1
int32 f(int32 x) { return x+x; }
#else
int32 f(int32 x) { return x*x; }
#endif
""", ["FMT-BINARY-SPACING"])

    def test_declaration_cast_vs_multiplication(self):
        self.check("""typedef int32 MyType;
int32 f(int32 x, int32 y) {
    MyType* p = (MyType*)&x;
    return (MyType)*p + x*y;
}
""", [])

    def test_local_incomplete_expression_is_unresolved(self):
        result = self.check("""int32 f(void) {
    int32 x = (1 +;
    return x;
}
""", [], unresolved=1, require=True)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_ast_parse_error_does_not_poison_layout(self):
        self.check("""#if 0
int32 f(int32 a) { return a+a; }
#else
int32 f(int32 a) { return a*a; }
#endif
""", ["FMT-BINARY-SPACING"])


if __name__ == "__main__":
    unittest.main()
