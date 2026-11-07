#!/usr/bin/env python3
"""Direct int64 casts are unnecessary for signed operands of SIZEOF products."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-SIZEOF-INT64-CAST\]: (.*)"
)
MESSAGE = "remove redundant int64 cast before multiplication by SIZEOF"


class SizeofInt64CastTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sizeof_int64_cast.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)], cwd=ROOT,
            capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column), message)
                 for line, column, message in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(
            found, [(line, column, MESSAGE) for line, column in expected],
            result.stdout + result.stderr,
        )

    def test_direct_cast_on_either_side_of_sizeof(self):
        self.check("""int64 f(int32 count, int32 *pointer) {
    int64 a = (int64)count*SIZEOF(*pointer);
    int64 b = SIZEOF(*pointer)*(int64)count;
    int64 c = (int64)(count)*SIZEOF(*pointer);
    int64 d = SIZEOF(*pointer)*(int64)(count);
    int64 e = ( int64 ) /* gap */ count /* gap */ * SIZEOF(*pointer);
    return a + b + c + d + e;
}
""", [(2, 15), (3, 32), (4, 15), (5, 32), (6, 15)])

    def test_macro_replacements_branches_and_nested_calls(self):
        self.check("""#define BAD(N, P) ((int64)N*SIZEOF(*P))
#define OTHER_BAD(N, P) (SIZEOF(*P)*(int64)N)
#define GOOD(N, P) (N*SIZEOF(*P))
#if defined(OPTION)
int64 f(int32 n, char *p) { return (int64)n*SIZEOF(*p); }
#else
int64 f(int32 n, char *p) { return SIZEOF(*p)*(int64)n; }
#endif
int64 g(int32 n, int32 *p) { return use((int64)n*SIZEOF(*p)); }
""", [(1, 20), (2, 37), (5, 36), (7, 47), (9, 41)])

    def test_no_cast_or_nontrivial_cast_is_not_reported(self):
        self.check("""int64 f(int32 count, int32 a, int32 b, int32 *p) {
    int64 v0 = count*SIZEOF(*p);
    int64 v1 = (int64)(a + b)*SIZEOF(*p);
    int64 v2 = (int64)-count*SIZEOF(*p);
    int64 v3 = (int64)count*(int64)other;
    int64 v4 = (uint64)count*SIZEOF(*p);
    int64 v5 = (int64)count*OTHER_SIZEOF(*p);
    int64 v6 = SIZEOF(*p)*(int64)(a + b);
    int64 v7 = SIZEOF(*p)*(int64)-count;
    int64 v8 = (int64)array[index]*SIZEOF(*p);
    int64 v9 = (int64)call()*SIZEOF(*p);
    // (int64)count*SIZEOF(*p)
    char *s = "(int64)count*SIZEOF(*p)";
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9;
}
""", [])


if __name__ == "__main__":
    unittest.main()
