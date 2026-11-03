#!/usr/bin/env python3
"""Regression tests for sorted API-policy name lookup."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"
API_ISSUE = re.compile(r":(\d+):\d+: error\[(API-[A-Z-]+)\]")


class ApiNameLookupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "src" / "sample.c"
        self.source.parent.mkdir(parents=True)
        self.config = self.root / "policy.conf"

    def check(self, expected, coverage=False):
        command = [str(CSANE), "--config", str(self.config),
                   "--file", str(self.source)]
        if coverage:
            command.append("--rule-coverage")
        result = subprocess.run(command, cwd=self.root, capture_output=True,
                                text=True, check=False)
        issues = [(int(line), rule) for line, rule in
                  API_ISSUE.findall(result.stdout)]
        self.assertEqual(issues, expected, result.stdout + result.stderr)
        self.assertEqual(result.returncode, 1 if expected else 0,
                         result.stdout + result.stderr)
        return result

    def test_unordered_names_prefixes_duplicates_and_coverage(self):
        self.config.write_text("""clear-never-use
clear-cbase-only
never-use zzz_api
never-use a_api
never-use bb_api
never-use a_api
cbase-only z_only
cbase-only c_only
cbase-only c_only
""", encoding="utf-8")
        self.source.write_text("""void f(void) {
    zzz_api();
    a_api();
    bb_api();
    a_api_long();
    zzz_api_extra();
    obj.a_api();
    c_only();
    z_only();
    c_only_long();
    z_only_extra();
    ignored();
}
#define BAD() zzz_api()
""", encoding="utf-8")
        result = self.check([
            (2, "API-NEVER"), (3, "API-NEVER"), (4, "API-NEVER"),
            (8, "API-CBASE-ONLY"), (9, "API-CBASE-ONLY"),
            (14, "API-NEVER"),
        ], coverage=True)
        self.assertIn("API", result.stdout)

    def test_loaded_lists_then_added_names(self):
        (self.root / "never.txt").write_text(
            "# ignore comments\nz_foo\na_foo\na_foo\n",
            encoding="utf-8")
        (self.root / "restricted.txt").write_text(
            "y_bar\nb_bar\n", encoding="utf-8")
        self.config.write_text("""never-use-file never.txt
cbase-only-file restricted.txt
never-use m_foo
cbase-only c_bar
""", encoding="utf-8")
        self.source.write_text("""void f(void) {
    a_foo();
    m_foo();
    z_foo();
    a_foo_extra();
    b_bar();
    c_bar();
    y_bar();
    b_bar_more();
    strcpy(dst, src);
    memcpy(dst, src, n);
}
""", encoding="utf-8")
        self.check([
            (2, "API-NEVER"), (3, "API-NEVER"), (4, "API-NEVER"),
            (6, "API-CBASE-ONLY"), (7, "API-CBASE-ONLY"),
            (8, "API-CBASE-ONLY"),
        ])

    def test_cleared_empty_lists(self):
        self.config.write_text("clear-never-use\nclear-cbase-only\n",
                               encoding="utf-8")
        self.source.write_text("""void f(void) {
    strcpy(dst, src);
    memcpy(dst, src, n);
    a_api();
}
""", encoding="utf-8")
        self.check([])


if __name__ == "__main__":
    unittest.main()
