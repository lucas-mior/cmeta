#!/usr/bin/env python3
"""FMT-ASSIGN-WRAP: unnecessary breaks after compound assignment operators."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[FMT-ASSIGN-WRAP\]")


class CompoundAssignmentWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, name="compound_assignment.c", config=""):
        path = self.root / name
        config_path = self.root / "csanecheck.conf"
        path.write_text(source, encoding="utf-8")
        config_path.write_text(config, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config", str(config_path),
             "--file", str(path)], cwd=ROOT, capture_output=True,
            text=True, check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_reported_example_and_fixed_version(self):
        self.check("""void f(void) {
            failed_trial_evals +=
                progressive_stage.failed_trial_evals;
}
""", [2])
        self.check("""void f(void) {
            failed_trial_evals += progressive_stage.failed_trial_evals;
}
""", [])

    def test_all_assignment_operators(self):
        operators = ("=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                     "<<=", ">>=")
        lines = ["void f(void) {"]
        for operator in operators:
            lines.extend((f"    value {operator}", "        amount;"))
        lines.append("}")
        self.check("\n".join(lines) + "\n", list(range(2, 24, 2)))

    def test_compound_assignment_even_next_to_necessary_wrap(self):
        self.check("""void f(void) {
    failed_trial_evals +=
        progressive_stage.failed_trial_evals;
    result =
        a_really_long_identifier_that_prevents_combining_this_statement_on_one_line;
}
""", [2])

    def test_long_or_complex_assignments_must_stay_wrapped(self):
        self.check("""void f(void) {
    value +=
        a_really_long_name_that_already_uses_up_the_available_width_when_joined;
    value +=
        compute_something(a_really_long_argument_that_forces_a_wrap,
                          another_really_long_argument);
    value += some_value;
    value += next_value
             + another_value;
}
""", [])

    def test_comments_preprocessor_and_generated_file(self):
        source = """void f(void) {
    value += /* keep comment */
        amount;
    value +=
#if defined(OPTION)
        option_one;
#else
        option_two;
#endif
}
"""
        self.check(source, [])
        self.check("void f(void) {\n    value +=\n        amount;\n}\n", [],
                   name="generated.meta.h")
        self.check("void f(void) {\n    value +=\n        amount;\n}\n", [],
                   config="allow-rule FMT-ASSIGN-WRAP .*\n")

    def test_nested_operators_not_standalone_assignments(self):
        self.check("""void f(void) {
    call(value +=
         amount);
    if ((value +=
             amount) > 0) {
        use(value);
    }
    value += amount;
    value == amount;
}
""", [])

    def test_existing_equal_assignment_behavior(self):
        self.check("""void f(void) {
    value =
        amount;
    another = amount;
}
""", [2])


if __name__ == "__main__":
    unittest.main()
