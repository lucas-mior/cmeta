#!/usr/bin/env python3
"""Nullable string lengths must use opt_strlen32 directly."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-OPT-STRLEN32\]")


class OptStrlen32Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "opt_strlen32.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path),
             *extra],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_nullable_length_expressions(self):
        self.check("""int32 f(char *color, State *state, int32 index) {
    int32 a = color ? strlen32(color) : 0;
    int32 b = state->color ? strlen32(state->color) : 0;
    int32 c = state->colors[index] ? strlen32(state->colors[index]) : 0;
    int32 d = (color) ? (strlen32((color))) : 0;
    return a + b + c + d;
}
""", [(2, 23), (3, 30), (4, 38), (5, 26)])

    def test_comments_macros_and_conditional_branches(self):
        self.check("""#define OPTIONAL_LEN(value) value ? strlen32(value) : 0
#if defined(FIRST)
int32 first(char *name) {
    return name /* condition */ ? strlen32(/* argument */ name) : 0;
}
#else
int32 second(char *name) {
    return name ? (strlen32(name)) : 0;
}
#endif
""", [(1, 37), (4, 35), (8, 20)])

    def test_other_conditional_expressions_are_allowed(self):
        self.check("""int32 f(char *color, char *other, bool enabled) {
    int32 a = opt_strlen32(color);
    int32 b = color ? strlen32(other) : 0;
    int32 c = color ? strlen32(color) : 1;
    int32 d = enabled && color ? strlen32(color) : 0;
    int32 e = color ? strlen32(color) : 0 + offset;
    int32 f = color ? other_len : strlen32(color);
    int32 g = object->strlen32(color);
    return a + b + c + d + e + f + g;
}
""", [])

    def test_rule_exemption(self):
        source = "int32 f(char *s) { return s ? strlen32(s) : 0; }\n"
        self.check(source, [(1, 31)])
        config = Path(self.temp.name) / "csanecheck.conf"
        config.write_text(
            "allow-rule STYLE-OPT-STRLEN32 .*\\.c$\n", encoding="utf-8",
        )
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
