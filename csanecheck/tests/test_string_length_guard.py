#!/usr/bin/env python3
"""Reject redundant length guards before string prefix/suffix macros."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-STRING-LENGTH-GUARD\]")


class StringLengthGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "string_length_guard.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config", "--file",
                   str(self.path)]
        if config is not None:
            command += ["--config", str(config)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=15)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line, _ in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected_lines, result.stdout + result.stderr)

    def test_guards_and_both_macros(self):
        self.check('''void f(void) {
    if ((name->len >= 6 && ENDS_WITH(name->text, name->len, "_count"))
        || (name->len >= 4 && ENDS_WITH(name->text, name->len, "_len"))
        || (name->len >= 4 && ENDS_WITH(name->text, name->len, "_cap"))
        || (name->len >= 9 && ENDS_WITH(name->text, name->len, "_capacity"))) {
        use();
    }
    if (n > 3 && BEGINS_WITH(text, n, "abc")) { use(); }
    bool found = (len >= 3 && ENDS_WITH(text, len, "end"));
    if ((name->len > 3) && BEGINS_WITH(name->text, name->len, "abc")) { use(); }
}
''', [2, 3, 4, 5, 8, 9, 10])

    def test_unrelated_conditions_not_reported(self):
        self.check('''void f(void) {
    if (other_len >= 3 && ENDS_WITH(text, name_len, "abc")) { use(); }
    if (len >= 3 || ENDS_WITH(text, len, "abc")) { use(); }
    if (len >= 3 && ready && ENDS_WITH(text, len, "abc")) { use(); }
    if (len < 3 && ENDS_WITH(text, len, "abc")) { use(); }
    if (len >= 3 && other_function(text, len)) { use(); }
    if (ENDS_WITH(text, len, "abc")) { use(); }
}
''', [])

    def test_rule_exemption(self):
        source = 'void f(void) { if (len >= 3 && ENDS_WITH(text, len, "abc")) { use(); } }\n'
        self.check(source, [1])
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-STRING-LENGTH-GUARD .*\\.c$\n",
                          encoding="utf-8")
        self.check(source, [], config)


if __name__ == "__main__":
    unittest.main()
