#!/usr/bin/env python3
"""Prefer SNPRINTF over fmt_sprintf with SIZEOF of a fixed char array."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-SPRINTF-ARRAY\]")


class SprintfArrayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sprintf_array.c"
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

    def test_local_and_file_scope_arrays(self):
        self.check('''static char global[64];
void sample(void) {
    char local[100];
    static char persistent[200];
    fmt_sprintf(local, SIZEOF(local), "%d", 1);
    fmt_sprintf(persistent, SIZEOF(persistent), "%d", 2);
    fmt_sprintf(global, SIZEOF(global), "%d", 3);
    SNPRINTF(local, "%d", 4);
}
''', [5, 6, 7])

    def test_pointer_and_unrelated_capacity(self):
        self.check('''static char global[64];
void sample(char *ptr, char parameter[20]) {
    char buffer[100];
    char *alias = buffer;
    fmt_sprintf(ptr, SIZEOF(ptr), "%s", "a");
    fmt_sprintf(parameter, SIZEOF(parameter), "%s", "a");
    fmt_sprintf(alias, SIZEOF(alias), "%s", "a");
    fmt_sprintf(buffer, SIZEOF(ptr), "%s", "a");
    fmt_sprintf(buffer, 100, "%s", "a");
    fmt_sprintf(buffer + 1, SIZEOF(buffer), "%s", "a");
    fmt_sprintf(global, SIZEOF(*global), "%s", "a");
    item.fmt_sprintf(buffer, SIZEOF(buffer), "%s", "a");
    custom_fmt_sprintf(buffer, SIZEOF(buffer), "%s", "a");
}
''', [])

    def test_declarators_and_wrapping(self):
        self.check('''void sample(void) {
    char first[8], second[16];
    char *pointer, third[32];
    fmt_sprintf((second), (SIZEOF(second)), "%s", "a");
    fmt_sprintf(third, SIZEOF(third), "%s", "a");
    fmt_sprintf(pointer, SIZEOF(pointer), "%s", "a");
    fmt_sprintf(first, SIZEOF(second), "%s", "a");
}
''', [4, 5])

    def test_local_scope_and_shadowing(self):
        self.check('''static char data[64];
void sample(void) {
    fmt_sprintf(data, SIZEOF(data), "%s", "a");
    {
        char *data;
        fmt_sprintf(data, SIZEOF(data), "%s", "a");
    }
    fmt_sprintf(data, SIZEOF(data), "%s", "a");
    {
        char inner[64];
        fmt_sprintf(inner, SIZEOF(inner), "%s", "a");
    }
    fmt_sprintf(inner, SIZEOF(inner), "%s", "a");
}
''', [3, 8, 11])

    def test_parameters_shadow_global_arrays(self):
        self.check('''static char buffer[64];
void sample(char *buffer) {
    fmt_sprintf(buffer, SIZEOF(buffer), "%s", "a");
}
void other(void) {
    fmt_sprintf(buffer, SIZEOF(buffer), "%s", "a");
}
''', [6])

    def test_global_array_in_conditional_function(self):
        self.check('''static char global[256];
static char other[10];
#if FEATURE
void f(void) {
    char local[64];
    fmt_sprintf(local, SIZEOF(local), "%d", 1);
    fmt_sprintf(global, SIZEOF(global), "%d", 1);
}
#else
void f(void) {
    fmt_sprintf(global, SIZEOF(global), "%d", 2);
}
#endif
''', [6, 7, 11])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-SPRINTF-ARRAY .*\\.c$\n",
                          encoding="utf-8")
        sample = '''void sample(void) {
    char text[64];
    fmt_sprintf(text, SIZEOF(text), "hello");
}
'''
        self.check(sample, [3])
        self.check(sample, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
