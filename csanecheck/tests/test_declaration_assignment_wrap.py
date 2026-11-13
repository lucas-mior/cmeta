#!/usr/bin/env python3
"""FMT-ASSIGN-WRAP on declarations and independent adjacent assignments."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[FMT-ASSIGN-WRAP\]")


class DeclarationAssignmentWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, name="declaration_assignment.c", config=""):
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

    def test_pointer_declaration_and_single_line_equivalent(self):
        self.check("""void f(void) {
    CySystemRuntime *sys =
        cy_system_from_template(&cy_meta_boiler_combustion);
}
""", [2])
        self.check("""void f(void) {
    CySystemRuntime *sys = cy_system_from_template(&cy_meta_boiler_combustion);
}
""", [])

    def test_primitive_and_qualified_declarations(self):
        self.check("""void f(void) {
    int32 counter =
        compute_count();
    static CySystemRuntime *sys =
        get_system();
}
""", [2, 4])

    def test_global_declaration_initializer(self):
        self.check("""CySystemRuntime *sys =
    get_system();
""", [1])

    def test_necessary_break_stays_unreported(self):
        self.check("""void f(void) {
    CySystemRuntime *sys =
        extremely_long_function_name_that_cannot_fit_with_the_declaration();
    int32 number =
        very_long_function_name_that_would_not_fit_either_when_joined();
}
""", [])

    def test_adjacent_long_assignment_does_not_mask_short_declaration(self):
        self.check("""void f(void) {
    another_result =
        extremely_long_function_name_that_cannot_fit_with_the_assignment();
    CySystemRuntime *sys =
        get_system();
}
""", [4])

    def test_adjacent_long_declaration_does_not_mask_short_assignment(self):
        self.check("""void f(void) {
    CySystemRuntime *some_descriptive_name =
        extremely_long_function_name_that_cannot_fit_with_the_declaration();
    result =
        get_result();
}
""", [4])

    def test_adjacent_ineligible_declaration_does_not_mask_assignment(self):
        self.check("""void f(void) {
    CySystemRuntime *sys =
        get_system(); /* preserve comment */
    result =
        get_result();
}
""", [4])

    def test_unsafe_constructs_and_generated_file(self):
        self.check("""void f(void) {
    CyType item =
        {1, 2};
    CyType *sys = /* preserve comment */
        get_system();
}
""", [])
        self.check("""void f(void) {
    CyType *sys =
        get_system();
}
""", [], name="generated.meta.h")
        self.check("""void f(void) {
    CyType *sys =
        get_system();
}
""", [], config="allow-rule FMT-ASSIGN-WRAP .*\n")


if __name__ == "__main__":
    unittest.main()
