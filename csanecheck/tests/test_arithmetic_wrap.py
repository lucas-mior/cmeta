#!/usr/bin/env python3
"""Arithmetic wrapping and precedence alignment in simple assignments."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[(FMT-ARITH-[A-Z]+)\]")


class ArithmeticWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config="", name="arithmetic.c"):
        path = self.root / name
        config_path = self.root / "csanecheck.conf"
        path.write_text(source, encoding="utf-8")
        config_path.write_text(config, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config", str(config_path),
             "--file", str(path)], cwd=ROOT, capture_output=True,
            text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column), rule)
                 for line, column, rule in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_user_examples(self):
        source = """double f(double a_long_variable, double some_other_variable,
         double xyz, double variable) {
double x = 2.0*a_long_variable
   + (some_other_variable*(xyz + 4.0)) / SQUARE(variable);
double y = 2.0*a_long_variable
               + (some_other_variable*(xyz + 4.0)) / SQUARE(variable);
double z = 2.0*a_long_variable
           + (some_other_variable*(xyz + 4.0)) / SQUARE(variable);
double w = 2.0*a_long_variable
           + (some_other_variable*(xyz + 4.0))
             / SQUARE(variable);
    return x + y + z + w;
}
"""
        self.check(source, [(4, 4, "FMT-ARITH-ALIGN"),
                            (6, 16, "FMT-ARITH-ALIGN")])

    def test_two_alignment_errors_and_correct_precedence(self):
        prefix = "    double x = first_value*cols"
        rhs_column = prefix.index("first_value")
        good = " "*rhs_column + "+ (left_part*right_part)"
        div_column = good.index("(")
        source = (
            "void f(void) {\n" + prefix + "\n"
            + "        + second_value\n"
            + "         / third_value;\n"
            + prefix.replace("x", "y", 1) + "\n"
            + good + "\n"
            + " "*div_column + "/ divisor;\n"
            + "}\n"
        )
        self.check(source, [(3, 9, "FMT-ARITH-ALIGN"),
                            (4, 10, "FMT-ARITH-ALIGN")])

    def test_nested_parentheses_and_multiplication(self):
        first = "    double x = (alpha + beta"
        inner_column = first.index("alpha")
        outer_column = first.index("(")
        source = (
            "void f(void) {\n" + first + "\n"
            + " "*inner_column + "+ gamma)\n"
            + " "*outer_column + "* delta;\n"
            + first.replace("x", "y", 1) + "\n"
            + "    + gamma)\n"
            + "   * delta;\n"
            + "}\n"
        )
        self.check(source, [(6, 5, "FMT-ARITH-ALIGN"),
                            (7, 4, "FMT-ARITH-ALIGN")])

    def test_break_after_arithmetic_operator(self):
        source = """void f(void) {
    double x = first +
               second;
    double y = first*
               second;
    double z = first /
               second;
    double w = first %
               second;
    double good = first
                  + second;
}
"""
        self.check(source, [(2, 22, "FMT-ARITH-WRAP"),
                            (4, 21, "FMT-ARITH-WRAP"),
                            (6, 22, "FMT-ARITH-WRAP"),
                            (8, 22, "FMT-ARITH-WRAP")])

    def test_function_calls_and_subscripts_remain_opaque(self):
        source = """double f(double *array, double a, double b) {
    double x = function_call(a +
                             b);
    double y = array[a +
                     b];
    double z = function_call(a, b)
               + array[0];
    do_work(a +
            b);
    return a +
           b;
}
"""
        self.check(source, [])

    def test_non_simple_assignments_are_excluded(self):
        source = """void f(void) {
    double x = condition ? a +
                           b : c;
    double y = (double)a +
               b;
    double a = 1, b = 2 +
                          3;
    if ((x = a +
              b) > 0) {
        return;
    }
    x += a +
         b;
    x = array[a +
              b];
}
"""
        self.check(source, [])

    def test_global_assignment_and_unary(self):
        source = """double global = a_long_name
    + other;
void f(void) {
    double negated = -value
                     + offset;
    double ptr = *pointer
                 + count;
}
"""
        self.check(source, [(2, 5, "FMT-ARITH-ALIGN")])

    def test_exemptions_generated_files_and_comments(self):
        source = """void f(void) {
    double x = first /* comment */
   + second;
    char *literal = "first +\\n second";
    // double fake = a +
    //     b;
}
"""
        self.check(source, [(3, 4, "FMT-ARITH-ALIGN")])
        self.check(source, [], "allow-rule FMT-ARITH-ALIGN .*\n")
        self.check(source, [], name="generated.meta.h")

    def test_conditional_branches(self):
        source = """#if defined(A)
void f(void) {
    double x = first
      + second;
}
#else
void f(void) {
    double x = first
       + second;
}
#endif
"""
        self.check(source, [(4, 7, "FMT-ARITH-ALIGN"),
                            (9, 8, "FMT-ARITH-ALIGN")])


if __name__ == "__main__":
    unittest.main()
