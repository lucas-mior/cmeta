#!/usr/bin/env python3
"""Separate first-argument call rules and macro-scoped policy exemptions."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"


class FirstArgumentScopeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "src").mkdir()
        (self.root / "cbase").mkdir()

    def check(self, source, config=""):
        path = self.root / "src" / "scope.c"
        path.write_text(source, encoding="utf-8")
        conf = self.root / "cbase" / "csanecheck.conf"
        conf.write_text(config, encoding="utf-8")
        command = [str(CHECKER), "--config", str(conf), "--file", str(path)]
        result = subprocess.run(command, cwd=self.root, capture_output=True,
                                text=True, check=False)
        self.assertLessEqual(result.returncode, 1, result.stderr)
        return re.findall(r"error\[(FMT-CALL-FIRST(?:-TOPLEVEL)?)\]",
                          result.stdout)

    def test_function_and_toplevel_macro_have_distinct_ids(self):
        source = ("NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS(\n"
                  "    media_lib, MediaLibraryScreen,\n"
                  "    media_lib_screen_can_move_to_previous_column,\n"
                  "    media_lib_screen_can_move_to_next_column,\n"
                  "    media_lib_screen_previous_column, "
                  "media_lib_screen_next_column\n"
                  ")\n"
                  "void f(void) {\n"
                  "    work(\n"
                  "        x, y);\n"
                  "}\n")
        self.assertCountEqual(
            self.check(source),
            ["FMT-CALL-FIRST-TOPLEVEL", "FMT-CALL-FIRST"])

    def test_macro_exception_targets_name_only(self):
        source = ("NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS(\n"
                  "    media_lib, MediaLibraryScreen, something)\n"
                  "OTHER_MACRO(\n"
                  "    a, b)\n"
                  "void f(void) {\n"
                  "    work(\n"
                  "        x, y);\n"
                  "}\n")
        config = ("allow-macro-rule FMT-CALL-FIRST-TOPLEVEL "
                  ".* ^NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS$\n")
        self.assertCountEqual(
            self.check(source, config),
            ["FMT-CALL-FIRST-TOPLEVEL", "FMT-CALL-FIRST"])

    def test_macro_exception_respects_file_regex(self):
        source = "NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS(\n    x, y)\n"
        config = ("allow-macro-rule FMT-CALL-FIRST-TOPLEVEL "
                  "(^|/)different\\.c$ "
                  "^NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS$\n")
        self.assertEqual(self.check(source, config),
                         ["FMT-CALL-FIRST-TOPLEVEL"])

    def test_rule_id_policy_matches_exact_length(self):
        source = "NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS(\n    x, y)\n"
        for rule_id in ("FMT-CALL-FIRST-TOPLEVEL",
                        "FMT-CALL-FIRST-TOPLEVE",
                        "FMT-CALL-FIRST-TOPLEVELX"):
            with self.subTest(rule_id=rule_id):
                config = "allow-rule " + rule_id + " .*\n"
                path = self.root / "src" / "scope.c"
                conf = self.root / "cbase" / "csanecheck.conf"
                path.write_text(source, encoding="utf-8")
                conf.write_text(config, encoding="utf-8")
                result = subprocess.run(
                    [str(CHECKER), "--config", str(conf), "--file", str(path)],
                    cwd=self.root, capture_output=True, text=True,
                    check=False,
                )
                if rule_id != "FMT-CALL-FIRST-TOPLEVEL":
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("invalid csanecheck policy directive",
                                  result.stderr)
                else:
                    self.assertEqual(result.returncode, 0,
                                     result.stdout + result.stderr)
                    self.assertNotIn("FMT-CALL-FIRST-TOPLEVEL",
                                     result.stdout)

    def test_allow_rule_can_exempt_all_toplevel_macro_calls(self):
        source = "NC_SCREEN_COLUMN_CAPABILITY_CALLBACKS(\n    x, y)\n"
        self.assertEqual(self.check(source,
                         "allow-rule FMT-CALL-FIRST-TOPLEVEL .*\n"), [])


if __name__ == "__main__":
    unittest.main()
