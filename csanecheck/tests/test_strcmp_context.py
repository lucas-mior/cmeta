#!/usr/bin/env python3
"""Only direct sorting returns and variable assignments may use strcmp."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-STRCMP\]")


class StrcmpContextTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "strcmp.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected_lines, result.stdout + result.stderr)
        self.assertLessEqual(result.returncode, 1, result.stderr)

    def test_direct_return_and_variable_assignment_are_allowed(self):
        self.check("""int strcmp(char *a, char *b);
int compare(char *a, char *b) {
    return strcmp(a, b);
}
int compare_wrapped(char *a, char *b) {
    return ((strcmp(a, b)));
}
void sort(char *a, char *b) {
    int result = strcmp(a, b);
    result = strcmp(a, b);
    int first = strcmp(a, b), second = strcmp(b, a);
    first = (strcmp(a, b));
    if (result) {
        result = strcmp(a, b);
    }
}
""", [])

    def test_conditions_and_inline_assignment_are_forbidden(self):
        self.check("""void compare(char *a, char *b) {
    int result;
    if (strcmp(a, b) == 0) {}
    if (0 != strcmp(a, b)) {}
    if (!strcmp(a, b)) {}
    if ((result = strcmp(a, b)) == 0) {}
    if (ready() && strcmp(a, b) < 0) {}
    while (strcmp(a, b)) {}
    for (; strcmp(a, b); ) {}
    switch (strcmp(a, b)) { default: break; }
    do {} while (strcmp(a, b));
}
""", list(range(3, 12)))

    def test_other_expression_contexts_are_forbidden(self):
        self.check("""int compare(char *a, char *b, int flag) {
    int result = strcmp(a, b) == 0;
    result = flag ? strcmp(a, b) : 0;
    use(strcmp(a, b));
    strcmp(a, b);
    result += strcmp(a, b);
    return strcmp(a, b) == 0;
}
""", [2, 3, 4, 5, 6, 7])

    def test_parenthesized_function_designator(self):
        self.check("""int strcmp(char *a, char *b);
int compare(char *a, char *b) {
    int result = (strcmp)(a, b);
    result = ((strcmp))(a, b);
    if ((strcmp)(a, b) == 0) {}
    if (((strcmp))(a, b)) {}
    return (strcmp)(a, b);
}
""", [5, 6])

    def test_preprocessor_branches_and_macros(self):
        self.check("""#define SAME(a, b) (strcmp((a), (b)) == 0)
#define SORT(a, b) strcmp((a), (b))
int compare(char *a, char *b) {
#if defined(FEATURE)
    if (strcmp(a, b) == 0) {}
    return strcmp(a, b);
#else
    int result = strcmp(a, b);
    if (result) { return strcmp(b, a); }
    if (strcmp(a, b) != 0) {}
    return result;
#endif
}
""", [1, 2, 5, 10])

    def test_identifiers_comments_and_declarations_are_not_calls(self):
        self.check("""int strcmp(char *a, char *b);
void inspect(char *a, char *b, Struct *object) {
    // if (strcmp(a, b)) {}
    char *message = "strcmp(a, b)";
    int strcmp_result = 0;
    object->strcmp(a, b);
    object->strcmp_result = 1;
    if (strequal(a, b)) {}
    if (STREQUAL(a, 1, "x")) {}
}
""", [])


if __name__ == "__main__":
    unittest.main()
