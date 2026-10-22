#!/usr/bin/env python3
"""Compare the legacy Perl pattern checker with csanecheck.

The legacy diagnostic text is classified using the Step 1 inventory. A
comparison preserves multiplicity and original source lines, but deliberately
ignores differences in wording and columns. Unrecognized Perl diagnostics are
reported, never silently dropped. Additional csanecheck-only rules are kept
separate from the Perl parity result.
"""

import argparse
from collections import Counter
from dataclasses import dataclass
import csv
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


PROJECT = Path(__file__).resolve().parents[2]
INVENTORY = PROJECT / "csanecheck/perl_compatibility.tsv"
PERL = PROJECT / "cbase/functions_forbidden_patterns.pl"
CSANE = PROJECT / "bin/csanecheck"

# Patterns must match the complete Perl description. Their order is irrelevant.
# They are intentionally independent of the names of csanecheck functions.
LEGACY_MESSAGES = {
    "P01": r"line exceeds \d+ columns",
    "P02": r"tab character is forbidden",
    "P03": r"trailing whitespace",
    "P04": r"use a block comment after #endif",
    "P05": r"missing final newline",
    "P06": r"use #if defined or #if !defined",
    "P07": r"use SIZEOF instead of sizeof",
    "P08": r"(?:if|else|for|while|switch|do) body must use curly braces",
    "P09": r"(?:case|default) must align with switch",
    "P10": r"only one variable declaration per line",
    "P11": r"artificial block at nesting level 3 or more is forbidden",
    "P12": r"\w+ call breaks before the first argument",
    "P13": r"\w+ function header parameter is not aligned with first parameter",
    "P14": r"\w+ call string literal is not aligned with first literal",
    "P15": r"\w+ call argument is not aligned with its alignment anchor",
    "P16": r"\w+ format-string line must contain all format arguments or none",
    "P17": r"\w+ related (?:_len|x/y|width/height) arguments must be the only arguments on their lines",
    "P18": r"\w+ call fits on one line but is split across multiple lines",
    "P19": r"assignment = must end the preceding line instead of beginning a line",
    "P20": r"assignment fits on one line but is split after =",
    "P21": r"use ASSERT_BETWEEN\(.*\) instead of consecutive ASSERT_GE/ASSERT_LT",
    "P22": r"use ASSERT_(?:GE|GT|LE|LT) for comparison with integers",
    "P23": r"STRLIT_LEN called on a literal string",
    "P24": r"STREQUAL called with STRLIT literal",
    "P25": r"single-argument format string without literal content",
}
LEGACY_LINE = re.compile(r"^(.*):(\d+):(.*)$")
CSANE_LINE = re.compile(
    r"^(.*):(\d+):(\d+): error\[([A-Za-z0-9-]+)\]: (.*)$"
)
# This final user guidance is not a diagnostic rule.
CSANE_GUIDANCE = frozenset({
    "In order to understand and learn how to fix the problems above, "
    "read cbase/c-guidelines.md and cbase/c-format.md",
    "if you think any of them are a false positive, stop what you are "
    "doing and report.",
})
CSANE_ONLY = frozenset({
    "API-NEVER", "API-CBASE-ONLY", "STYLE-STATUS-TEMP", "FMT-MULTIPLY",
    "FMT-BINARY-SPACING", "FMT-ASSIGN-SPACING", "FMT-COMMA-SPACING",
    "FMT-MEMBER-SPACING", "FMT-UNARY-SPACING", "FMT-CAST-SPACING",
    "FMT-POINTER-SPACING", "FMT-CALL-SPACING", "FMT-TRAILING-COMMA",
    "STYLE-INCDEC", "COVERAGE-INCOMPLETE",
    "TYPE-CONST-SIGNATURE", "TYPE-STDINT-T",
    "TYPE-ENUM-TYPEDEF", "TYPE-UNION-TYPEDEF",
    "TYPE-POINTER-TYPEDEF",
})


@dataclass(frozen=True)
class Rule:
    key: str
    category: str
    csane_id: str
    status: str


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    category: str
    description: str
    column: int | None = None

    @property
    def location(self):
        return self.path, self.line, self.category


@dataclass
class Comparison:
    legacy: list[Finding]
    csane: list[Finding]
    legacy_unknown: list[str]
    csane_extra: list[Finding]
    csane_unknown: list[str]

    @property
    def missing(self):
        return Counter(item.location for item in self.legacy) - Counter(
            item.location for item in self.csane
        )

    @property
    def added(self):
        return Counter(item.location for item in self.csane) - Counter(
            item.location for item in self.legacy
        )

    @property
    def matches(self):
        return not (self.missing or self.added or self.legacy_unknown
                    or self.csane_unknown)

    def summary(self):
        lines = []
        for heading, items in (("legacy only", self.missing),
                               ("csanecheck only", self.added)):
            for (path, line, category), count in sorted(items.items()):
                lines.append(f"{heading}: {path}:{line} {category} x{count}")
        lines.extend(f"unclassified legacy: {item}" for item in
                     self.legacy_unknown)
        lines.extend(f"unknown csanecheck rule: {item}" for item in
                     self.csane_unknown)
        for item in self.csane_extra:
            lines.append(f"non-Perl csanecheck rule: {item.path}:{item.line} "
                         f"{item.category}")
        return "\n".join(lines) if lines else "Perl parity matched"


