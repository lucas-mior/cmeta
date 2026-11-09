#!/usr/bin/env python3
"""Compiler selection uses CC_* flags except in platform detection."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
RULE = re.compile(r":(\d+):(\d+): error\[PP-COMPILER-MACRO\]: (.*)")


class CompilerMacroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "sample.h"

    def check(self, source, expected, args=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config"]
        if args:
            command.extend(args)
        result = subprocess.run(
            command + ["--file", str(self.path)], cwd=ROOT,
            capture_output=True, text=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        matches = [(int(line), int(column), message)
                   for line, column, message in RULE.findall(result.stdout)]
        self.assertEqual(matches, expected, result.stdout + result.stderr)

    def test_if_elif_and_ifdef_forms(self):
        self.check("""#if defined(__clang__)
#elif defined __GNUC__
#elif !defined(__TINYC__) || defined(_MSC_VER)
#endif
#ifdef __GNUC__
#endif
#ifndef __clang__
#endif
""", [
            (1, 13, "use CC_CLANG instead of checking compiler macro definedness"),
            (2, 15, "use CC_GCC instead of checking compiler macro definedness"),
            (3, 16, "use CC_TCC instead of checking compiler macro definedness"),
            (3, 38, "use CC_MSVC instead of checking compiler macro definedness"),
            (5, 8, "use CC_GCC instead of checking compiler macro definedness"),
            (7, 9, "use CC_CLANG instead of checking compiler macro definedness"),
        ])

    def test_comments_literals_versions_and_continuations(self):
        self.check("""// #if defined(__clang__)
char *text = "#if defined(__GNUC__)";
/* #elif defined(__TINYC__) */
#if CC_CLANG || CC_GCC || CC_TCC || CC_MSVC
#endif
#if __GNUC__ >= 7
#endif
#if defined(UNRELATED) /* defined(__clang__) */
#endif
#if defined( /* comment */ \\
    __clang__)
#endif
""", [
            (11, 5, "use CC_CLANG instead of checking compiler macro definedness"),
        ])

    def test_only_platform_detection_is_exempt(self):
        self.check("#if defined(__clang__)\n#endif\n", [
            (1, 13, "use CC_CLANG instead of checking compiler macro definedness"),
        ], ["--config", str(ROOT / "cbase/csanecheck.conf")])
        platform = self.root / "cbase/platform_detection.h"
        platform.parent.mkdir(parents=True)
        platform.write_text("#if defined(__clang__)\n#endif\n", encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config",
             str(ROOT / "cbase/csanecheck.conf"), "--file", str(platform)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertNotIn("error[PP-COMPILER-MACRO]", result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
