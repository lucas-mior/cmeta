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

    def cache_files(self):
        directory = self.root / ".cache" / "csanecheck"
        self.assertTrue(directory.is_dir(), directory)
        return sorted(directory.glob("*.bin"))

    def cache_file_for(self, name):
        source = (name + ":").encode("utf-8")
        matches = [path for path in self.cache_files()
                   if source in path.read_bytes()]
        self.assertEqual(len(matches), 1, matches)
        return matches[0]

    def mark_cached_diagnostics(self, path):
        original = path.read_bytes()
        marked = original.replace(b"error[SRC-COLUMNS]",
                                  b"error[TST-COLUMNS]")
        self.assertNotEqual(original, marked)
        self.assertEqual(len(original), len(marked))
        path.write_bytes(marked)
        return marked

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

    def test_creates_cache_record_for_each_source(self):
        self.assertFalse((self.root / ".cache").exists())
        initial = self.run_checker()
        self.assertEqual(initial.returncode, 1, initial.stderr)
        entries = self.cache_files()
        self.assertEqual(len(entries), 2, entries)
        for name in ("first.c", "second.c"):
            with self.subTest(source=name):
                self.assertGreater(self.cache_file_for(name).stat().st_size, 0)

    def test_cache_hit_replays_stored_diagnostics(self):
        initial = self.run_checker()
        self.assertEqual(initial.returncode, 1, initial.stderr)
        self.assertEqual(len(self.cache_files()), 2)
        first_cache = self.cache_file_for("first.c")
        marked = self.mark_cached_diagnostics(first_cache)

        cached = self.run_checker()
        self.assertEqual(cached.returncode, initial.returncode, cached.stderr)
        self.assertIn("error[TST-COLUMNS]", cached.stdout)
        self.assertEqual(cached.stdout.count("error[TST-COLUMNS]"), 2,
                         cached.stdout)
        self.assertEqual(cached.stdout.count("error[SRC-COLUMNS]"), 1,
                         cached.stdout)
        self.assertEqual(cached.stdout.splitlines()[-1], "Findings: 3")
        self.assertEqual(first_cache.read_bytes(), marked)

    def test_cached_findings_and_changed_file(self):
        initial = self.run_checker()
        self.assertEqual(initial.returncode, 1, initial.stderr)
        self.assertEqual(len(self.cache_files()), 2)
        first_cache = self.cache_file_for("first.c")
        second_cache = self.cache_file_for("second.c")
        first_record = self.mark_cached_diagnostics(first_cache)
        second_record = second_cache.read_bytes()

        cached = self.run_checker()
        self.assertEqual(cached.returncode, 1, cached.stderr)
        self.assertEqual(cached.stdout.count("error[TST-COLUMNS]"), 2,
                         cached.stdout)
        self.assertEqual(cached.stdout.count("error[SRC-COLUMNS]"), 1,
                         cached.stdout)
        self.assertEqual(cached.stdout.splitlines()[-1], "Findings: 3")

        self.second.write_text("// fine\n", encoding="utf-8")
        changed = self.run_checker()
        self.assertEqual(changed.returncode, 1, changed.stderr)
        self.assertEqual(changed.stdout.count("error[TST-COLUMNS]"), 2,
                         changed.stdout)
        self.assertNotIn("error[SRC-COLUMNS]", changed.stdout)
        self.assertEqual(changed.stdout.splitlines()[-1], "Findings: 2")
        self.assertEqual(len(self.cache_files()), 2)
        self.assertEqual(first_cache.read_bytes(), first_record)
        self.assertNotEqual(second_cache.read_bytes(), second_record)
        self.assertNotIn(b"error[SRC-COLUMNS]", second_cache.read_bytes())

        refreshed_record = second_cache.read_bytes()
        updated = self.run_checker()
        self.assertEqual(updated.returncode, changed.returncode, updated.stderr)
        self.assertEqual(updated.stdout, changed.stdout)
        self.assertEqual(second_cache.read_bytes(), refreshed_record)

    def test_statistics_and_summary_agree(self):
        result = self.run_checker("--stats")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("    violations=3\n", result.stdout)
        self.assertEqual(result.stdout.splitlines()[-1], "Findings: 3")


if __name__ == "__main__":
    unittest.main()
