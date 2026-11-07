#!/usr/bin/env python3
"""free2 must use a NULL-safe strlen helper for its size argument."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-FREE2-STRLEN\]")


class Free2StrlenTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "free2_strlen.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_direct_and_null_safe_forms(self):
        self.check("""void f(char *name, char *other) {
    free2(name, strlen32(name) + 1);
    free2(name, opt_strlen32(name) + 1);
    free2(name, strlen32(other) + 1);
    free2(name, strlen32(name));
    free2(name, strlen32(name) + 2);
    free2(name, strlen32(name) + 1 + 1);
    free2(name, strnlen32(name, 80) + 1);
    free2(name, 1 + strlen32(name));
    free2(name, (strlen32(name) + 1));
    free2(name, (strlen32(name) + 1)*SIZEOF(*name));
}
""", [2, 9, 10])

    def test_members_array_subscripts_and_parentheses(self):
        self.check("""void f(char *name, struct State *state, int32 i) {
    free2(state->name, strlen32(state->name) + 1);
    free2(state->names[i], strlen32(state->names[i]) + 1);
    free2((name), (strlen32((name)) + 1));
    free2(state->names[i], strlen32(state->names[i + 1]) + 1);
    free2(state->name, strlen32(state->other) + 1);
    free2(state->name, opt_strlen32(state->name) + 1);
    free2(name, (1 + strlen32(name)));
}
""", [2, 3, 4, 8])

    def test_comments_multiline_branches_and_macros(self):
        self.check("""#define BAD_FREE(p) free2(p, strlen32(p) + 1)
#define GOOD_FREE(p) free2(p, opt_strlen32(p) + 1)
#if defined(USE_FIRST)
void f(char *name) {
    free2(name, /* same pointer */
          strlen32(name) + 1);
}
#else
void g(char *name) {
    free2(name, strlen32(name) + 1);
}
#endif
""", [1, 5, 10])

    def test_no_false_positive_for_other_calls_or_nested_expression(self):
        self.check("""void f(char *name, char *other) {
    record_size(name, strlen32(name) + 1);
    free2(name, strlen32(other) + 1);
    free2(name, strlen32(name) + offset);
    free2(name, strlen32(name) + 1*SIZEOF(*name));
    free2(name, strlen32(name) + (1 + 1));
    free2(name, (int64)strlen32(name) + 1);
    free2(name, (strlen32(name) + 1)*SIZEOF(*name));
}
""", [])

    def test_exemption(self):
        source = "void f(char *name) { free2(name, strlen32(name) + 1); }\n"
        self.check(source, [1])
        config = Path(self.temp.name) / "csanecheck.conf"
        config.write_text(
            "allow-rule STYLE-FREE2-STRLEN .*\\.c$\n", encoding="utf-8",
        )
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
