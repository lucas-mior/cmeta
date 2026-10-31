#!/usr/bin/env python3
"""Full-tree acceptance tests for legacy P01-P25 compatibility."""

import os
from pathlib import Path
import re
import subprocess
import unittest

from differential import CSANE, PROJECT, load_rules, run_checkers


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


class FullTreeCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load_rules()
        cls.legacy_files = legacy_selected_files(PROJECT)

    def test_legacy_source_selection_matches_csanecheck(self):
        actual = csane_legacy_selected_files(PROJECT)
        self.assertEqual(actual, self.legacy_files)

    def test_full_tree_p01_p25_parity_except_call_wrap(self):
        comparison = run_checkers(self.legacy_files, PROJECT, file_list=True,
                                  rules=self.rules)
        # csanecheck deliberately reports additional call-wrap violations
        # when adjacent calls have different names and first arguments.
        # Everything else must still match the legacy Perl checker.
        self.assertFalse(comparison.missing, comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())
        unexpected = [item for item in comparison.added
                      if item[2] != self.rules["P18"].category]
        self.assertFalse(unexpected, comparison.summary())

    def test_every_legacy_category_has_focused_regression_reference(self):
        missing = [rule.key for rule in self.rules.values()
                   if rule.status != "implemented"]
        self.assertEqual(missing, [])

        inventory = PROJECT / "csanecheck/perl_compatibility.tsv"
        with inventory.open(encoding="utf-8") as stream:
            header = stream.readline().rstrip("\n").split("\t")
            legacy_test = header.index("legacy_test")
            uncovered = []
            for line in stream:
                fields = line.rstrip("\n").split("\t")
                if not fields or not fields[0]:
                    continue
                if fields[legacy_test] in ("", "-"):
                    uncovered.append(fields[0])
        self.assertEqual(uncovered, [])


if __name__ == "__main__":
    unittest.main()
