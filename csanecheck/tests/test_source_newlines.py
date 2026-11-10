#!/usr/bin/env python3
"""Check source-level diagnostics for excessive consecutive newlines."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[SRC-EXCESS-NEWLINES\]")


class SourceNewlinesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "newlines.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config=None):
        self.path.write_bytes(source)
        command = [str(CHECKER), "--no-project-config", "--file",
                   str(self.path)]
        if config is not None:
            command += ["--config", str(config)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=10)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        return result

    def test_zero_one_and_two_newlines_allowed(self):
        self.check(b"int32 x;", [])
        self.check(b"int32 x;\nint32 y;\n", [])
        self.check(b"int32 x;\n\nint32 y;\n", [])

    def test_three_newlines_at_start_middle_and_end(self):
        self.check(b"\n\n\nint32 x;\n", [(3, 1)])
        self.check(b"int32 x;\n\n\nint32 y;\n", [(3, 1)])
        self.check(b"int32 x;\n\n\n", [(3, 1)])

    def test_longer_runs_report_each_extra_newline(self):
        self.check(b"int32 x;\n\n\n\n\nint32 y;\n",
                   [(3, 1), (4, 1), (5, 1)])

    def test_crlf_newlines_and_mixed_endings(self):
        self.check(b"int32 x;\r\n\r\n\r\nint32 y;\r\n", [(3, 2)])
        self.check(b"int32 x;\n\r\n\nint32 y;\n", [(3, 1)])

    def test_escaped_newlines_and_spaces_are_not_consecutive(self):
        self.check(b'char *s = "\\n\\n\\n";\nint32 x;\n', [])
        self.check(b"int32 x;\n \n\nint32 y;\n", [])

    def test_policy_exemption(self):
        config = Path(self.temp.name) / "csanecheck.conf"
        config.write_text("allow-rule SRC-EXCESS-NEWLINES .*\n",
                          encoding="utf-8")
        result = self.check(b"int32 x;\n\n\n", [], config=config)
        self.assertNotIn("error[SRC-EXCESS-NEWLINES]", result.stdout)


if __name__ == "__main__":
    unittest.main()
