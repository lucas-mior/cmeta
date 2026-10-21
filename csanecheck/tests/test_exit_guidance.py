#!/usr/bin/env python3
"""The checker prints actionable guidance only for actual violations."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from differential import load_rules, normalize_csane


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
GUIDANCE = (
    "In order to understand and learn how to fix the problems above, "
    "read cbase/c-guidelines.md and cbase/c-format.md\n"
    "if you think any of them are a false positive, stop what you are "
    "doing and report.\n"
)


class ExitGuidanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def check(self, sources, *args):
        command = [str(CHECKER)]
        for name, source in sources.items():
            path = self.root / name
            path.write_text(source, encoding="utf-8")
            command.extend(("--file", str(path)))
        command.extend(args)
        return subprocess.run(command, cwd=ROOT, capture_output=True,
                              text=True, check=False)

    def test_violation_prints_footer_once_at_end(self):
        result = self.check({"bad1.c": "int32 a = sizeof(int32);\n",
                             "bad2.c": "int32 b = sizeof(int32);\n",
                             "good.c": "int32 c = 1;\n"}, "--stats")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertEqual(result.stdout.count("error[PP-SIZEOF]"), 2)
        self.assertEqual(result.stdout.count(GUIDANCE), 1)
        self.assertTrue(result.stdout.endswith(GUIDANCE), result.stdout)
        self.assertIn("violations=2", result.stdout)

    def test_clean_input_has_no_footer(self):
        result = self.check({"good.c": "int32 c = 1;\n"})
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn(GUIDANCE, result.stdout)

    def test_strict_parse_failure_without_violation_has_no_footer(self):
        result = self.check({"incomplete.c": "#if 0\nint32 c = 1;\n"},
                            "--strict-parse", "--report-parse-issues")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("note[PP-STRUCTURE]", result.stdout)
        self.assertNotIn(GUIDANCE, result.stdout)

    def test_guidance_is_not_a_parity_diagnostic(self):
        rules = load_rules()
        parsed, extras, unknown = normalize_csane(
            "bad.c:1:1: error[PP-SIZEOF]: use SIZEOF instead of sizeof\n"
            + GUIDANCE, ROOT, rules)
        self.assertEqual(len(parsed), 1)
        self.assertEqual(extras, [])
        self.assertEqual(unknown, [])


if __name__ == "__main__":
    unittest.main()
