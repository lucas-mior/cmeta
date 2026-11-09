#!/usr/bin/env python3
"""Reject direct STRLIT_LEN calls on string literals."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-STRLIT-LEN\]")


class StrlitLenLiteralTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "strlit_len_literal.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_literal_forms(self):
        self.check('''int32 f(void) {
    int32 a = STRLIT_LEN("literal");
    int32 b = STRLIT_LEN("one" "two");
    int32 c = STRLIT_LEN(("wrapped"));
    int32 d = STRLIT_LEN(/* gap */ "literal");
    return a + b + c + d;
}
''', [(2, 15), (3, 15), (4, 15), (5, 15)])

    def test_nonliteral_and_approved_wrapper(self):
        self.check('''int32 f(char *string, int32 offset) {
    int32 a = STRLIT_LEN(string);
    int32 b = STRLIT_LEN("literal" + offset);
    int32 c = STREQUAL(string, 3, "yes");
    consume_string(STRLIT("literal"));
    return a + b + c;
}
''', [])


if __name__ == "__main__":
    unittest.main()
