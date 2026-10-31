#!/usr/bin/env python3
"""Check that csanecheck discovers the source files selected by the wrapper."""

import os
from pathlib import Path
import re
import subprocess
import unittest

PROJECT = Path(__file__).resolve().parents[2]
CSANE = PROJECT / "bin/csanecheck"

LEGACY_ROOTS = (
    "cbase", "src", "test", "tests", "examples", "cyblocks", "tools",
)
COVERAGE_FILE = re.compile(r"^coverage file=(\S+) ")


def legacy_selected_files(root):
    """Model functions_forbidden.sh default source discovery."""
    root = Path(root).resolve()
    selected = set()

    def add_tree(directory, direct_only=False):
        path = root / directory
        if not path.is_dir():
            return
        if direct_only:
            candidates = path.iterdir()
        else:
            candidates = path.rglob("*")
        for candidate in candidates:
            if not candidate.is_file():
                continue
            relative = candidate.relative_to(root).as_posix()
            if candidate.suffix.lower() not in (".c", ".h"):
                continue
            if relative.endswith(".meta.h"):
                continue
            parts = Path(relative).parts
            if "stc" in parts[:-1]:
                continue
            selected.add(relative)

    add_tree(".", direct_only=True)
    for directory in LEGACY_ROOTS:
        add_tree(directory)

    # The wrapper explicitly scans this tree even when general discovery is
    # depth-limited. It is already included above in the default case, but
    # retaining the operation makes the model mirror the shell policy.
    add_tree("cbase/ryu")
    return sorted(selected)


def csane_legacy_selected_files(root):
    """Ask csanecheck to discover using exactly the legacy source roots."""
    args = [str(CSANE), "--root", ".", "--coverage",
            "--clear-source-roots"]
    for directory in LEGACY_ROOTS:
        args.extend(("--source-root", directory))

    env = dict(os.environ)
    env.pop("TEST_FILTER", None)
    env.pop("TEST_EXCLUDE_PATTERN", None)
    env.pop("TEST_MAXDEPTH", None)
    result = subprocess.run(args, cwd=root, env=env, text=True,
                            capture_output=True, check=False)
    if result.returncode not in (0, 1):
        raise RuntimeError(
            f"csanecheck discovery failed ({result.returncode}): "
            f"{result.stderr}"
        )
    selected = []
    for line in result.stdout.splitlines():
        match = COVERAGE_FILE.match(line)
        if match is not None:
            selected.append(Path(match.group(1)).as_posix().removeprefix("./"))
    return sorted(set(selected))


class FullTreeDiscoveryTests(unittest.TestCase):
    def test_wrapper_source_selection_matches_csanecheck(self):
        expected = legacy_selected_files(PROJECT)
        actual = csane_legacy_selected_files(PROJECT)
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
