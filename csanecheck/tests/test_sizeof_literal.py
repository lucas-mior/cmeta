#!/usr/bin/env python3
"""Reject SIZEOF calls whose operand is a string literal."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-SIZEOF-LITERAL\]")


class SizeofLiteralTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sizeof_literal.c"
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

    def test_literal_and_nonliteral_arguments(self):
        self.check("""int32 f(char *string) {
    int64 bad = SIZEOF("literal");
    int64 good = SIZEOF(*string);
    int64 expression = SIZEOF("literal"[0]);
    return (int32)(bad + good + expression);
}
""", [(2, 17)])

    def test_concatenated_and_parenthesized_literals(self):
        self.check('''int32 f(char *string, int32 offset) {
    int64 a = SIZEOF("one" "two");
    int64 b = SIZEOF(("wrapped"));
    int64 c = SIZEOF(/* gap */ "literal");
    int64 d = SIZEOF("literal" + offset);
    int64 e = SIZEOF(offset ? "yes" : "no");
    return a + b + c + d + e;
}
''', [(2, 15), (3, 15), (4, 15)])

    def test_macro_replacements_and_branches(self):
        self.check("""#define BAD_SIZE SIZEOF("literal")
#if defined(FIRST)
int64 first = SIZEOF("first");
#else
int64 second = SIZEOF("second");
#endif
""", [(1, 18), (3, 15), (5, 16)])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-SIZEOF-LITERAL .*\\.c$\n",
                          encoding="utf-8")
        source = "int64 size = SIZEOF(\"literal\");\n"
        self.check(source, [(1, 14)])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
