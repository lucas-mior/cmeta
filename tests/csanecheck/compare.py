#!/usr/bin/env python3
"""Compare migrated csanecheck rules with the frozen legacy fixture baseline.

This is an offline migration tool. The legacy Perl/Python/shell programs are
invoked only here, never by the csanecheck executable.
"""

import argparse
import collections
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import baseline


HERE = Path(__file__).resolve().parent
SUPPORTED = {
    "API-NEVER", "API-CBASE-ONLY", "PP-DEFINED", "PP-SIZEOF",
    "SRC-TABS", "SRC-TRAILING", "SRC-COLUMNS",
    "FMT-ASSIGN-EQ", "AST-DECL-ONE",
    "AST-BRACES", "AST-SWITCH-ALIGN", "AST-ARTIFICIAL-BLOCK",
    "RYU-ENDIF-COMMENT",
    "RYU-FINAL-NEWLINE",
    "STYLE-ASSERT-REL", "STYLE-ASSERT-BOUNDS", "STYLE-STRLIT-LEN",
    "STYLE-STREQUAL", "STYLE-PRINTF", "STYLE-STATUS-TEMP",
}
CLEAN_CASES = {"preprocessor_defined", "sizeof_macro_definition",
               "non_ryu_newline"}
RULE = re.compile(r"^.+?:[0-9]+:[0-9]+: error\[([^\]]+)\]:")


def new_rules(executable, cwd, path):
    environment = baseline.environment(cwd)
    result = subprocess.run([str(executable), "--file", str(path)],
                            cwd=cwd, env=environment, capture_output=True,
                            text=True, check=False)
    if result.returncode not in (0, 1):
        raise ValueError(f"csanecheck failed: {result.stderr}")
    return sorted(rule.group(1) for line in result.stdout.splitlines()
                  if (rule := RULE.match(line)))


def compare_fixtures(executable, verbose):
    rules = baseline.load_rules()
    cases = json.loads((HERE / "cases.json").read_text(encoding="utf-8"))
    mismatches = 0
    checked = 0
    for case in cases:
        scoped = set(case["legacy"] + case["target"])
        if not scoped.intersection(SUPPORTED) and case["id"] not in CLEAN_CASES:
            continue
        checked += 1
        with tempfile.TemporaryDirectory(prefix="csanecheck-compare-") as tmp:
            cwd = Path(tmp)
            path = cwd / case["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(HERE / "cases" / case["fixture"], path)
            ok, detail = baseline.run_fixture(case, rules)
            actual = new_rules(executable, cwd, case["path"])
        target = sorted(code for code in case["target"] if code in SUPPORTED)
        unexpected = [code for code in actual if code not in SUPPORTED]
        matched = ok and actual == target and not unexpected
        if not matched:
            mismatches += 1
            print(f"DIFF {case['id']}: target={target}, csanecheck={actual}; "
                  f"legacy={detail}")
        elif verbose:
            print(f"MATCH {case['id']}: {actual}")
    print(f"Migration fixtures: {checked - mismatches}/{checked} matched "
          f"for {len(SUPPORTED)} implemented rules")
    return 1 if mismatches else 0


def normalize(path, root):
    candidate = Path(path)
    if candidate.is_absolute():
        try:
            return candidate.relative_to(root).as_posix()
        except ValueError:
            return str(candidate)
    return path.removeprefix("./")


def compare_project(executable, project, max_depth, filt, exclude):
    name, sep, path = project.partition("=")
    if not sep or not name or not path:
        raise ValueError("--project expects NAME=PATH")
    root = Path(path).resolve()
    with tempfile.TemporaryDirectory(prefix="csanecheck-project-compare-") as tmp:
        output = Path(tmp) / "legacy.json"
        arguments = argparse.Namespace(project=[project], maxdepth=max_depth,
                                       filter=filt, exclude=exclude,
                                       output=str(output), verify=None)
        status = baseline.project_command(arguments)
        if status:
            raise ValueError("legacy project baseline failed")
        old = json.loads(output.read_text(encoding="utf-8"))["projects"][name]
        env = baseline.environment(Path(tmp))
        if max_depth is not None:
            env["TEST_MAXDEPTH"] = str(max_depth)
        if filt is not None:
            env["TEST_FILTER"] = filt
        if exclude is not None:
            env["TEST_EXCLUDE_PATTERN"] = exclude
        result = subprocess.run([str(executable), "--root", str(root), "--stats"],
                                cwd=root, env=env, capture_output=True,
                                text=True, check=False)
    if result.returncode not in (0, 1):
        raise ValueError(result.stderr)
    selected = re.search(r"\bfiles=(\d+)\b", result.stdout)
    if selected is None:
        raise ValueError("csanecheck did not report file count")
    new = collections.Counter()
    for line in result.stdout.splitlines():
        matched = re.match(r"^(.*?):(\d+):\d+: error\[([^\]]+)\]:", line)
        if matched and matched.group(3) in SUPPORTED:
            new[(normalize(matched.group(1), root), int(matched.group(2)),
                 matched.group(3))] += 1
    old_supported = collections.Counter(
        (normalize(entry["path"], root), entry["line"], entry["rule"])
        for entry in old["diagnostics"] if entry["rule"] in SUPPORTED)
    missing = list((old_supported - new).elements())
    added = list((new - old_supported).elements())
    summary = {
        "project": name, "legacy_files": old["source_count"],
        "csanecheck_files": int(selected.group(1)),
        "legacy_supported_diagnostics": sum(old_supported.values()),
        "csanecheck_supported_diagnostics": sum(new.values()),
        "missing": sorted(missing), "added": sorted(added),
        "deferred_rules": sorted({entry["rule"] for entry in old["diagnostics"]
                                  if entry["rule"] not in SUPPORTED}),
    }
    print(json.dumps(summary, indent=2))
    return bool(missing or added or old["source_count"] != int(selected.group(1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--project", action="append", default=[],
                        help="NAME=PATH: compare real project diagnostics")
    parser.add_argument("--maxdepth", type=int)
    parser.add_argument("--filter")
    parser.add_argument("--exclude")
    parser.add_argument("--fail-on-difference", action="store_true")
    args = parser.parse_args()
    executable = args.executable.resolve()
    if not executable.is_file():
        parser.error("executable not found")
    try:
        status = compare_fixtures(executable, args.verbose)
        different = False
        for project in args.project:
            different |= compare_project(executable, project, args.maxdepth,
                                         args.filter, args.exclude)
        return status or (args.fail_on_difference and different)
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
