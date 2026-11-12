#!/usr/bin/env python3
"""Keep the first SIZEOF operand on the opening parenthesis line."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[FMT-SIZEOF-FIRST\]: (.*)")
MESSAGE = "keep the first SIZEOF operand on the same line as SIZEOF("


class SizeofFirstTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "sizeof_first.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path), *extra],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column), message)
                 for line, column, message in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(
            found, [(line, column, MESSAGE) for line, column in expected],
            result.stdout + result.stderr,
        )

    def test_user_example_and_legal_wrap(self):
        self.check("""void f(Ctrl *ctrl, int32 ninputs) {
    free2(ctrl->delay_U,
          ctrl->delay_U_capacity_steps*ninputs*SIZEOF(
              *ctrl->delay_U));
    free2(ctrl->delay_U,
          ctrl->delay_U_capacity_steps*ninputs*SIZEOF(*ctrl->delay_U));
}
""", [(3, 54)])

    def test_whitespace_and_comments(self):
        self.check("""int64 f(int32 *p) {
    int64 a = SIZEOF( 
        *p);
    int64 b = SIZEOF(/* comment */
        *p);
    int64 c = SIZEOF(/* comment
        */ *p);
    int64 d = SIZEOF( /* comment */ *p);
    int64 e = SIZEOF(*p);
    int64 f = SIZEOF( /* comment */ (*p));
    return a + b + c + d + e + f;
}
""", [(2, 21), (4, 21), (6, 21)])

    def test_preprocessor_macros_and_conditional_branches(self):
        self.check("""#define BAD(P) SIZEOF( \\
    *P)
#define GOOD(P) SIZEOF(*P)
#if defined(ONE)
int64 a = SIZEOF(
    *pointer);
#else
int64 b = SIZEOF(
    *pointer);
#endif
""", [(1, 22), (5, 17), (8, 17)])

    def test_non_sizeof_mentions_do_not_trigger(self):
        self.check("""// SIZEOF(
//     *pointer)
char *text = "SIZEOF(\\n*p)";
int64 a = OTHER_SIZEOF(
    *pointer);
int64 b = SIZEOF /* comment */ (*pointer);
""", [])

    def test_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule FMT-SIZEOF-FIRST .*\\.c$\n",
                          encoding="utf-8")
        source = "int64 a = SIZEOF(\n    *pointer);\n"
        self.check(source, [(1, 17)])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
