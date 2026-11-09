#!/usr/bin/env python3
"""Reject strlen32 calls that directly measure string literals."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-STRLEN32-LITERAL\]")


class Strlen32LiteralTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "strlen32_literal.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_direct_literals_and_parentheses(self):
        self.check('''int32 f(void) {
    int32 a = strlen32("literal");
    int32 b = strlen32("one" "two");
    int32 c = strlen32(("wrapped"));
    int32 d = strlen32(/* before */ "with" /* between */ "comments");
    return a + b + c + d;
}
''', [(2, 15), (3, 15), (4, 15), (5, 15)])

    def test_other_expressions_and_declarations_are_not_rejected(self):
        self.check('''int32 strlen32(char *string);
int32 f(char *string, int32 offset, Object *object) {
    int32 a = strlen32(string);
    int32 b = strlen32(get_string());
    int32 c = strlen32("literal" + offset);
    int32 d = strlen32(offset ? "yes" : "no");
    int32 e = strlen32(STRLIT("literal"));
    int32 f = object->strlen32("literal");
    int32 g = object.strlen32("literal");
    int32 h = my_strlen32("literal");
    return a + b + c + d + e + f + g + h;
}
''', [])

    def test_macro_replacements_and_conditional_branches(self):
        self.check('''#define BAD_LEN strlen32("literal")
#define GOOD_LEN(value) strlen32(value)
#if defined(FIRST)
int32 first = strlen32("first");
#else
int32 second = strlen32("second");
#endif
''', [(1, 17), (4, 15), (6, 16)])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-STRLEN32-LITERAL .*\\.c$\n",
                          encoding="utf-8")
        source = 'int32 length = strlen32("literal");\n'
        self.check(source, [(1, 16)])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
