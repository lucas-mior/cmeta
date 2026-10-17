#!/usr/bin/env python3
"""Integration tests for csanecheck's build.sh install mode."""

import os
import pathlib
import shlex
import shutil
import stat
import subprocess
import tempfile
import unittest


PROJECT = pathlib.Path(__file__).resolve().parents[2]
BUILD = PROJECT / "build.sh"


class CsanecheckInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.stage = pathlib.Path(self.temp.name)

    def install(self, prefix, staged=True):
        environment = os.environ.copy()
        environment["PREFIX"] = prefix
        if staged:
            environment["DESTDIR"] = str(self.stage)
        else:
            environment.pop("DESTDIR", None)
        compiler = pathlib.Path(shlex.split(environment.get("CC", "cc"))[0]).name
        if compiler.startswith("clang") and shutil.which("ld.lld"):
            if "-fuse-ld=" not in environment.get("LDFLAGS", ""):
                environment["LDFLAGS"] = (
                    environment.get("LDFLAGS", "") + " -fuse-ld=lld"
                )
        return subprocess.run(
            [str(BUILD), "install"],
            cwd=PROJECT,
            env=environment,
            capture_output=True,
            text=True,
            timeout=120,
            check=False,
        )

    def check_executable(self, path):
        self.assertTrue(path.is_file(), path)
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o755)
        self.assertEqual(path.read_bytes(), (PROJECT / "bin/csanecheck").read_bytes())
        result = subprocess.run(
            [str(path), "--version"],
            cwd=self.stage,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("csanecheck", result.stdout)
        source = self.stage / "installed-check.c"
        source.write_text("int installed_check(void) { return 1; }\n",
                          encoding="utf-8")
        result = subprocess.run(
            [str(path), "--file", str(source)],
            cwd=self.stage,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_staged_standard_prefix(self):
        result = self.install("/usr/local")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_executable(self.stage / "usr/local/bin/csanecheck")

    def test_staged_custom_prefix_with_spaces(self):
        result = self.install("/opt/custom tools")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_executable(self.stage / "opt/custom tools/bin/csanecheck")

    def test_direct_prefix_without_destdir(self):
        result = self.install(str(self.stage / "local tools"), staged=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_executable(self.stage / "local tools/bin/csanecheck")

    def test_relative_prefix_is_rejected(self):
        result = self.install("relative/path")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PREFIX must be an absolute path", result.stderr)
        self.assertEqual(list(self.stage.rglob("csanecheck")), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
