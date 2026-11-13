#!/usr/bin/env python3
"""ASSERT(STREQUAL(...)) should use the string overload of ASSERT_EQ."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-ASSERT-STREQUAL\]")


class AssertStrequalTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "assert_strequal.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, options=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path),
             *options],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stderr)

    def test_three_and_four_string_arguments(self):
        self.check('''void sample(char *name, int32 length, char *other, int32 size) {
    ASSERT(STREQUAL(name, length, "literal"));
    ASSERT(STREQUAL(name, length, other, size));
    ASSERT_EQ(name, length, "literal");
    ASSERT_EQ(name, length, other, size);
}
''', [2, 3])

    def test_wrapped_assert_and_nested_arguments(self):
        self.check('''void sample(char *name, int32 length, char *other) {
    ASSERT((STREQUAL(name, length, "literal")));
    ASSERT(((STREQUAL(name, length, other, strlen32(other)))));
    ASSERT(STREQUAL(get_name(name), length + 1, "literal"));
}
''', [2, 3, 4])

    def test_other_expressions_are_not_equivalent(self):
        self.check('''void sample(char *name, int32 length, bool ok) {
    ASSERT(!STREQUAL(name, length, "literal"));
    ASSERT(STREQUAL(name, length, "literal") && ok);
    ASSERT(STREQUAL(name, length, "literal") == ok);
    ASSERT(ok || STREQUAL(name, length, "literal"));
    ASSERT(helper(STREQUAL(name, length, "literal")));
    ASSERT(STREQUAL(name, length, "literal") ? ok : false);
    ASSERT_CUSTOM(STREQUAL(name, length, "literal"));
    if (STREQUAL(name, length, "literal")) { use(); }
}
''', [])

    def test_parenthesized_arguments_comments_and_macro_replacement(self):
        self.check('''#define EXPECT_SAME(a, n) ASSERT(STREQUAL(a, n, "match"))
#define EXPECT_OTHER(a, n) ASSERT_EQ(a, n, "match")
void sample(char *name, int32 length) {
    ASSERT(/* check */ STREQUAL(name, length, "literal"));
    ASSERT(STREQUAL((name), (length), ("literal")));
}
''', [1, 4, 5])

    def test_conditional_branches(self):
        self.check('''#if FEATURE
void sample(char *name, int32 n) {
    ASSERT(STREQUAL(name, n, "first"));
}
#else
void sample(char *name, int32 n) {
    ASSERT(STREQUAL(name, n, "second"));
}
#endif
''', [3, 7])

    def test_not_a_complete_strequal_call(self):
        self.check('''void sample(char *name, int32 length) {
    ASSERT(STREQUAL(name, length));
    ASSERT(STREQUAL(name, length, "literal", length, 3));
    ASSERT(name.STREQUAL(name, length, "literal"));
    ASSERT((STREQUAL(name, length, "literal")) + 1);
}
''', [])

    def test_allow_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-ASSERT-STREQUAL .*\\.c$\n",
                          encoding="utf-8")
        source = '''void sample(char *name, int32 n) {
    ASSERT(STREQUAL(name, n, "match"));
}
'''
        self.check(source, [2])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
