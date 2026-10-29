#!/usr/bin/env python3
"""Regression checks for the int64 floor/ceil wrappers."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
ROUND64 = re.compile(r":(\d+):\d+: error\[STYLE-ROUND64\]: (.*)")


class Round64Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "round64.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)], cwd=ROOT,
            capture_output=True, text=True, check=False,
        )
        found = [(int(line), message) for line, message in
                 ROUND64.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_direct_casts_and_nested_calls(self):
        self.check("""int64 f(double x) {
    int64 a = (int64)floor(x);
    int64 b = (int64)ceil(x);
    int64 c = (int64) /* comment */ floor(ceil(x));
    return a + b + c;
}
""", [
            (2, "use floor64(x) instead of (int64)floor(x)"),
            (3, "use ceil64(x) instead of (int64)ceil(x)"),
            (4, "use floor64(x) instead of (int64)floor(x)"),
        ])

    def test_allowed_calls_and_nonmatching_casts(self):
        self.check("""double floor(double x);
double ceil(double x);
int64 f(double x) {
    int64 a = floor64(x);
    int64 b = ceil64(x);
    double c = floor(x) + ceil(x);
    int64 d = (int64)trunc(x);
    int64 e = (uint64)floor(x);
    int64 g = (int64)(floor(x));
    // (int64)floor(x)
    char *text = "(int64)ceil(x)";
    return a + b + (int64)c + d + e + g;
}
""", [])

    def test_preprocessor_replacements_and_branches(self):
        self.check("""#define DOWN(x) ((int64)floor(x))
#define UP(x) ((int64)ceil(x))
#if defined(OPTION)
int64 f(double x) { return (int64)floor(x); }
#else
int64 g(double x) { return (int64)ceil(x); }
#endif
""", [
            (1, "use floor64(x) instead of (int64)floor(x)"),
            (2, "use ceil64(x) instead of (int64)ceil(x)"),
            (4, "use floor64(x) instead of (int64)floor(x)"),
            (6, "use ceil64(x) instead of (int64)ceil(x)"),
        ])


if __name__ == "__main__":
    unittest.main()
