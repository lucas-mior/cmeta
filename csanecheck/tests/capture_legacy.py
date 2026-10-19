#!/usr/bin/env python3
"""Refresh or verify the source/diagnostic snapshots of the legacy test corpus.

Run only on demand: both original unittest suites execute their old tools and
can be considerably slower than the csanecheck regression suite.
"""

import argparse
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = Path(__file__).with_name("legacy_cases.json")
SOURCES = (
    ("perl", "test_functions_forbidden_patterns.py", "ForbiddenPatternsTests",
     "run_perl"),
    ("status", "test_status_temporary_patterns.py",
     "StatusTemporaryPatternsTests", "run_checker"),
)


def load_module(path, name):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def capture_suite(prefix, script, class_name, method_name):
    module = load_module(ROOT / "cbase" / script, "legacy_" + prefix)
    klass = getattr(module, class_name)
    original = getattr(klass, method_name)
    cases = []

    def record(test, *args, **kwargs):
        output = original(test, *args, **kwargs)
        if prefix == "perl":
            name, source = args
        else:
            name, source = "src/sample.c", args[0]
        diagnostics = []
        for line in output.splitlines():
            match = re.search(r":(\d+):(.*)$", line)
            if not match:
                raise AssertionError(f"unrecognized diagnostic: {line}")
            diagnostics.append({
                "line": int(match.group(1)), "message": match.group(2),
            })
        cases.append({
            "id": f"{prefix}/{test._testMethodName}/{len(cases)}",
            "path": name,
            "source": source,
            "file_list": bool(kwargs.get("file_list", False)),
            "legacy": diagnostics,
        })
        return output

    setattr(klass, method_name, record)
    try:
        runner = unittest.TextTestRunner(stream=io.StringIO(), verbosity=0)
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(klass)
        result = runner.run(suite)
    finally:
        setattr(klass, method_name, original)
    if not result.wasSuccessful():
        raise RuntimeError(f"legacy tests failed: {script} "
                           f"({len(result.failures)} failures, "
                           f"{len(result.errors)} errors)")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true",
                        help="fail if captured fixtures differ from snapshot")
    action.add_argument("--write", action="store_true",
                        help="replace the checked-in snapshot")
    parser.add_argument("--suite", choices=("all", "perl", "status"),
                        default="all", help="check just one reference suite")
    args = parser.parse_args()
    cases = []
    for entry in SOURCES:
        if args.suite != "all" and entry[0] != args.suite:
            continue
        part = capture_suite(*entry)
        print(f"{entry[0]}: captured {len(part)} fixtures", file=sys.stderr)
        cases.extend(part)
    if not cases:
        raise RuntimeError("no fixtures captured")
    content = json.dumps(cases, indent=2, ensure_ascii=False) + "\n"
    if args.check:
        existing = json.loads(OUTPUT.read_text(encoding="utf-8"))
        if args.suite != "all":
            existing = [c for c in existing
                        if c["id"].startswith(args.suite + "/")]
        if json.dumps(existing, indent=2, ensure_ascii=False) + "\n" != content:
            print(f"outdated snapshot: {OUTPUT}", file=sys.stderr)
            return 1
        print("Legacy snapshot matches original regression suites.")
    else:
        if args.suite != "all":
            raise SystemExit("--write requires --suite all")
        OUTPUT.write_text(content, encoding="utf-8")
        print(f"Saved {len(cases)} cases in {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
