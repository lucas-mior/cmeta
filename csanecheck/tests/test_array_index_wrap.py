#!/usr/bin/env python3
"""FMT-ARRAY-FIRST: an array subscript starts on the '[' line."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[FMT-ARRAY-FIRST\]: (.*)"
)
MESSAGE = "array index must begin on the opening bracket's line"


class ArrayIndexWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source_path = self.root / "array_index_wrap.c"
        self.config_path = self.root / "csanecheck.conf"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config="", name=None):
        path = self.source_path if name is None else self.root / name
        path.write_text(source, encoding="utf-8")
        self.config_path.write_text(config, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--config", str(self.config_path),
             "--file", str(path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column), message)
                 for line, column, message in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found,
                         [(line, column, MESSAGE)
                          for line, column in expected],
                         result.stdout + result.stderr)

    def test_bad_examples_and_good_alternatives(self):
        self.check("""int32 f(int32 *my_array_has_a_long_name,
        int32 variable_long_name, int32 cols, int32 another_variable) {
    int32 value = my_array_has_a_long_name[
        variable_long_name];
    int32 bad_arithmetic = my_array_has_a_long_name[
        variable_long_name*cols + another_variable];
    int32 bad_call = my_array_has_a_long_name[
        function_that_returns_index_does_not_fit_here()];
    int32 good = my_array_has_a_long_name[variable_long_name];
    int32 good_arithmetic = my_array_has_a_long_name[variable_long_name*cols
                                           + another_variable];
    int32 idx = function_that_returns_index_does_not_fit_here();
    int32 good_call = my_array_has_a_long_name[idx];
    return value + bad_arithmetic + bad_call + good + good_arithmetic
           + good_call;
}
""", [(3, 43), (5, 52), (7, 46)])

    def test_nested_indexing_and_comments(self):
        self.check("""int32 f(int32 **matrix, int32 *a, int32 i, int32 j) {
    int32 x = matrix[i][
        j];
    int32 y = (a)[
        i];
    int32 z = a[ /* comment */
        i];
    int32 ok = a[i /* comment
        inside subscript */ + j];
    // a[
    char *literal = "a[\\n i]";
    return x + y + z + ok;
}
""", [(2, 24), (4, 18), (6, 16)])

    def test_declarators_and_designators_are_not_subscripts(self):
        self.check("""int32 data[
    10];
int32 grid[2][
    3];
typedef int32 Row[
    5];
int32 first, second[
    6];
int32 initialized = 0, another[
    4];
void f(int32 param[
    8]) {
    int32 local[
        4];
    int32 (*array_pointer)[
        3];
    int32 init[2] = {[
        0] = 1, [1] = 2};
    int32 value = data[
        param[0]];
    int32 initialized_subscript = data[
        param[0]], other = 1;
}
""", [(19, 23), (21, 39)])

    def test_preprocessor_branches_and_rule_exemption(self):
        source = """#if defined(OPTION)
int32 f(int32 *a, int32 i) { return a[
    i]; }
#else
int32 f(int32 *a, int32 i) { return a[
    i + 1]; }
#endif
"""
        self.check(source, [(2, 38), (5, 38)])
        self.check(source, [], "allow-rule FMT-ARRAY-FIRST .*\n")

    def test_macro_replacements(self):
        self.check("""#define BAD(A, I) A[ \\
    I]
#define ALSO_BAD(A, I) A[ /* comment */ \\
    I]
#define GOOD(A, I) A[I]
""", [(1, 20), (3, 25)])

    def test_generated_header_exemption(self):
        self.check("""int32 f(int32 *a, int32 i) { return a[
    i]; }
""", [], name="generated.meta.h")


if __name__ == "__main__":
    unittest.main()
