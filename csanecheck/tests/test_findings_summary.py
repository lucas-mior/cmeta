#!/usr/bin/env python3
"""Verify that csanecheck summarizes emitted rule findings."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"


class FindingsSummaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.first = self.root / "first.c"
        self.second = self.root / "second.c"
        self.first.write_text("//" + "x" * 79 + "\n"
                              + "//" + "y" * 79 + "\n", encoding="utf-8")
        self.second.write_text("//" + "z" * 79 + "\n", encoding="utf-8")

    def run_checker(self, *extra):
        return subprocess.run(
            [str(CSANE), "--file", "first.c", "--file", "second.c", *extra],
            cwd=self.root, text=True, capture_output=True, check=False,
        )

    def test_reports_total_after_findings(self):
        result = self.run_checker()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout.count("error[SRC-COLUMNS]"), 3,
                         result.stdout)
        self.assertEqual(result.stdout.splitlines()[-1], "Findings: 3")

    def test_omits_summary_when_no_findings(self):
        self.first.write_text("// fine\n", encoding="utf-8")
        self.second.write_text("// fine\n", encoding="utf-8")
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Findings:", result.stdout)

    def test_cached_findings_and_changed_file(self):
        initial = self.run_checker()
        cached = self.run_checker()
        self.assertEqual(initial.stdout, cached.stdout)
        self.assertEqual(cached.stdout.splitlines()[-1], "Findings: 3")
        self.second.write_text("// fine\n", encoding="utf-8")
        changed = self.run_checker()
        self.assertEqual(changed.returncode, 1, changed.stderr)
        self.assertEqual(changed.stdout.count("error[SRC-COLUMNS]"), 2,
                         changed.stdout)
        self.assertEqual(changed.stdout.splitlines()[-1], "Findings: 2")

    def test_statistics_and_summary_agree(self):
        result = self.run_checker("--stats")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("    violations=3\n", result.stdout)
        self.assertEqual(result.stdout.splitlines()[-1], "Findings: 3")


if __name__ == "__main__":
    unittest.main()