def load_rules(path=INVENTORY):
    with Path(path).open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    rules = {}
    for row in rows:
        key = row["legacy_key"]
        if key in rules:
            raise ValueError(f"duplicate legacy key: {key}")
        rules[key] = Rule(key, row["category"], row["csane_rule_id"],
                          row["status"])
    if set(rules) != set(LEGACY_MESSAGES):
        raise ValueError("legacy patterns do not match inventory keys")
    ids = [item.csane_id for item in rules.values()]
    if len(ids) != len(set(ids)):
        raise ValueError("multiple legacy categories share a csanecheck ID")
    return rules


def source_path(name, cwd):
    """Canonicalize paths emitted as absolute or relative to checker cwd."""
    path = Path(name)
    absolute = (path if path.is_absolute() else cwd / path).resolve()
    try:
        return absolute.relative_to(cwd.resolve()).as_posix()
    except ValueError:
        return absolute.as_posix()


def normalize_perl(output, cwd, rules):
    records, unknown = [], []
    compiled = {key: re.compile(pattern) for key, pattern in
                LEGACY_MESSAGES.items()}
    for text in output.splitlines():
        match = LEGACY_LINE.fullmatch(text)
        if match is None:
            unknown.append(text)
            continue
        path, lineno, description = match.groups()
        matching = [key for key, pattern in compiled.items()
                    if pattern.fullmatch(description)]
        if len(matching) != 1:
            unknown.append(text)
            continue
        key = matching[0]
        records.append(Finding(source_path(path, cwd), int(lineno),
                               rules[key].category, description))
    return records, unknown


def normalize_csane(output, cwd, rules):
    records, extras, unknown = [], [], []
    by_id = {rule.csane_id: rule.category for rule in rules.values()}
    by_id["FMT-CALL-FIRST-TOPLEVEL"] = rules["P12"].category
    for text in output.splitlines():
        if text in CSANE_GUIDANCE:
            continue
        match = CSANE_LINE.fullmatch(text)
        if match is None:
            unknown.append(text)
            continue
        path, lineno, column, code, description = match.groups()
        category = by_id.get(code, code)
        record = Finding(source_path(path, cwd), int(lineno), category,
                         description, int(column))
        if code in by_id:
            records.append(record)
        elif code in CSANE_ONLY:
            extras.append(record)
        else:
            unknown.append(text)
    return records, extras, unknown


def run_checkers(files, cwd, *, file_list=False, perl_cache=None,
                 no_column_limit=None, rules=None):
    """Execute both tools on identical files. Paths may contain spaces."""
    cwd = Path(cwd).resolve()
    rules = load_rules() if rules is None else rules
    env = dict(os.environ)
    env.pop("CBASE_FORBIDDEN_PROFILE", None)
    env.pop("TEST_FILTER", None)
    env.pop("TEST_EXCLUDE_PATTERN", None)
    env.pop("TEST_MAXDEPTH", None)
    filenames = [str(path) for path in files]
    with tempfile.TemporaryDirectory(prefix="csane-parity-") as scratch:
        scratch = Path(scratch)
        cache = (Path(perl_cache) if perl_cache is not None
                 else scratch / "perl-cache.json")
        env["CBASE_FORBIDDEN_PATTERNS_CACHE"] = str(cache)
        if file_list:
            listing = scratch / "sources.list"
            listing.write_text("".join(f"{path}\n" for path in filenames),
                               encoding="utf-8")
            perl_args = ["--file-list", str(listing)]
            csane_args = ["--file-list", str(listing)]
        else:
            perl_args = filenames
            csane_args = [item for path in filenames
                          for item in ("--file", path)]
        if no_column_limit is not None:
            perl_args = ["--no-column-limit", str(no_column_limit),
                         *perl_args]
            csane_args = ["--no-column-limit",
                          re.escape(str(no_column_limit)) + r"$",
                          *csane_args]
        perl = subprocess.run(["perl", str(PERL), *perl_args], cwd=cwd,
                              env=env, text=True, capture_output=True,
                              check=False)
        csane = subprocess.run([str(CSANE), *csane_args], cwd=cwd,
                               env=env, text=True, capture_output=True,
                               check=False)
    if perl.returncode != 0 or csane.returncode not in (0, 1):
        raise RuntimeError(f"checker execution failed: perl {perl.returncode}: "
                           f"{perl.stderr}; csane {csane.returncode}: "
                           f"{csane.stderr}")
    legacy, unclassified = normalize_perl(perl.stdout, cwd, rules)
    newer, extras, other = normalize_csane(csane.stdout, cwd, rules)
    return Comparison(legacy, newer, unclassified, extras, other)



def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--cwd", type=Path, default=Path.cwd())
    parser.add_argument("--file-list", action="store_true")
    parser.add_argument("--no-column-limit", type=Path,
                        help="exact source path exempted from column limit")
    parser.add_argument("--strict", action="store_true",
                        help="fail on every unverified parity difference")
    args = parser.parse_args(argv)
    comparison = run_checkers(args.files, args.cwd,
                              file_list=args.file_list,
                              no_column_limit=args.no_column_limit)
    print(comparison.summary())
    return int(args.strict and not comparison.matches)


if __name__ == "__main__":
    sys.exit(main())
