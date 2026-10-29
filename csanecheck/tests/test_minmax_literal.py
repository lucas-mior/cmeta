#!/usr/bin/env python3
"""MAXOF/MINOF require a type-derived expression, not integer literals."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):\d+: error\[STYLE-MINMAX-LITERAL\]: (.*)"
)
LITERAL_CAST = re.compile(
    r":(\d+):\d+: error\[STYLE-LITERAL-INT-CAST\]: (.*)"
)
MESSAGE = (
    "use a named type limit instead of MAXOF/MINOF "
    "on an integer literal"
)


class MinmaxLiteralTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "minmax.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, expected_call_cast_lines=()):
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
        found_casts = [int(line) for line, _ in
                       LITERAL_CAST.findall(result.stdout)]
        self.assertEqual(found_casts, list(expected_call_cast_lines),
                         result.stdout + result.stderr)

    def test_integer_literals_and_casted_literals(self):
        self.check("""void f(int64 n) {
    if (n >= MAXOF((int32)0)) { return; }
    if (n <= MINOF((int64)0)) { return; }
    if (n >= MAXOF(0)) { return; }
    if (n <= MINOF(-1LL)) { return; }
    if (n >= MAXOF((uint32)0xFFFFFFFFu)) { return; }
    if (n >= MAXOF(((int32)(+123)))) { return; }
    if (n <= MINOF((unsigned long)077ULL)) { return; }
    if (n <= MINOF((int16)0b1010U)) { return; }
}
""", list(range(2, 10)))

    def test_macros_comments_branches_and_nested_calls(self):
        self.check("""#define MAX_CHECK(n) MAXOF((int32)0)
#define MIN_CHECK(n) MINOF((int64)(-1))
#if defined(ALTERNATE)
void a(void) { use(MINOF((int64)/* comment */ (1))); }
#else
void b(void) { use(MAXOF(((uint16)0x10))); }
#endif
void c(void) { use(MAXOF(variable), MINOF((int32)2)); }
""", [1, 2, 4, 6, 8])

    def test_non_literals_and_named_limits_are_valid(self):
        self.check("""int32 get_limit(void);
void f(int32 n, int32 variable) {
    if (n >= INT32_MAX) { return; }
    if (n <= INT32_MIN) { return; }
    if (n >= MAXOF(variable)) { return; }
    if (n <= MINOF(get_limit())) { return; }
    use(MAXOF((int32)variable), MINOF((int64)get_limit()));
    use(MAXOF((int32)(variable + 1)), MINOF((int64)0 + variable));
    use(MAXOF(SOME_CONSTANT), MINOF((int32)CONSTANT_MACRO));
    use(MAXOF(1.5), MINOF((float)0.5));
    use(MAXOF((int32)1 + 2), MINOF(variable - 3));
    // MAXOF((int32)0)
    char *text = "MINOF((int64)0)";
    use((int32)1, MAXOF(get_limit()));
}
""", [], [14])

    def test_no_duplicate_diagnostic_for_cast_literal(self):
        self.check("""void f(void) {
    use(MAXOF((int32)0), MINOF((int64)1));
    use((int32)2, MAXOF((int32)3));
}
""", [2, 2, 3], [3])


if __name__ == "__main__":
    unittest.main()
