#!/usr/bin/env python3
"""Report repeated flattened row-major 2D subscripts in a source file."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-MANUAL-2D-INDEX\]")


class Manual2DIndexTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "index.c"
        self.config = self.root / "csanecheck.conf"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, config=""):
        self.source.write_text(source, encoding="utf-8")
        self.config.write_text(config, encoding="utf-8")
        run = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config", str(self.config),
             "--file", str(self.source)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(run.returncode, (0, 1), run.stderr)
        found = [(int(line), int(column))
                 for line, column in DIAGNOSTIC.findall(run.stdout)]
        self.assertEqual([line for line, _ in found], expected_lines,
                         run.stdout + run.stderr)
        for line, column in found:
            self.assertGreater(column, 0)
            self.assertIn("[", source.splitlines()[line - 1])

    def test_single_manual_subscript_allowed(self):
        self.check("""double f(double *a, int row, int cols, int col) {
    return a[row*cols + col];
}
""", [])

    def test_two_manual_subscripts_report_both(self):
        self.check("""double f(double *a, int row, int cols, int col) {
    double x = a[row*cols + col];
    return x + a[col + row*cols];
}
""", [2, 3])

    def test_additive_orders_parentheses_and_casts(self):
        self.check("""double f(double *a, int row, int cols, int col) {
    double a0 = a[row*cols + col];
    double a1 = a[col + row*cols];
    double a2 = a[((row)*(cols) + (col))];
    double a3 = a[(int)row*cols + col];
    double a4 = a[(row + 1)*cols + col];
    double a5 = a[row*cols + 0];
    double a6 = a[row*4 + col];
    return a0 + a1 + a2 + a3 + a4 + a5 + a6;
}
""", [2, 3, 4, 5, 6, 7, 8])

    def test_multiline_subscript_nested_and_member_access(self):
        self.check("""double f(double *a, double **b, int row, int cols,
         int col, int i) {
    double x = a[row*cols
                 + col];
    double y = b[i][row*cols + col];
    double z = (&a[0])[row*cols + col];
    double w = a[ col + (row*cols) ];
    return x + y + z + w;
}
""", [3, 5, 6, 7])

    def test_declarations_literals_and_linear_subscripts(self):
        self.check("""typedef double Numeric;
double f(double *values, int row, int stride, int column, int i) {
    double a[row*stride + column];
    Numeric b[row*stride + column];
    double grid[2][row*stride + column];
    double (*pointer)[row*stride + column];
    double x = values[i];
    double y = values[row*stride];
    double z = values[2*i + 1];
    double w = values[i*2 + 1];
    double t = values[2*4 + 1];
    double u = values[row*stride + column];
    grid[0][1] = values[row*stride + column];
    // values[row*stride + column]
    char *s = "values[row*stride + column]";
    return x + y + z + w + t + u + a[0] + b[0] + s[0];
}
""", [12, 13])

    def test_macro_replacements_and_policy(self):
        text = """#define RAW(A, R, N, C) ((A)[(R)*(N) + (C)])
#define CY_MATRIX_AT(A, R, N, C) ((A)[(R)*(N) + (C)])
#define OTHER(A, R, N, C) ((A)[(R)*(N) + (C)])
"""
        self.check(text, [1, 2, 3])
        self.check(text, [1, 3],
                   "allow-macro-rule STYLE-MANUAL-2D-INDEX .* "
                   "^CY_MATRIX_AT$\n")
        self.check(text, [],
                   "allow-macro-rule STYLE-MANUAL-2D-INDEX .* "
                   "^(CY_MATRIX_AT|OTHER)$\n")
        self.check(text, [], "allow-rule STYLE-MANUAL-2D-INDEX .*\\.c$\n")

    def test_conditional_branches_and_macro_continuation(self):
        self.check("""#if defined(A)
double f(double *a, int r, int n, int c) { return a[r*n + c]; }
#else
double f(double *a, int r, int n, int c) { return a[c + r*n]; }
#endif
#define BAD(A, R, N, C) ((A)[(R)*(N) + \\
                                      (C)])
""", [2, 4, 6])

    def test_not_a_subscript_or_additive_cell(self):
        self.check("""int f(int *a, int row, int cols, int col) {
    int x = row*cols + col;
    int y = a[calculate(row*cols + col)];
    int z = a[row*cols - col];
    int k = a[row + col];
    int j = a[row*cols + col];
    return x + y + z + k + j;
}
""", [])


if __name__ == "__main__":
    unittest.main()
