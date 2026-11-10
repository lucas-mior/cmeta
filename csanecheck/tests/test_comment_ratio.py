#!/usr/bin/env python3
"""Comment-density limits for C implementations and headers."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
RULE = re.compile(r"error\[SRC-COMMENT-RATIO\]: (.*)")


class CommentRatioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, suffix, expected, args=None):
        path = self.root / ("sample" + suffix)
        path.write_bytes(source.encode("utf-8"))
        command = [str(CHECKER), "--no-project-config"]
        if args:
            command.extend(args)
        result = subprocess.run(
            command + ["--file", str(path)], cwd=ROOT,
            capture_output=True, text=True, check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        self.assertEqual(RULE.findall(result.stdout), expected,
                         result.stdout + result.stderr)

    def test_c_limit_is_inclusive(self):
        source = "// explanation\n" + "int x;\n" * 9
        self.check(source, ".c", [])
        self.check("// one\n// two\n" + "int x;\n" * 8, ".c", [
            "2 of 10 nonblank lines contain comments; exceeds 10% limit",
        ])

    def test_h_limit_is_inclusive(self):
        self.check("// one\n/* two */\nint a;\nint b;\nint c;\n",
                   ".h", [])
        self.check("// one\n/* two */\n// three\nint a;\nint b;\n",
                   ".h", [
            "3 of 5 nonblank lines contain comments; exceeds 40% limit",
        ])

    def test_multiple_comments_on_a_single_line_count_once(self):
        self.check("int x; /* one */ /* two */\n"
                   "int y; // inline\nint z;\n", ".c", [
            "2 of 3 nonblank lines contain comments; exceeds 10% limit",
        ])

    def test_multiline_block_comment_counts_each_nonblank_line(self):
        self.check("/* opening\n * body\n */\nint x;\nint y;\n",
                   ".h", [
            "3 of 5 nonblank lines contain comments; exceeds 40% limit",
        ])

    def test_literals_do_not_count_as_comments(self):
        self.check('char *a = "// not a comment";\n'
                   'char *b = "/* not a comment */";\n'
                   "char slash = '/';\n", ".c", [])

    def test_blank_lines_do_not_dilute_ratio(self):
        self.check("// one\n\n    \nint x;\n", ".c", [
            "1 of 2 nonblank lines contain comments; exceeds 10% limit",
        ])

    def test_crlf_and_missing_final_newline(self):
        self.check("/* first */\r\nint x;\r\n// last", ".h", [
            "2 of 3 nonblank lines contain comments; exceeds 40% limit",
        ])

    def test_empty_file(self):
        self.check("", ".c", [])
        self.check("\n \n\t\n", ".h", [])

    def test_config_exemption(self):
        config = self.root / "checker.conf"
        config.write_text("allow-rule SRC-COMMENT-RATIO .*\n",
                          encoding="utf-8")
        self.check("// only a comment\n", ".c", [],
                   ["--config", str(config)])


if __name__ == "__main__":
    unittest.main()
