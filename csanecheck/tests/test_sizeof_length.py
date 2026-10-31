#!/usr/bin/env python3
"""Require LENGTH(array) rather than manually dividing SIZEOF expressions."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-SIZEOF-LENGTH\]")


class SizeofLengthTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sizeof_length.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_exact_pattern_and_spacing(self):
        self.check("""int32 f(void) {
    int32 methods[] = {1, 2, 3};
    int32 a = SIZEOF(methods) / SIZEOF(methods[0]);
    int32 b = SIZEOF( methods )/SIZEOF(methods [ 0 ]);
    int32 c = SIZEOF(methods)
              / SIZEOF(methods[0]);
    int32 d = SIZEOF(methods) /* gap */ / /* gap */ SIZEOF(methods[0]);
    return a + b + c + d;
}
""", [(3, 15), (4, 15), (5, 15), (7, 15)])

    def test_valid_and_dissimilar_expressions(self):
        self.check("""int32 f(void) {
    int32 methods[] = {1, 2, 3};
    int32 other[] = {1, 2};
    int32 a = LENGTH(methods);
    int32 b = SIZEOF(methods) / SIZEOF(other[0]);
    int32 c = SIZEOF(methods) / SIZEOF(methods[1]);
    int32 d = SIZEOF(methods) / SIZEOF(methods);
    int32 e = SIZEOF(methods[0]) / SIZEOF(methods[0]);
    int32 g = SIZEOF(methods) * SIZEOF(methods[0]);
    // SIZEOF(methods) / SIZEOF(methods[0])
    char *text = "SIZEOF(methods) / SIZEOF(methods[0])";
    return a + b + c + d + e + g;
}
""", [])

    def test_macro_replacements_and_branches(self):
        self.check("""#define BAD_COUNT(x) (SIZEOF(x) / SIZEOF(x[0]))
#define GOOD_COUNT(x) LENGTH(x)
#if defined(USE_FIRST)
int32 a = SIZEOF(first) / SIZEOF(first[0]);
#else
int32 b = SIZEOF(second) / SIZEOF(second[0]);
#endif
""", [(1, 23), (4, 11), (6, 11)])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-SIZEOF-LENGTH .*\\.c$\n",
                          encoding="utf-8")
        self.check("int32 n = SIZEOF(methods)/SIZEOF(methods[0]);\n",
                   [(1, 11)])
        self.check("int32 n = SIZEOF(methods)/SIZEOF(methods[0]);\n",
                   [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
