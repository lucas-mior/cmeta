#!/usr/bin/env python3
"""Pointer conditions and redundant free2 guard regression tests."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
RULE = re.compile(r":(\d+):\d+: error\[(STYLE-(?:POINTER-NOT|POINTER-NE-NULL|FREE2-GUARD))\]")


class PointerGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "pointer_guards.c"

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path), *extra],
            cwd=ROOT, text=True, capture_output=True, check=False,
            timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), rule) for line, rule in RULE.findall(result.stdout)]
        self.assertEqual(sorted(found), sorted(expected), result.stdout + result.stderr)

    def test_parameters_conditions_and_assertions(self):
        self.check("""void f(char *ptr, int32 count, bool flag) {
    if (ptr != NULL) { work(); }
    if (NULL != ptr) { work(); }
    if (!ptr) { work(); }
    if (!(ptr)) { work(); }
    if ((count != NULL)) { work(); }
    if (!flag) { work(); }
    ASSERT(ptr != NULL);
    ASSERT(!ptr);
    while (ptr != NULL) { work(); }
    if (ptr == NULL) { work(); }
    if (ptr) { work(); }
}
""", [
            (2, "STYLE-POINTER-NE-NULL"),
            (3, "STYLE-POINTER-NE-NULL"),
            (4, "STYLE-POINTER-NOT"),
            (5, "STYLE-POINTER-NOT"),
        ])

    def test_compound_expressions_and_nested_calls(self):
        self.check("""void f(double *ptr, bool flag, int32 x) {
    if (flag && (ptr != NULL)) { work(); }
    if (!flag || !ptr) { work(); }
    if (check(ptr != NULL)) { work(); }
    if (ptr->field != NULL) { work(); }
    if (!ptr->field) { work(); }
    if (x != NULL) { work(); }
}
""", [
            (2, "STYLE-POINTER-NE-NULL"),
            (3, "STYLE-POINTER-NOT"),
        ])

    def test_local_declarations_shadowing_and_typedefs(self):
        self.check("""typedef char *Handle;
void f(int32 *ptr) {
    if (ptr != NULL) { work(); }
    {
        int32 ptr = 0;
        if (!ptr) { work(); }
        if (ptr != NULL) { work(); }
    }
    if (!ptr) { work(); }
    char *other = NULL;
    if (other != NULL) { work(); }
    char *first, *second;
    if (!first || second != NULL) { work(); }
    Handle alias = NULL;
    if (!alias) { work(); }
    int32 scalar = 0;
    if (!scalar) { work(); }
}
""", [
            (3, "STYLE-POINTER-NE-NULL"),
            (9, "STYLE-POINTER-NOT"),
            (11, "STYLE-POINTER-NE-NULL"),
            (13, "STYLE-POINTER-NOT"),
            (13, "STYLE-POINTER-NE-NULL"),
            (15, "STYLE-POINTER-NOT"),
        ])

    def test_redundant_free_guards_and_non_guards(self):
        self.check("""void f(char *ptr, char *other, bool flag) {
    if (ptr) { free2(ptr, 8); }
    if (ptr != NULL) { free2(ptr, 8); }
    if (NULL != ptr) free2(ptr, 8);
    if (ptr == NULL) { free2(ptr, 8); }
    if (!ptr) { free2(ptr, 8); }
    if (ptr && flag) { free2(ptr, 8); }
    if (ptr) { free2(other, 8); }
    if (ptr) { free2(ptr, 8); work(); }
    if (ptr) { work(); free2(ptr, 8); }
    if (ptr) { free2(ptr, 8); } else { work(); }
    ASSERT(ptr != NULL);
}
""", [
            (2, "STYLE-FREE2-GUARD"),
            (3, "STYLE-FREE2-GUARD"),
            (3, "STYLE-POINTER-NE-NULL"),
            (4, "STYLE-FREE2-GUARD"),
            (4, "STYLE-POINTER-NE-NULL"),
            (6, "STYLE-POINTER-NOT"),
        ])

    def test_grouped_operands_and_member_guards(self):
        self.check("""void f(char *ptr, struct State *state) {
    if ((ptr) != (NULL)) { work(); }
    if ((NULL) != (ptr)) { work(); }
    if ((state->buffer)) { free2(state->buffer, 8); }
    if (state->buffer != NULL) { free2(state->buffer, 8); }
    if (state.child.data) { free2(state.child.data, 8); }
    if (state->buffer) { free2(state->other, 8); }
    if (get_state()->buffer) { free2(get_state()->buffer, 8); }
    if (state->buffer != NULL) { work(); }
}
""", [
            (2, "STYLE-POINTER-NE-NULL"),
            (3, "STYLE-POINTER-NE-NULL"),
            (4, "STYLE-FREE2-GUARD"),
            (5, "STYLE-FREE2-GUARD"),
            (6, "STYLE-FREE2-GUARD"),
        ])

    def test_assertions_and_unrelated_calls_do_not_trigger(self):
        self.check("""void f(char *ptr, bool flag) {
    ASSERT(ptr != NULL);
    ASSERT(!ptr);
    ASSERT((ptr) != NULL);
    if (flag) { ASSERT(ptr != NULL); }
    if (flag) { free2(ptr, 8); }
    if (ptr && flag) { free2(ptr, 8); }
    if (ptr) { free2(ptr, 8); log_event(); }
    if (ptr) { free2(ptr, 8); } else { log_event(); }
}
""", [])

    def test_rule_exemptions(self):
        source = """void f(char *ptr) {
    if (!ptr) { work(); }
    if (ptr != NULL) { free2(ptr, 8); }
}
"""
        expected = [
            (2, "STYLE-POINTER-NOT"),
            (3, "STYLE-POINTER-NE-NULL"),
            (3, "STYLE-FREE2-GUARD"),
        ]
        self.check(source, expected)
        config = Path(self.temp.name) / "csanecheck.conf"
        config.write_text(
            "allow-rule STYLE-POINTER-NOT .*\\.c$\n"
            "allow-rule STYLE-POINTER-NE-NULL .*\\.c$\n"
            "allow-rule STYLE-FREE2-GUARD .*\\.c$\n",
            encoding="utf-8",
        )
        self.check(source, [], ("--config", str(config)))

    def test_for_initializer_shadowing_and_pointer_for_variable(self):
        self.check("""void f(char *ptr) {
    for (int32 ptr = 0; ptr < 2; ptr += 1) {
        if (!ptr) { work(); }
    }
    if (!ptr) { work(); }
    for (char *item = ptr; item; item = NULL) {
        if (item != NULL) { work(); }
    }
    if (item != NULL) { work(); }
}
""", [
            (5, "STYLE-POINTER-NOT"),
            (7, "STYLE-POINTER-NE-NULL"),
        ])

    def test_preprocessor_branches_and_macro_definitions(self):
        self.check("""#define ASSERT_POINTER(p) ASSERT(p != NULL)
#if defined(TEST_ONE)
void f(char *ptr) {
    if (ptr != NULL) { work(); }
}
#else
void g(int32 *ptr) {
    if (!ptr) { work(); }
}
#endif
""", [
            (4, "STYLE-POINTER-NE-NULL"),
            (8, "STYLE-POINTER-NOT"),
        ])


if __name__ == "__main__":
    unittest.main()
