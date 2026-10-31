#!/usr/bin/env python3
"""Full-tree acceptance gate for unresolved csanecheck coverage."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import unittest

PROJECT = Path(__file__).resolve().parents[2]
CSANE = PROJECT / "bin/csanecheck"


ALLOWLIST = PROJECT / "csanecheck/tests/full_tree_coverage_allowlist.tsv"
STRUCTURAL = re.compile(
    r'^coverage_unresolved file=(\S+) line=(\d+) column=(\d+) '
    r'target=(\S+) reason="([^"]+)"$'
)
RULE = re.compile(
    r'^rule_coverage_unresolved file=(\S+) group=(\S+) line=(\d+) '
    r'column=(\d+) reason=(\S+)$'
)
SPACE = re.compile(r"\s+")
REASON_FIXTURES = {
    "ambiguous-operator-context": (
        "test_layout.py", "test_unknown_bare_type_remains_ambiguous"
    ),
    "conditional construct could not be merged": (
        "test_structure.py", "conditional construct could not be merged"
    ),
    "control-structure-unrecognized": (
        "test_coverage.py", "control-structure-unrecognized"
    ),
}


def normalize_source(text):
    return SPACE.sub(" ", text.strip())


def load_allowlist():
    allowed = Counter()
    with ALLOWLIST.open(encoding="utf-8") as stream:
        for line in stream:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 5:
                raise AssertionError(f"invalid coverage allowlist row: {line!r}")
            path, target, reason, source, count = fields
            key = (path, target, reason, normalize_source(source))
            if key in allowed:
                raise AssertionError(f"duplicate coverage allowlist row: {key}")
            allowed[key] = int(count)
    return allowed


def source_signature(path, line, target, reason):
    relative = path.removeprefix("./")
    source_path = PROJECT / relative
    lines = source_path.read_text(encoding="utf-8").splitlines()
    if line < 1 or line > len(lines):
        raise AssertionError(f"invalid unresolved location: {path}:{line}")
    source = normalize_source(lines[line - 1])
    return relative, target, reason, source


def unresolved_records(output):
    records = Counter()
    for line in output.splitlines():
        match = STRUCTURAL.match(line)
        if match is not None:
            path, line_no, _column, target, reason = match.groups()
            key = source_signature(path, int(line_no),
                                   f"STRUCTURE:{target}", reason)
            records[key] += 1
            continue
        match = RULE.match(line)
        if match is not None:
            path, group, line_no, _column, reason = match.groups()
            key = source_signature(path, int(line_no), f"RULE:{group}",
                                   reason)
            records[key] += 1
    return records


def format_records(records):
    lines = []
    for (path, target, reason, source), count in sorted(records.items()):
        lines.append(
            f"{path}\t{target}\t{reason}\t{source}\t{count}"
        )
    return "\n".join(lines)


class FullTreeCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.allowed = load_allowlist()
        result = subprocess.run(
            [str(CSANE), "--root", ".", "--require-coverage"],
            cwd=PROJECT, text=True, capture_output=True, check=False,
        )
        if result.returncode not in (0, 1):
            raise RuntimeError(
                f"csanecheck coverage run failed ({result.returncode}): "
                f"{result.stderr}"
            )
        cls.result = result
        cls.actual = unresolved_records(result.stdout)

    def test_no_new_unresolved_coverage(self):
        unexpected = Counter()
        for key, count in self.actual.items():
            excess = count - self.allowed[key]
            if excess > 0:
                unexpected[key] = excess
        self.assertFalse(
            unexpected,
            "new or increased unresolved coverage:\n"
            + format_records(unexpected),
        )

    def test_allowlisted_reason_classes_have_regression_fixtures(self):
        reasons = {key[2] for key in self.allowed}
        self.assertEqual(reasons, set(REASON_FIXTURES))
        for _reason, (filename, anchor) in REASON_FIXTURES.items():
            text = (PROJECT / "csanecheck/tests" / filename).read_text(
                encoding="utf-8"
            )
            self.assertIn(anchor, text)

    def test_structural_unresolved_is_narrowly_allowlisted(self):
        structural = Counter({key: count for key, count in self.actual.items()
                              if key[1].startswith("STRUCTURE:")})
        unexpected = Counter()
        for key, count in structural.items():
            excess = count - self.allowed[key]
            if excess > 0:
                unexpected[key] = excess
        self.assertFalse(
            unexpected,
            "unexpected structural unresolved coverage:\n"
            + format_records(unexpected),
        )


if __name__ == "__main__":
    unittest.main()
