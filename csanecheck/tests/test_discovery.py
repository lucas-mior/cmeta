#!/usr/bin/env python3
"""Regression tests for csanecheck project source discovery."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin" / "csanecheck"


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_source(self, relative):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("int32 value;\n", encoding="utf-8")
        return path

    def discovered(self, *extra):
        command = [str(CHECKER), "--root", str(self.root), "--coverage"]
        command.extend(extra)
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        prefix = "coverage file="
        paths = set()
        for line in result.stdout.splitlines():
            if not line.startswith(prefix):
                continue
            path = line[len(prefix):].split(" declarations=", 1)[0]
            paths.add(Path(path).resolve())
        return paths

    def test_default_roots_include_csanecheck(self):
        visible = [
            self.write_source("root.c"),
            self.write_source("src/source.c"),
            self.write_source("cbase/base.c"),
            self.write_source("csanecheck/checker.c"),
            self.write_source("tools/tool.c"),
        ]
        found = self.discovered()
        for path in visible:
            with self.subTest(path=path):
                self.assertIn(path.resolve(), found)

    def test_default_discovery_skips_artifact_and_vendor_directories(self):
        visible = self.write_source("src/visible.c")
        hidden = [
            self.write_source("src/build/hidden.c"),
            self.write_source("src/generated/hidden.c"),
            self.write_source("src/stc/hidden.c"),
            self.write_source("src/third_party/hidden.c"),
            self.write_source("src/vendor/hidden.c"),
        ]
        found = self.discovered()
        self.assertIn(visible.resolve(), found)
        for path in hidden:
            with self.subTest(path=path):
                self.assertNotIn(path.resolve(), found)

    def test_hidden_sources_are_not_discovered(self):
        visible = self.write_source("src/visible.c")
        hidden = [
            self.write_source(".root.c"),
            self.write_source("src/.welcome.h"),
            self.write_source("src/.internal/hidden.c"),
            self.write_source("cbase/ryu/.hidden.c"),
            self.write_source("cbase/ryu/.internal/hidden.c"),
        ]
        found = self.discovered()
        self.assertIn(visible.resolve(), found)
        for path in hidden:
            with self.subTest(path=path):
                self.assertNotIn(path.resolve(), found)

    def test_hidden_sources_are_skipped_even_when_explicit(self):
        visible = self.write_source("src/visible.c")
        hidden = [
            self.write_source("src/.welcome.h"),
            self.write_source("src/.internal/hidden.c"),
        ]
        file_list = self.root / "files.txt"
        file_list.write_text("\n".join(str(path) for path in hidden)
                             + "\n", encoding="utf-8")
        commands = [
            [str(CHECKER), "--file", str(hidden[0])],
            [str(CHECKER), str(hidden[1])],
            [str(CHECKER), "--file-list", str(file_list)],
            [str(CHECKER), "--root", str(self.root)],
        ]
        for command in commands:
            with self.subTest(command=command):
                result = subprocess.run(command + ["--coverage"], cwd=ROOT,
                                        capture_output=True, text=True,
                                        check=False)
                self.assertIn(result.returncode, (0, 1), result.stderr)
                self.assertNotIn("coverage file=" + str(hidden[0]),
                                 result.stdout)
                self.assertNotIn("coverage file=" + str(hidden[1]),
                                 result.stdout)
        self.assertIn(visible.resolve(), self.discovered())

    def test_source_root_adds_project_tree(self):
        custom = self.write_source("engine/custom.c")
        self.assertNotIn(custom.resolve(), self.discovered())
        self.assertIn(custom.resolve(), self.discovered("--source-root",
                                                       "engine"))

    def test_clear_source_roots_allows_custom_root_set(self):
        default = self.write_source("src/default.c")
        custom = self.write_source("engine/custom.c")
        found = self.discovered("--clear-source-roots",
                                "--source-root", "engine")
        self.assertNotIn(default.resolve(), found)
        self.assertIn(custom.resolve(), found)

    def test_skip_dir_extends_default_exclusions(self):
        visible = self.write_source("src/visible.c")
        skipped = self.write_source("src/cache/hidden.c")
        found = self.discovered("--skip-dir", "cache")
        self.assertIn(visible.resolve(), found)
        self.assertNotIn(skipped.resolve(), found)

    def test_source_root_and_skip_dir_are_order_independent(self):
        visible = self.write_source("engine/visible.c")
        skipped = self.write_source("engine/cache/hidden.c")
        first = self.discovered("--skip-dir", "cache",
                                "--source-root", "engine")
        second = self.discovered("--source-root", "engine",
                                 "--skip-dir", "cache")
        self.assertEqual(first, second)
        self.assertIn(visible.resolve(), first)
        self.assertNotIn(skipped.resolve(), first)


if __name__ == "__main__":
    unittest.main()
