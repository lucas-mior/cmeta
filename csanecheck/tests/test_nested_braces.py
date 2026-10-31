#!/usr/bin/env python3
"""FMT051: consecutive opening or closing braces need separate lines."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[FMT-NESTED-BRACES\]")


class NestedBracesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "braces.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True,
            check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_nested_arrays_and_blocks(self):
        source = """int32 matrix[2][2] = {{1, 2}, {3, 4}};
void f(void) {{ do_work(); }}
int32 cube[1][1][1] = {{{1}}};
"""
        # The second brace of each consecutive pair is the diagnostic site.
        self.check(source, [(1, 23), (1, 37), (2, 15), (2, 29),
                            (3, 24), (3, 25), (3, 28), (3, 29)])

    def test_whitespace_and_comments_between_braces(self):
        self.check("""int32 a[1][1] = {  {1}  };
int32 b[1][1] = { /* gap */ {2} /* gap */ };
""", [(1, 20), (1, 25), (2, 29), (2, 43)])

    def test_newlines_and_literal_contents_are_exempt(self):
        self.check("""int32 array[1][1] = {
    {
        1,
    },
};
int32 second[1][1] = { /* comment
    */ { 1 } /* comment
    */ };
char *string = "{{ or }}";
char character = '{';
// {{ or }}
/* {{ or }} */
void empty(void) {}
""", [])

    def test_compound_literals_and_designated_initializers(self):
        self.check("""typedef struct S { int32 a[1][1]; } S;
S s = (S){.a = {{1}}};
S t = (S){
    .a = {
        {
            1,
        },
    },
};
""", [(2, 17), (2, 20), (2, 21)])

    def test_macro_replacements_and_alternative_branches(self):
        self.check("""#define ARRAY_PAIR {{1}}
#define MULTILINE_PAIR { \\
    {1} \\
}
#if ONE
int32 a[1][1] = {{1}};
#else
int32 b[1][1] = {{2}};
#endif
""", [(1, 21), (1, 24), (6, 18), (6, 21),
         (8, 18), (8, 21)])

    def test_other_brace_sequences_not_flagged(self):
        self.check("""typedef struct S { int32 x; } S;
S s = {0};
void f(void) {
    if (s.x) {
        use((S){0});
    }
}
""", [])


if __name__ == "__main__":
    unittest.main()
