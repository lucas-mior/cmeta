#!/usr/bin/env python3
"""Diagnostic locations use ANSI red/green/blue on terminal stdout."""

import errno
import os
from pathlib import Path
import pty
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"


class DiagnosticColorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_checker(self, *args, terminal=False):
        command = [str(CHECKER), "--no-project-config", *args]
        if not terminal:
            result = subprocess.run(command, cwd=self.root, text=True,
                                    capture_output=True, check=False)
            return result.returncode, result.stdout

        master, slave = pty.openpty()
        try:
            result = subprocess.run(command, cwd=self.root, stdout=slave,
                                    stderr=subprocess.PIPE, check=False)
        finally:
            os.close(slave)
        output = bytearray()
        try:
            while True:
                try:
                    chunk = os.read(master, 4096)
                except OSError as exc:
                    if exc.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                output.extend(chunk)
        finally:
            os.close(master)
        return result.returncode, output.decode("utf-8").replace("\r\n", "\n")

    def assert_colored(self, output, location, category):
        path, line, column = location
        prefix = (f"\x1b[0;31m{path}\x1b[0m:"
                  f"\x1b[0;32m{line}\x1b[0m:"
                  f"\x1b[0;34m{column}\x1b[0m: {category}")
        self.assertIn(prefix, output)

    def test_errors_plain_when_redirected_and_colored_when_terminal(self):
        (self.root / "source.c").write_text("int32 f(void) {  \n    return 1;\n}\n",
                                            encoding="utf-8")
        status, plain = self.run_checker("--file", "source.c")
        self.assertEqual(status, 1, plain)
        self.assertIn("source.c:1:16: error[SRC-TRAILING]", plain)
        self.assertNotIn("\x1b[", plain)
        status, colored = self.run_checker("--file", "source.c", terminal=True)
        self.assertEqual(status, 1, colored)
        self.assert_colored(colored, ("source.c", 1, 16),
                            "error[SRC-TRAILING]")

    def test_notes_and_coverage_errors_are_colored(self):
        (self.root / "notes.c").write_text("#endif\n", encoding="utf-8")
        status, notes = self.run_checker("--file", "notes.c",
                                         "--report-parse-issues", terminal=True)
        self.assertIn(status, (0, 1), notes)
        self.assert_colored(notes, ("notes.c", 1, 1), "note[PP-STRUCTURE]")

        (self.root / "coverage.c").write_text(
            "int32 f(void) {\n    int32 x = (1 +;\n    return x;\n}\n",
            encoding="utf-8")
        status, output = self.run_checker("--file", "coverage.c",
                                          "--require-coverage", terminal=True)
        self.assertEqual(status, 1, output)
        self.assert_colored(output, ("coverage.c", 1, 1),
                            "error[COVERAGE-INCOMPLETE]")

    def test_cache_keeps_terminal_and_plain_output_separate(self):
        (self.root / "source.h").write_text("//" + "x" * 79 + "\n",
                                            encoding="utf-8")
        args = ("--file", "source.h")
        for terminal in (False, True, True, False):
            with self.subTest(terminal=terminal):
                status, output = self.run_checker(*args, terminal=terminal)
                self.assertEqual(status, 1, output)
                if terminal:
                    self.assert_colored(output, ("source.h", 1, 1),
                                        "error[SRC-COLUMNS]")
                else:
                    self.assertIn("source.h:1:1: error[SRC-COLUMNS]", output)
                    self.assertNotIn("\x1b[", output)
        records = list((self.root / ".cache" / "csanecheck").glob("*.bin"))
        self.assertEqual(len(records), 2, records)


if __name__ == "__main__":
    unittest.main()
