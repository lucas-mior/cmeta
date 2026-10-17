#!/usr/bin/env python3
"""Phase 4.1 integration tests; run with the built executable as argv[1]."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []


class CsanecheckCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def source(self, path, contents="int ok(void) { return 1; }\n"):
        location = self.root / path
        location.parent.mkdir(parents=True, exist_ok=True)
        location.write_text(contents, encoding="utf-8")
        return str(location)

    def run_check(self, *args):
        self.assertIsNotNone(EXECUTABLE)
        return subprocess.run(
            [str(EXECUTABLE), *map(str, args)],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
            timeout=30,
        )

    def test_help_and_version(self):
        for argument in ("--help", "--version"):
            result = self.run_check(argument)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("csanecheck", result.stdout)

    def test_explicit_clean_source(self):
        path = self.source("clean.c")
        result = self.run_check("--file", path, "--stats")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("files=1 ", result.stdout)
        self.assertIn("parse_issues=0", result.stdout)
        self.assertIn("preprocessor_issues=0", result.stdout)

    def test_malformed_is_nonfatal_by_default(self):
        path = self.source("malformed.c", "int broken( {\n")
        result = self.run_check("--file", path, "--stats")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("note[", result.stdout)
        self.assertNotIn("parse_issues=0", result.stdout)

    def test_strict_parse_reports_location_and_fails(self):
        path = self.source("malformed.c", "int broken( {\n")
        result = self.run_check("--strict-parse", path)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("malformed.c:1:", result.stdout)
        self.assertIn("note[PARSE-MALFORMED]", result.stdout)

    def test_report_only_does_not_fail(self):
        path = self.source("malformed.c", "int broken( {\n")
        result = self.run_check("--report-parse-issues", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("note[PARSE-MALFORMED]", result.stdout)

    def test_bad_preprocessor(self):
        path = self.source("bad.c", "#endif\nint good;\n")
        result = self.run_check("--strict-parse", path)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("note[PP-STRUCTURE]", result.stdout)

    def test_root_discovery_and_exclusions(self):
        self.source("top.c")
        self.source("src/one.c")
        self.source("src/subdir/two.c")
        self.source("tests/one.h")
        self.source("stc/ignore.c")
        self.source("bin/ignore.c")
        self.source("src/generated.meta.h")
        self.source("cbase/ryu/f2s.c")
        self.source("cbase/ryu/nested/f2s.h")
        one = self.run_check("--root", self.root, "--max-depth", "1",
                             "--stats")
        self.assertEqual(one.returncode, 0, one.stderr)
        self.assertIn("files=5 ", one.stdout)
        two = self.run_check("--max-depth", "2", "--root", self.root,
                             "--stats")
        self.assertEqual(two.returncode, 0, two.stderr)
        self.assertIn("files=6 ", two.stdout)

    def test_duplicate_files_and_file_list(self):
        path = self.source("one.c")
        listing = self.root / "files.txt"
        listing.write_text(f"{path}\r\n\r\n{path}\n", encoding="utf-8")
        result = self.run_check("--file", path, "--file-list", listing,
                                "--file", path, "--stats")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("files=1 ", result.stdout)

    def test_default_root(self):
        self.source("top.h")
        result = self.run_check("--stats")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("files=1 ", result.stdout)

    def test_bad_arguments_and_missing_files(self):
        for args in (("--missing",), ("--file",),
                     ("--max-depth", "-1"), ("--max-depth", "x"),
                     ("--file", "missing.c"), ("--root", "missing"),
                     ("--file-list", "missing.txt")):
            with self.subTest(args=args):
                result = self.run_check(*args)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertTrue(result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
