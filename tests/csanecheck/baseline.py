#!/usr/bin/env python3
"""Freeze legacy checker behavior; this is test tooling, not csanecheck."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]
CBASE = REPOSITORY / "cbase"
DIAGNOSTIC = re.compile(r"^(.+?):([0-9]+):(.+)$")


def load_rules():
    rules = json.loads((HERE / "rules.json").read_text(encoding="utf-8"))
    identifiers = [rule["id"] for rule in rules]
    if len(set(identifiers)) != len(identifiers):
        raise ValueError("duplicate rule IDs")
    return rules


def classify(message, rules):
    matches = [rule["id"] for rule in rules
               if rule.get("diagnostic")
               and re.search(rule["diagnostic"], message)]
    if len(matches) != 1:
        raise ValueError(f"expected one rule for {message!r}, got {matches}")
    return matches[0]


def parse_shell_diagnostics(output, rules):
    """The grep-based scans print matched source lines, not rule messages."""
    section = None
    diagnostics = []
    sections = {
        "Error: functions that must never be used:": "API-NEVER",
        "Error: functions only allowed inside cbase:": "API-CBASE-ONLY",
        "Error: forbidden code patterns and formatting idioms:": "perl",
        "Error: status temporaries whose assignment/use pairs should be inlined:":
            "status",
    }
    for line in output.splitlines():
        if line in sections:
            section = sections[line]
            continue
        match = DIAGNOSTIC.fullmatch(line)
        if match is None:
            continue
        if section is None:
            raise ValueError(f"diagnostic without category: {line}")
        path, number, message = match.groups()
        rule = (section if section.startswith("API-")
                else classify(message, rules))
        diagnostics.append({"path": path, "line": int(number),
                            "rule": rule, "message": message})
    return diagnostics


def parse_diagnostics(output, rules):
    diagnostics = []
    for line in output.splitlines():
        match = DIAGNOSTIC.fullmatch(line)
        if match is None:
            raise ValueError(f"unrecognized diagnostic: {line!r}")
        path, line_number, message = match.groups()
        diagnostics.append({"path": path, "line": int(line_number),
                            "rule": classify(message, rules),
                            "message": message})
    return diagnostics


def environment(cache_dir):
    values = dict(os.environ)
    for name in ("TEST_FILTER", "TEST_MAXDEPTH", "TEST_EXCLUDE_PATTERN"):
        values.pop(name, None)
    values["CBASE_FORBIDDEN_PROFILE"] = "0"
    values["CBASE_FORBIDDEN_PATTERNS_CACHE"] = str(cache_dir / "patterns.json")
    values["CBASE_STATUS_TEMPORARY_PATTERNS_CACHE"] = str(cache_dir / "status.json")
    return values


def run(command, root, env):
    return subprocess.run(command, cwd=root, env=env, check=False,
                          capture_output=True, text=True)


def run_fixture(case, rules):
    fixture = HERE / "cases" / case["fixture"]
    if fixture.suffix != ".txt" or not fixture.is_file():
        raise ValueError(f"missing .txt fixture: {fixture}")
    with tempfile.TemporaryDirectory(prefix="csanecheck-fixture-") as tmp:
        root = Path(tmp)
        path = root / case["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fixture, path)
        env = environment(root)
        engine = case["engine"]
        if engine == "perl":
            result = run(["perl", str(CBASE / "functions_forbidden_patterns.pl"),
                          case["path"]], root, env)
            diagnostics = result.stdout
        elif engine == "status":
            result = run([sys.executable,
                          str(CBASE / "status_temporary_patterns.py"),
                          "--no-cache", case["path"]], root, env)
            diagnostics = result.stdout
        elif engine == "shell":
            cb = root / "cbase"
            cb.mkdir(exist_ok=True)
            for filename in ("functions_never_use.txt",
                             "functions_allowed_cbase_only.txt",
                             "functions_forbidden_patterns.pl",
                             "status_temporary_patterns.py"):
                destination = cb / filename
                if not destination.exists():
                    destination.symlink_to(CBASE / filename)
            # Source the real entry point, using the same formatting callback
            # that the existing shell regression suite supplies.
            result = run(["sh", "-c",
                          'error() { printf "$@" >&2; }; . "$1"',
                          "check", str(CBASE / "functions_forbidden.sh")],
                         root, env)
            diagnostics = result.stderr
        else:
            raise ValueError(f"invalid engine {engine!r}")
        if engine != "shell" and result.returncode != 0:
            raise ValueError(f"{engine} failed: {result.stderr}")
        if engine == "shell" and result.returncode not in (0, 1):
            raise ValueError(f"shell checker failed: {result.stderr}")
        detected = (parse_shell_diagnostics(diagnostics, rules)
                    if engine == "shell"
                    else parse_diagnostics(diagnostics, rules))
        actual = sorted(item["rule"] for item in detected)
        expected = sorted(case["legacy"])
        if actual != expected:
            return False, (f"{case['id']}: expected {expected}, found {actual}; "
                           f"diagnostics: {detected!r}")
        return True, f"{case['id']}: {actual}"


def fixture_command(args):
    rules = load_rules()
    valid = {rule["id"] for rule in rules if rule["status"] == "active"}
    cases = json.loads((HERE / "cases.json").read_text(encoding="utf-8"))
    ids = [case["id"] for case in cases]
    if len(set(ids)) != len(ids):
        raise ValueError("duplicate fixture case IDs")
    covered = set()
    failed = 0
    for case in cases:
        for field in ("legacy", "target"):
            for rule in case[field]:
                if rule not in valid:
                    raise ValueError(f"{case['id']}: unknown {field} rule {rule}")
        covered.update(case["legacy"])
        try:
            ok, detail = run_fixture(case, rules)
        except (ValueError, OSError) as error:
            ok, detail = False, f"{case['id']}: {error}"
        if not ok:
            print(f"FAIL {detail}", file=sys.stderr)
            failed += 1
        elif args.verbose:
            print(f"PASS {detail}")
    uncovered = sorted(valid - covered)
    if uncovered:
        print("No triggering fixture for: " + ", ".join(uncovered),
              file=sys.stderr)
    print(f"{len(cases) - failed}/{len(cases)} cases passed; "
          f"{len(covered & valid)}/{len(valid)} active rules triggered")
    return 1 if failed or uncovered else 0


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project_command(args):
    rules = load_rules()
    projects = {}
    with tempfile.TemporaryDirectory(prefix="csanecheck-baseline-") as tmp:
        for entry in args.project:
            name, separator, location = entry.partition("=")
            if not separator or not name or not location:
                raise ValueError("--project expects NAME=PATH")
            if re.fullmatch(r"[A-Za-z0-9_-]+", name) is None:
                raise ValueError(f"invalid project name {name!r}")
            if name in projects:
                raise ValueError(f"duplicate project {name}")
            root = Path(location).resolve()
            if not root.is_dir():
                raise ValueError(f"not a project directory: {location}")
            cache = Path(tmp) / name
            cache.mkdir(parents=True)
            env = environment(cache)
            env["CBASE_FORBIDDEN_PROFILE"] = "1"
            if args.maxdepth is not None:
                env["TEST_MAXDEPTH"] = str(args.maxdepth)
            if args.filter is not None:
                env["TEST_FILTER"] = args.filter
            if args.exclude is not None:
                env["TEST_EXCLUDE_PATTERN"] = args.exclude
            result = run(["sh", "-c",
                          'error() { printf "$@" >&2; }; . "$1"',
                          "check", str(CBASE / "functions_forbidden.sh")],
                         root, env)
            # The shell entry point prefixes sections and postscript text.
            diagnostics = parse_shell_diagnostics(result.stderr, rules)
            selected = re.search(r"\[profile shell\] selected sources: (\d+)",
                                 result.stderr)
            if selected is None or result.returncode not in (0, 1):
                raise ValueError(f"project {name} failed:\n{result.stderr}")
            projects[name] = {
                "source_count": int(selected.group(1)),
                "exit_status": result.returncode,
                "diagnostics": diagnostics,
            }
    fingerprint = {name: hash_file(CBASE / name) for name in (
        "functions_forbidden.sh", "functions_forbidden_patterns.pl",
        "functions_never_use.txt", "functions_allowed_cbase_only.txt",
        "status_temporary_patterns.py")}
    data = {"schema": 1, "checker_sha256": fingerprint,
            "filters": {"maxdepth": args.maxdepth, "filter": args.filter,
                        "exclude": args.exclude}, "projects": projects}
    serialized = json.dumps(data, indent=2, sort_keys=True) + "\n"
    if args.verify:
        saved = json.loads(Path(args.verify).read_text(encoding="utf-8"))
        if saved != data:
            print("Baseline differs from " + args.verify, file=sys.stderr)
            if args.output:
                Path(args.output).write_text(serialized, encoding="utf-8")
            return 1
        print(f"Baseline matches {args.verify}")
    elif args.output:
        Path(args.output).write_text(serialized, encoding="utf-8")
    else:
        sys.stdout.write(serialized)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    fixtures = commands.add_parser("fixtures", help="verify fixed legacy cases")
    fixtures.add_argument("--verbose", action="store_true")
    projects = commands.add_parser("projects", help="capture project baseline")
    projects.add_argument("--project", action="append", required=True,
                           help="NAME=PATH; repeat for each checkout")
    projects.add_argument("--maxdepth", type=int)
    projects.add_argument("--filter")
    projects.add_argument("--exclude")
    projects.add_argument("--output", help="write deterministic JSON baseline")
    projects.add_argument("--verify", help="compare with existing baseline JSON")
    args = parser.parse_args()
    try:
        return (fixture_command(args) if args.command == "fixtures"
                else project_command(args))
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
