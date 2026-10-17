#!/usr/bin/env python3
"""Phase 4.3 policy, canonical-path, and legacy-filter integration tests."""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []


class CsanePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def write(self, path, source):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(source, encoding="utf-8")
        return target

    def check(self, *arguments, env=None):
        values = os.environ.copy()
        for name in ("TEST_FILTER", "TEST_EXCLUDE_PATTERN", "TEST_MAXDEPTH"):
            values.pop(name, None)
        if env:
            values.update(env)
        return subprocess.run([str(EXECUTABLE), *map(str, arguments)],
                              cwd=self.root, env=values, capture_output=True,
                              text=True, timeout=30, check=False)

    def test_default_and_custom_names(self):
        self.write("src/one.c", "void f(void) { strcpy(0, 0); extra(0); }\n")
        self.write("policy.txt", "clear-never-use\nnever-use extra\n")
        result = self.check("--config", "policy.txt", "--root", ".")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout.count("error[API-NEVER]"), 1,
                         result.stdout)

    def test_read_policy_lists(self):
        self.write("src/a.c", "void f(void) { mycopy(); myother(); }\n")
        self.write("never.txt", "# comment\nmycopy\n")
        self.write("only.txt", "myother\n")
        self.write("policy.txt", "never-use-file never.txt\n"
                    "cbase-only-file only.txt\n")
        result = self.check("--config", "policy.txt", "--root", ".")
        self.assertIn("error[API-NEVER]", result.stdout)
        self.assertIn("error[API-CBASE-ONLY]", result.stdout)

    def test_policy_paths_relative_to_config(self):
        self.write("src/a.c", "void f(void) { custom(); memcpy(0,0,1); }\n")
        self.write("settings/never.txt", "custom\n")
        self.write("settings/policy.txt", "never-use-file never.txt\n"
                   "cbase-root ../src\n")
        result = self.check("--config", "settings/policy.txt", "--root", ".")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(result.stdout.count("error[API-NEVER]"), 1,
                         result.stdout)
        self.assertNotIn("error[API-CBASE-ONLY]", result.stdout)

    def test_absolute_file_and_list_outside_project_cwd(self):
        self.write("repo/cbase/functions_never_use.txt", "custom\n")
        self.write("repo/cbase/functions_allowed_cbase_only.txt", "memcpy\n")
        a = self.write("repo/src/a.c", "void f(void) { custom(); }\n")
        b = self.write("repo/cbase/b.c", "void f(void) { memcpy(0,0,1); }\n")
        other = self.root / "other"
        other.mkdir()
        listed = self.write("files.txt", str(a) + "\n" + str(b) + "\n")
        for args in (("--file", a, "--file", b),
                     ("--file-list", listed)):
            with self.subTest(args=args):
                result = subprocess.run([str(EXECUTABLE), *map(str, args)],
                                        cwd=other, capture_output=True,
                                        text=True, timeout=30, check=False)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertEqual(result.stdout.count("error[API-NEVER]"), 1,
                                 result.stdout)
                self.assertNotIn("error[API-CBASE-ONLY]", result.stdout)

    def test_standalone_cbase_ryu_discovery_and_exemption(self):
        self.write("functions_never_use.txt", "strcpy\n")
        self.write("functions_allowed_cbase_only.txt", "memcpy\n")
        self.write("ryu/f2s.c", "int a = sizeof(int);\n")
        self.write("ryu/deep/inner.c", "int b = sizeof(int);\n")
        result = self.check("--root", ".", "--max-depth", "0", "--stats")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("files=2 ", result.stdout)
        self.assertEqual(result.stdout.count("error[PP-SIZEOF]"), 1,
                         result.stdout)

    def test_standard_lists_override_compiled_fallback(self):
        self.write("cbase/functions_never_use.txt", "foo\n")
        self.write("cbase/functions_allowed_cbase_only.txt", "bar\n")
        self.write("src/a.c", "void f(void) { foo(); bar(); strcpy(); }\n")
        result = self.check("--root", ".")
        self.assertEqual(result.stdout.count("error[API-NEVER]"), 1,
                         result.stdout)
        self.assertEqual(result.stdout.count("error[API-CBASE-ONLY]"), 1,
                         result.stdout)

    def test_exclusions_and_rule_exemptions(self):
        self.write("src/ignored.c", "void f(void) { strcpy(); }\n")
        self.write("src/allowed.c", "void f(void) { strcpy(); }\n")
        self.write("src/checked.c", "void f(void) { strcpy(); }\n")
        self.write("policy.txt", r"exclude (^|/)ignored\.c$" + "\n"
                    r"allow-rule API-NEVER (^|/)allowed\.c$" + "\n")
        result = self.check("--root", ".", "--config", "policy.txt")
        self.assertEqual(result.stdout.count("error[API-NEVER]"), 1,
                         result.stdout)
        self.assertIn("checked.c:", result.stdout)
        result = self.check("--root", ".", "--exclude", r"checked\.c$",
                            "--config", "policy.txt", "--stats")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("files=1 ", result.stdout)

    def test_column_config_and_no_column_limit(self):
        self.write("src/a.c", "// " + "x"*92 + "\n")
        self.write("src/b.c", "// " + "x"*92 + "\n")
        self.write("policy.txt", "column-limit 70\n"
                    r"columns (^|/)a\.c$ 100" + "\n"
                    r"no-column-limit (^|/)b\.c$" + "\n")
        result = self.check("--root", ".", "--config", "policy.txt")
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.check("--root", ".", "--no-column-limit", r"b\.c$")
        self.assertEqual(result.stdout.count("error[SRC-COLUMNS]"), 1,
                         result.stdout)

    def test_piscou_config_h_exception(self):
        self.write("piscou.1", "")
        self.write("config.h", "// " + "x"*90 + "\n")
        result = self.check("--root", ".")
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_canonical_cbase_symlinks(self):
        internal = self.write("cbase/internal.c",
                              "void f(void) { memcpy(0, 0, 1); }\n")
        external = self.write("src/external.c",
                              "void f(void) { memcpy(0, 0, 1); }\n")
        (self.root / "src/inside_link.c").symlink_to(internal)
        (self.root / "cbase/external_link.c").symlink_to(external)
        inside = self.check("--root", ".", "--file", "src/inside_link.c")
        # The project also sees the external file and its symlink.
        self.assertIn("src/external.c", inside.stdout)
        self.assertIn("cbase/external_link.c", inside.stdout)
        self.assertNotIn("src/inside_link.c:", inside.stdout)
        self.assertNotIn("cbase/internal.c:", inside.stdout)
        self.assertEqual(inside.returncode, 1, inside.stdout)

    def test_explicit_cbase_root(self):
        internal = self.write("common/internal.c",
                              "void f(void) { memcpy(0, 0, 1); }\n")
        without = self.check("--file", internal)
        with_root = self.check("--cbase-root", "common", "--file", internal)
        self.assertEqual(without.returncode, 1, without.stdout)
        self.assertEqual(with_root.returncode, 0, with_root.stdout)

    def test_legacy_environment_filter_and_headers(self):
        self.write("src/one.c", "void f(void) { strcpy(); }\n")
        self.write("src/two.c", "void f(void) { strcpy(); }\n")
        self.write("src/always.h", "#if defined(OK)\n#endif\n")
        result = self.check("--root", ".", "--stats",
                            env={"TEST_FILTER": "one.c"})
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("files=2 ", result.stdout)
        self.assertNotIn("two.c:", result.stdout)
        result = self.check("--root", ".", "--stats",
                            env={"TEST_FILTER": "one.c",
                                 "TEST_EXCLUDE_PATTERN": r"one\.c$"})
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("files=1 ", result.stdout)

    def test_ryu_selection_and_depth(self):
        self.write("src/a.c", "int a;\n")
        self.write("src/deep/b.c", "int b;\n")
        self.write("cbase/ryu/f2s.c", "int c;\n")
        self.write("cbase/ryu/deep/nested.c", "int d;\n")
        result = self.check("--root", ".", "--stats",
                            env={"TEST_MAXDEPTH": "1", "TEST_FILTER": "ryu"})
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("files=1 ", result.stdout)
        result = self.check("--root", ".", "--stats",
                            env={"TEST_MAXDEPTH": "1"})
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("files=3 ", result.stdout)

    def test_case_insensitive_extension_discovery(self):
        self.write("src/one.C", "int one;\n")
        self.write("src/two.H", "int two;\n")
        result = self.check("--root", ".", "--stats")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("files=2 ", result.stdout)

    def test_invalid_config_regex_and_depth(self):
        self.write("src/a.c", "int a;\n")
        for contents in ("bogus hello\n", "columns abc unknown\n",
                         "exclude [\n", "never-use invalid-name\n",
                         "allow-rule UNKNOWN abc\n"):
            with self.subTest(contents=contents):
                self.write("policy.txt", contents)
                result = self.check("--root", ".", "--config", "policy.txt")
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertTrue(result.stderr)
        self.assertEqual(self.check("--exclude", "[", "--root", ".")
                         .returncode, 2)
        self.assertEqual(self.check("--root", ".", env={"TEST_MAXDEPTH": "x"})
                         .returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
