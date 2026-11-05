#!/usr/bin/env python3
"""Check that i, j and k are never named function parameters."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
RULE = re.compile(r":(\d+):(\d+): error\[STYLE-PARAM-IJK\]")


class ParameterNamesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "parameter_names.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check_names(self, source, expected, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER)]
        if config is not None:
            command.extend(["--config", str(config)])
        command.extend(["--file", str(self.path)])
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column
                 in RULE.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_prototypes_and_definitions(self):
        self.check_names("""void prototype(int32 i, int32 j, int32 k);
static void
implementation(int32 *i, double j[4], int32 k) {
    for (int32 i = 0; i < k; i += 1) {
        int32 j = i;
    }
}
int main(int i, char **argv) { return i; }
""", [(1, 22), (1, 31), (1, 40), (3, 23), (3, 33),
         (3, 45), (8, 14)])

    def test_nested_function_pointer_parameter(self):
        self.check_names("""void use_callback(int32 (*i)(int32 j), int32 data[k]);
void good(double *array, int32 idx);
""", [(1, 27), (1, 36)])

    def test_function_pointer_typedefs_and_fields(self):
        self.check_names("""typedef int32 (*k)(int32 i);
struct Holder {
    int32 (*k)(int32 j);
};
void example(void) {
    int32 (*k)(int32 value);
}
""", [(1, 26), (3, 22)])

    def test_tags_bounds_macros_and_locals_are_not_parameters(self):
        self.check_names("""struct i;
enum j { ONE };
void accept(struct i *ptr, enum j kind, int32 data[static 5]);
void also_accept(int32 data[static k], int32 idx);
void work(void) {
    int32 i = 0;
    for (int32 j = 0; j < 2; j += 1) {
        int32 k = j;
    }
    callback(i, j, k);
}
#define FUNCTION_ARGUMENT(i) (i)
""", [])

    def test_policy_exemption(self):
        policy = self.root / "policy.conf"
        policy.write_text("allow-rule STYLE-PARAM-IJK .*parameter_names[.]c$\n",
                          encoding="utf-8")
        self.check_names("void function(int32 i, int32 j);\n", [], config=policy)


if __name__ == "__main__":
    unittest.main()
