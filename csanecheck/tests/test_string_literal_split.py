#!/usr/bin/env python3
"""Report adjacent string literals that fit within the column limit."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
FINDING = re.compile(r":(\d+):\d+: error\[FMT-STRING-SPLIT\]")


class StringLiteralSplitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "string_split.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, options=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", *options,
             "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=10,
        )
        found = [int(line) for line in FINDING.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return result.stdout

    def test_simple_literal_and_sl_macro(self):
        self.check('''void f(void) {
    use("hello"
        " world");
    use(SL("first"
           " second"));
    use(SL(
        "hello"
        "world"));
}
''', [2, 4, 7])

    def test_one_diagnostic_per_group(self):
        self.check('''void f(void) {
    use("one"
        " two"
        " three");
    use("one" " two"
        " three");
}
''', [2, 5])

    def test_long_combined_line_is_allowed(self):
        self.check('''void f(void) {
    use("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890123456789"
        " second");
    use("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890123456789"
        " second", 1, 2, 3, 4, 5, 6);
}
''', [])

    def test_count_entire_resulting_line(self):
        # The combined literal alone fits, but the prefix and suffix matter.
        self.check('''void f(void) {
    some_call_with_a_very_long_name_xyz("123456789012345678901234567890"
                                       "abcdefghijklmno");
    use("123456789012345678901234567890"
        "abcdefghij", 123456789012345678901234567890);
}
''', [])

    def test_single_line_and_nonadjacent_literals(self):
        self.check('''void f(void) {
    use("hello" "world");
    use("hello", "world");
    use("hello" /* explanation */
        "world");
    use("hello" +
        "world");
    use('a', 123);
}
''', [])

    def test_encoding_prefixes(self):
        self.check('''void f(void) {
    use(L"wide"
        L" text");
    use(u8"utf"
        u8"8");
    use(U"wide"
        "narrow");
    use("ordinary"
        L"wide");
}
''', [2, 4])

    def test_escape_safety(self):
        self.check(r'''void f(void) {
    use("hello\n"
        " world");
    use("\x1"
        "2");
    use("\07"
        "4");
    use("\123"
        "4");
    use("?"
        "?/ ");
}
''', [2, 8])

    def test_exact_column_boundary(self):
        def sample(length):
            return ('void f(void) {\n    use("' + 'a'*45 + '"\n'
                    '        "' + 'b'*(length - 45) + '");\n}\n')

        self.check(sample(68), [2])
        self.check(sample(69), [])

    def test_trigraph_boundary_is_not_rewritten(self):
        self.check(r'''void f(void) {
    use("??"
        "/");
    use("?"
        "?=");
}
''', [])

    def test_preprocessor_directives_and_comments(self):
        self.check('''#define TEXT "abc" \\
    "def"
#if defined(A)
void f(void) { use("one"
                   " two"); }
#else
void f(void) { use("three"
                   " four"); }
#endif
''', [4, 7])

    def test_column_limit_and_allow_rule(self):
        source = '''void f(void) {
    use("abcdefghij"
        "klmnop");
}
'''
        self.check(source, [2])
        self.check(source, [], ("--no-column-limit", r"string_split\.c$"))
        config = Path(self.temp.name) / "style.conf"
        config.write_text(
            "allow-rule FMT-STRING-SPLIT string_split\\.c$\n",
            encoding="utf-8",
        )
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
