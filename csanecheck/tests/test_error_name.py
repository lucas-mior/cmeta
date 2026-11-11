#!/usr/bin/env python3
"""Disallow variables, members and parameters named error."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-ERROR-NAME\]")


class ErrorNameTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "error_name.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config"]
        if config is not None:
            command += ["--config", str(config)]
        command += ["--file", str(self.path)]
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_variables_globals_locals_and_for_initializers(self):
        self.check('''int32 error;
static char *error = NULL;
void f(void) {
    int32 error = 0;
    int32 ok, error = 1;
    for (int32 error = 0; error < 2; error += 1) {
        error("message");
    }
}
''', [(1, 7), (2, 14), (4, 11), (5, 15), (6, 16)])

    def test_parameters_and_function_pointers(self):
        self.check('''void f(int32 error, char **error);
void g(void (*error)(int32), int32 (*callback)(int32 error));
static int32 (*error)(int32 value);
void g(void (*callback)(int32 error)) {
    return;
}
''', [(1, 14), (1, 28), (2, 15), (2, 54), (3, 16), (4, 31)])

    def test_struct_and_union_members(self):
        self.check('''typedef struct Something {
    int32 error;
    char *error;
    struct Child {
        int32 error;
    } child;
} Something;
union Data {
    int32 error;
    double ok;
};
''', [(2, 11), (3, 11), (5, 15), (9, 11)])

    def test_references_types_macros_and_literals_are_not_declarations(self):
        self.check('''#define error(...) printer(__VA_ARGS__)
// error must not appear in a declaration here.
typedef int32 error;
struct error { int32 value; };
enum error { ERROR_FIRST = 1 };
struct error *node;
int32 error(void);
void f(struct error value, int32 *result) {
    int32 current = value.value;
    int32 other = result[error];
    int32 number = current + error;
    error("message");
    *result = value.error;
    char *message = "error";
}
''', [])

    def test_conditional_declarations(self):
        self.check('''#if defined(OPTION)
int32 error;
#else
int32 error;
#endif
''', [(2, 7), (4, 7)])

    def test_configured_exemption(self):
        config = Path(self.temp.name) / "policy.conf"
        config.write_text("allow-rule STYLE-ERROR-NAME .*\\.c$\n",
                          encoding="utf-8")
        self.check("int32 error;\n", [(1, 7)])
        self.check("int32 error;\n", [], config=config)


if __name__ == "__main__":
    unittest.main()
