#!/usr/bin/env python3
"""Independent checking passes and conservative rule-coverage reporting."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class CsanePassTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def scan(self, source, *options):
        path = self.root / "check.c"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(path), *options],
            cwd=self.root, capture_output=True, text=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertNotEqual(result.returncode, 2, result.stdout)
        return result

    def test_source_and_preprocessor_survive_failed_syntax(self):
        result = self.scan("int broken( {\n#ifdef FLAG\n"
                           "int value;\t \n#endif\n", "--coverage",
                           "--rule-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("error[SRC-TABS]", result.stdout)
        self.assertIn("error[SRC-TRAILING]", result.stdout)
        self.assertIn("error[PP-DEFINED]", result.stdout)
        self.assertIn("source=checked tokens=checked", result.stdout)
        self.assertIn("group=SRC status=checked", result.stdout)
        self.assertIn("group=PP status=checked", result.stdout)
        self.assertIn("group=API status=", result.stdout)
        self.assertRegex(result.stdout,
                         r"group=API status=(partial|not_checked)")

    def test_malformed_header_recovers_next_valid_function(self):
        result = self.scan("int broken( {\n}\n"
                           "int valid(void) { STRLIT_LEN(\"a\"); }\n",
                           "--coverage", "--rule-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(result.stdout.count("error[STYLE-STRLIT-LEN]"), 1)
        self.assertIn("unresolved_tokens=", result.stdout)
        self.assertIn("group=STYLE status=partial", result.stdout)

    def test_clean_source_has_complete_syntax_coverage(self):
        result = self.scan("int f(void) { return 1; }\n",
                           "--coverage", "--rule-coverage")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("syntax=checked context=checked", result.stdout)
        self.assertIn("unresolved_tokens=0 macro_tokens=0", result.stdout)
        self.assertIn("group=STYLE status=checked", result.stdout)
        self.assertEqual(result.stdout.count("rule_coverage file="), 9)

    def test_macro_replacement_is_not_claimed_as_parsed(self):
        result = self.scan("#define UNKNOWN(x) ((x)+1)\n"
                           "int f(void) { return 1; }\n",
                           "--coverage", "--rule-coverage")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("syntax=partial", result.stdout)
        self.assertRegex(result.stdout, r"macro_tokens=[1-9]")
        self.assertIn("group=API status=partial", result.stdout)

    def test_preprocessor_errors_do_not_disable_raw_checks(self):
        result = self.scan("#endif\nint value; \n", "--coverage",
                           "--rule-coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("error[SRC-TRAILING]", result.stdout)
        self.assertIn("source=checked tokens=partial", result.stdout)
        self.assertIn("group=SRC status=checked", result.stdout)
        self.assertIn("group=PP status=partial", result.stdout)

    def test_annotated_function_still_checks_literals(self):
        source = ("CY_AD_HELPER\nstatic double\n"
                  "boiler_sat_temperature(double p_bar) {\n"
                  "    a = STRLIT_LEN(\"aaa\");\n"
                  "    return CY_META_POLY(if97_sat_temperature_p, p_bar,\n"
                  "                        PRESSURE_MIN, PRESSURE_MAX,\n"
                  "                        BOILER_IF97_POLY_DEGREE);\n}\n")
        result = self.scan(source, "--coverage")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("error[STYLE-STRLIT-LEN]", result.stdout)
        self.assertIn("function_definitions=1", result.stdout)

    def test_rule_coverage_is_opt_in(self):
        result = self.scan("int f(void) { return 1; }\n")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("coverage_layers", result.stdout)
        self.assertNotIn("rule_coverage", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
