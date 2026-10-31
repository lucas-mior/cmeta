#!/usr/bin/env python3
"""Standalone tests for source discovery and column-limit policies."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"


class SourcePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_cecup_project_relative_sources_use_100_columns(self):
        project = self.root / "cecup"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 98 + "\n"
                          + "//" + "x" * 99 + "\n", encoding="utf-8")
        file_list = project / "files.txt"
        file_list.write_text("./src/limit.c\n", encoding="utf-8")
        commands = [
            ["--root", "."],
            ["--file", "./src/limit.c"],
            ["--file-list", "files.txt"],
            ["./src/limit.c"],
        ]
        for arguments in commands:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(CSANE), *arguments], cwd=project,
                                        capture_output=True, text=True,
                                        check=False)
                self.assertEqual(result.returncode, 1, result.stderr)
                columns = [line for line in result.stdout.splitlines()
                           if "error[SRC-COLUMNS]" in line]
                self.assertEqual(len(columns), 1, result.stdout)
                self.assertIn("limit.c:2:1:", columns[0])

    def test_non_cecup_relative_sources_keep_80_columns(self):
        project = self.root / "other"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 79 + "\n", encoding="utf-8")
        result = subprocess.run([str(CSANE), "--root", "."], cwd=project,
                                capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("limit.c:1:1: error[SRC-COLUMNS]", result.stdout)

    def test_cecup_config_column_override_takes_precedence(self):
        project = self.root / "cecup"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 109 + "\n", encoding="utf-8")
        config = project / "custom.conf"
        config.write_text("columns ^src/limit\\.c$ 120\n",
                          encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", "./src/limit.c",
                                 "--config", str(config)], cwd=project,
                                capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_no_column_limit_is_ignored(self):
        path = self.root / "src/limit.c"
        path.parent.mkdir(parents=True)
        path.write_text("x" * 81 + "\n", encoding="utf-8")
        command = [str(CSANE), "--file", str(path)]
        normal = subprocess.run(command, cwd=self.root, capture_output=True,
                                text=True, check=False)
        empty = subprocess.run([str(CSANE), "--no-column-limit", "",
                                *command[1:]], cwd=self.root,
                               capture_output=True, text=True, check=False)
        self.assertEqual(normal.returncode, 1, normal.stderr)
        self.assertIn("error[SRC-COLUMNS]", normal.stdout)
        self.assertEqual(empty.returncode, normal.returncode, empty.stderr)
        self.assertEqual(empty.stdout, normal.stdout)
        self.assertEqual(empty.stderr, normal.stderr)

    def test_root_discovery_skips_generated_headers_and_stc(self):
        source = "#ifdef BAD\n#endif\n"
        for relative in ("src/visible.c", "stc/hidden.c",
                         "src/generated.meta.h", "tests/fixtures/ignored.c"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
        result = subprocess.run([str(CSANE), "--root", str(self.root)],
                                cwd=self.root, capture_output=True,
                                text=True, check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        findings = [line for line in result.stdout.splitlines()
                    if "error[PP-DEFINED]" in line]
        self.assertEqual(len(findings), 1, result.stdout)
        self.assertIn("src/visible.c", findings[0])

if __name__ == "__main__":
    unittest.main()
