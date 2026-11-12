#!/usr/bin/env python3
"""Reject redundant strlen32 calls when a struct already owns the length."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-STRLEN32-REDUNDANT\]")


class Strlen32StructMemberTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "strlen32_struct_member.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_struct_member_in_strequal_only(self):
        self.check('''void f(State *state, State other, char *name) {
    STREQUAL(state->name, strlen32(state->name), name, strlen32(name));
    STREQUAL(other.name, strlen32(other.name), "abc");
    STREQUAL(state->inner.name, (strlen32((state->inner.name))), name, 3);
    consume(strlen32(state->name));
    STREQUAL(name, strlen32(name), state->name, state->name_len);
}
''', [(2, 27), (3, 26), (4, 34)])

    def test_array_item_always_and_no_duplicates_in_strequal(self):
        self.check('''void f(State *state, State other, int32 i, int32 j) {
    int32 a = strlen32(state->names[i]);
    int32 b = strlen32(other.names[i + 1]);
    int32 c = strlen32((state->names[lookup(i, j)]));
    STREQUAL(state->names[i], strlen32(state->names[i]), "abc");
    STREQUAL(other.names[i], strlen32(other.names[i]), state->name, 3);
    consume(strlen32(state->inner.names[j]));
}
''', [(2, 15), (3, 15), (4, 15), (5, 31), (6, 30), (7, 13)])

    def test_nested_members_and_whitespace(self):
        self.check('''void f(State *state, int32 i) {
    STREQUAL(state->items[i].name, strlen32(state->items[i].name), "x");
    int32 n = strlen32(
        state /* root */ -> /* member */ names [i]
    );
}
''', [(2, 36), (3, 15)])

    def test_no_false_positive_on_variables_calls_or_other_expressions(self):
        self.check('''void f(State *state, int32 i, char *name) {
    int32 a = strlen32(state->name);
    int32 b = strlen32(name);
    int32 c = strlen32(state->names);
    int32 d = strlen32(state->names[i] + 1);
    int32 e = strlen32(state->names[i][0]);
    int32 g = strlen32(get_string(state->names[i]));
    int32 h = strlen32(state->make_name(i));
    int32 j = strlen32(names[i]);
    STREQUAL(name, strlen32(state->name + 1), "abc");
    STREQUAL(name, strlen32(get_string(state->name)), "abc");
}
''', [])

    def test_macro_replacements_and_preprocessor_branches(self):
        self.check('''#define MEMBER_EQ(x) STREQUAL(x->name, strlen32(x->name), "name")
#define ARRAY_LEN(x, i) strlen32(x->names[i])
#if defined(USE_A)
int32 f(State *state) { return strlen32(state->names[0]); }
#else
int32 g(State *state) { return strlen32(state->names[1]); }
#endif
''', [(1, 40), (2, 25), (4, 32), (6, 32)])

    def test_malformed_arguments_are_not_diagnosed(self):
        self.check('''void f(State *state, int32 i) {
    int32 x = strlen32(state->names[);
    STREQUAL(state->name, strlen32(state->name + 1), "a");
}
''', [])

    def test_rule_exemption(self):
        source = '''void f(State *state) {
    STREQUAL(state->name, strlen32(state->name), "x");
    strlen32(state->names[0]);
}
'''
        self.check(source, [(2, 27), (3, 5)])
        config = Path(self.temp.name) / "csane.conf"
        config.write_text(
            "allow-rule STYLE-STRLEN32-REDUNDANT .*\\.c$\n", encoding="utf-8",
        )
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
