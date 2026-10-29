#!/usr/bin/env python3
"""Direct integer literal arguments should not be cast to int32/int64."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):\d+: error\[STYLE-LITERAL-INT-CAST\]: (.*)"
)
MESSAGE = "pass integer literals without int32/int64 casts"


class LiteralIntCastTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "literal_cast.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)], cwd=ROOT,
            capture_output=True, text=True, check=False,
        )
        found = [(int(line), message) for line, message in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(
            found, [(line, MESSAGE) for line in expected_lines],
            result.stdout + result.stderr,
        )

    def test_calls_macros_and_integer_forms(self):
        self.check("""void f(int32 x) {
    call_this_function(x, (int64)1);
    call_this_function((int32)0, (int64)-123);
    MY_MACRO((int32)+8, (int64)0xFFu);
    MY_MACRO((int32)077, (int64)0b1010ULL);
    use((int64)1ULL, (int32)42L, (int64)0XAB);
    nest(call_this_function((int32)5), (int64)6);
    obj.method((int64)7);
    obj->method((int32)8);
}
""", [2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 8, 9])

    def test_wrapped_comments_and_preprocessor(self):
        self.check("""#define BAD(x) use(x, (int64)1)
#define BAD2() MY_MACRO((int32)2)
#define GOOD() use((int32)value)
#if defined(OPTION)
void f(void) { use(((int32)1)); }
#else
void g(void) { use((int64) /* comment */ (-2)); }
#endif
void h(void) { use((int32)(+3), ((int64)(0x4))); }
""", [1, 2, 5, 7, 9, 9])

    def test_allowed_argument_expressions_and_unrelated_constructs(self):
        self.check("""int32 f(int32 x) {
    int32 a = (int32)1;
    int64 b = (int64)-1;
    use(1, -1, 2ULL, (int32)x, (int64)foo());
    use((int32)1 + 2, (int64)(1 + 2), (int32)SOME_CONSTANT);
    use((int32)1.5, (int64)1e3, (int32)0x1p3);
    use((int64)'a', (int32)"1", (int32)0.5f);
    use((uint64)1, (int16)1, (int64)(int32)1);
    if ((int32)1) { while ((int64)2) { break; } }
    switch ((int64)1) { default: break; }
    int32 size = sizeof((int64)1);
    return a + b + size + x;
}
""", [])

    def test_skip_noncode_and_distinct_nested_arguments(self):
        self.check("""void f(void) {
    // use((int64)1);
    char *text = "use((int32)1)";
    use(1 + (int64)2, ((int32)1 + 2));
    use(other((int64)3), (int32)4);
}
""", [5, 5])


if __name__ == "__main__":
    unittest.main()
