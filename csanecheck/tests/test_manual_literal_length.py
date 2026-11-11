#!/usr/bin/env python3
"""Reject explicit string-literal lengths in literal-aware comparison macros."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-LITERAL-LENGTH\]"
)


class ManualLiteralLengthTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "literal_length.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config", "--file",
                   str(self.path)]
        if config is not None:
            command += ["--config", str(config)]
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_literal_lengths_for_comparisons(self):
        self.check('''void f(char *name, int32 name_len) {
    STREQUAL(name, name_len, "test_leaf", 9);
    STREQUAL(name, name_len, "other", 99);
    BEGINS_WITH(name, name_len, "prefix", 6);
    ENDS_WITH(name, name_len, "suffix", 6);
}
''', [(2, 29), (3, 29), (4, 32), (5, 30)])

    def test_literals_wrapped_concatenated_and_comments(self):
        self.check('''void f(char *name, int32 n) {
    STREQUAL(name, n, ("ab" "cd"), 4);
    STREQUAL(name, n, /* comment */ "xyz", 3);
    BEGINS_WITH(name, n, (("foo")), 3);
    ENDS_WITH(name, n, "", 0);
}
''', [(2, 22), (3, 22), (4, 25), (5, 23)])

    def test_first_literal_and_second_literal_both_manual(self):
        self.check('''void f(char *name, int32 n) {
    STREQUAL("abc", 3, name, n);
    STREQUAL("abc", 3, "xyz", 3);
    BEGINS_WITH("abc", 3, "a");
}
''', [(2, 14), (3, 14), (3, 23), (4, 17)])

    def test_three_arg_forms_and_unrelated_numeric_arguments_allowed(self):
        self.check('''void f(char *name, int32 n, char *other, int32 other_len) {
    STREQUAL(name, 3, "yes");
    STREQUAL(name, n, other, other_len);
    BEGINS_WITH(name, n, "pre");
    ENDS_WITH(name, n, "end");
    STREQUAL(name, n, "prefix" + 1, 4);
    STREQUAL(name, n, variable, 3);
    call( "abc", 17 );
}
''', [])

    def test_macro_replacements_and_conditional_branches(self):
        self.check('''#define BAD(s, n) STREQUAL(s, n, "abc", 3)
#define GOOD(s, n) STREQUAL(s, n, "abc")
#if defined(OPTION)
int32 first = STREQUAL(value, n, "one", 3);
#else
int32 second = ENDS_WITH(value, n, "two", 3);
#endif
''', [(1, 33), (4, 33), (6, 35)])

    def test_configured_exemption(self):
        config = Path(self.temp.name) / "checker.conf"
        config.write_text("allow-rule STYLE-LITERAL-LENGTH .*\\.c$\n",
                          encoding="utf-8")
        source = 'int32 ok = STREQUAL(name, n, "abc", 3);\n'
        self.check(source, [(1, 29)])
        self.check(source, [], config=config)


if __name__ == "__main__":
    unittest.main()
