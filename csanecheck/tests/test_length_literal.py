#!/usr/bin/env python3
"""Reject LENGTH calls on literals without rejecting arrays or wrappers."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-LENGTH-LITERAL\]")


class LengthLiteralTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "length_literal.c"
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

    def test_literal_concat_parentheses_and_comments(self):
        self.check('''int32 f(void) {
    int32 a = LENGTH("literal");
    int32 b = LENGTH("one" "two");
    int32 c = LENGTH(("wrapped"));
    int32 d = LENGTH(/* before */ "with" /* between */ "comments");
    int32 e = LENGTH( ( "nested" ) );
    return a + b + c + d + e;
}
''', [(2, 15), (3, 15), (4, 15), (5, 15), (6, 15)])

    def test_nonliterals_and_approved_wrappers(self):
        self.check('''int32 f(char *string, int32 offset, Object *object) {
    char array[5];
    int32 a = LENGTH(array);
    int32 b = LENGTH(object->values);
    int32 c = LENGTH("literal" + offset);
    int32 d = LENGTH(offset ? "yes" : "no");
    int32 e = LENGTH("literal"[0]);
    int32 f = object->LENGTH("literal");
    int32 g = other_LENGTH("literal");
    int32 h = STREQUAL(string, 3, "yes");
    int32 i = BEGINS_WITH(string, 3, "pre");
    int32 j = ENDS_WITH(string, 3, "end");
    consume_string(STRLIT("literal"));
    Example example = {STRPASS(field, "literal")};
    return a + b + c + d + e + f + g + h + i + j;
}
''', [])

    def test_macro_replacements_and_conditional_branches(self):
        self.check('''#define BAD_COUNT LENGTH("literal")
#define GOOD_COUNT(array) LENGTH(array)
#if defined(FIRST)
int32 first = LENGTH("first");
#else
int32 second = LENGTH("second");
#endif
''', [(1, 19), (4, 15), (6, 16)])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-LENGTH-LITERAL .*\\.c$\n",
                          encoding="utf-8")
        source = 'int32 count = LENGTH("literal");\n'
        self.check(source, [(1, 15)])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
