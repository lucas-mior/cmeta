#!/usr/bin/env python3
"""Parity tests for csanecheck against captured Perl/Python checker fixtures.

These tests deliberately do not invoke the legacy checkers. Their diagnostic
snapshots come from the old test suites, and can be regenerated with
capture_legacy.py. Known discrepancies are individually tracked rather than
silently accepting or skipping their cases.
"""

import collections
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BINARY = Path(os.environ.get("CSANECHECK_BINARY", ROOT / "bin/csanecheck"))
DIAGNOSTIC = re.compile(r":\d+:\d+: error\[([^]]+)\]:")

# Map *diagnostic messages* to ids, not just yes/no: we must not accidentally
# pass a fixture because an unrelated rule fires at the same location.
LEGACY_MESSAGES = (
    ("call breaks before the first argument", "FMT-CALL-FIRST"),
    ("call fits on one line but is split across multiple lines",
     "FMT-CALL-WRAP"),
    ("call argument is not aligned", "FMT-ARG-ALIGN"),
    ("call string literal is not aligned", "FMT-LITERAL-ALIGN"),
    ("assignment = must end", "FMT-ASSIGN-EQ"),
    ("assignment fits on one line", "FMT-ASSIGN-WRAP"),
    ("only one variable declaration", "AST-DECL-ONE"),
    ("body must use curly braces", "AST-BRACES"),
    ("must align with switch", "AST-SWITCH-ALIGN"),
    ("artificial block at nesting", "AST-ARTIFICIAL-BLOCK"),
    ("use ASSERT_BETWEEN", "STYLE-ASSERT-BOUNDS"),
    ("inline the int32 status", "STYLE-STATUS-TEMP"),
    ("use SIZEOF", "PP-SIZEOF"),
    ("use #if defined", "PP-DEFINED"),
    ("line exceeds", "SRC-COLUMNS"),
    ("tab character", "SRC-TABS"),
    ("trailing whitespace", "SRC-TRAILING"),
    ("missing final newline", "RYU-FINAL-NEWLINE"),
    ("block comment after #endif", "RYU-ENDIF-COMMENT"),
)


def legacy_rules(case):
    rules = []
    for diagnostic in case["legacy"]:
        message = diagnostic["message"]
        matches = [rule for fragment, rule in LEGACY_MESSAGES
                   if fragment in message]
        if len(matches) != 1:
            raise AssertionError(f"unknown/ambiguous legacy message: {message}")
        rules.append(matches[0])
    return collections.Counter(rules)


def run_checker(args, cwd, overrides=None):
    env = os.environ.copy()
    # Never inherit caller-specific source-selection controls into the suite.
    for key in ("TEST_FILTER", "TEST_MAXDEPTH", "TEST_EXCLUDE_PATTERN"):
        env.pop(key, None)
    if overrides:
        env.update(overrides)
    return subprocess.run(
        [str(BINARY), *map(str, args)], cwd=cwd, env=env,
        capture_output=True, text=True, check=False,
    )


def rule_counts(process):
    return collections.Counter(DIAGNOSTIC.findall(process.stdout))


def write_file(root, name, source):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source, encoding="utf-8")
    return path


class CsanecheckCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file() or not os.access(BINARY, os.X_OK):
            raise AssertionError(
                f"missing csanecheck binary: {BINARY}; run ./build.sh build"
            )
        cls.cases = json.loads((HERE / "legacy_cases.json").read_text())
        cls.differences = json.loads(
            (HERE / "known_differences.json").read_text()
        )

    def test_legacy_fixture_coverage_and_baseline(self):
        cases = self.cases
        case_ids = [case["id"] for case in cases]
        self.assertEqual(len(case_ids), 95)
        self.assertEqual(sum(i.startswith("perl/") for i in case_ids), 81)
        self.assertEqual(sum(i.startswith("status/") for i in case_ids), 14)
        self.assertEqual(len(case_ids), len(set(case_ids)))
        self.assertEqual(set(self.differences) - set(case_ids), set())
        self.assertTrue(any(c["id"].startswith("perl/") for c in cases))
        self.assertTrue(any(c["id"].startswith("status/") for c in cases))
        self.assertTrue(any(c["file_list"] for c in cases))
        seen_known = set()
        for case in cases:
            with self.subTest(case=case["id"]):
                expected = legacy_rules(case)
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    source = write_file(root, case["path"], case["source"])
                    args = ["--file", source]
                    if case["file_list"]:
                        listed = write_file(root, "sources.list", f"{source}\n")
                        args = ["--file-list", listed]
                    result = run_checker(args, root)
                self.assertIn(result.returncode, (0, 1), result.stderr)
                actual = rule_counts(result)
                self.assertEqual(result.returncode, int(bool(actual)),
                                 result.stdout + result.stderr)
                missing = dict(expected - actual)
                extra = dict(actual - expected)
                difference = self.differences.get(case["id"])
                if difference is None:
                    self.assertEqual((missing, extra), ({}, {}),
                                     result.stdout)
                else:
                    seen_known.add(case["id"])
                    self.assertTrue(difference["reason"])
                    self.assertEqual(missing, difference["missing"],
                                     difference["reason"])
                    self.assertEqual(extra, difference["extra"],
                                     difference["reason"])
        self.assertEqual(seen_known, set(self.differences))

    def test_forbidden_api_and_cbase_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/sample.c", (
                "void example(void) {\n"
                "    strcpy(0, 0);\n"
                "    memcpy(0, 0, 0);\n"
                "}\n"
            ))
            result = run_checker(["--file", source], root)
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("API-NEVER", rule_counts(result))
            self.assertIn("API-CBASE-ONLY", rule_counts(result))

    def test_project_name_lists_override_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "cbase/functions_never_use.txt", "sample_api\n")
            write_file(root, "cbase/functions_allowed_cbase_only.txt",
                       "local_api\n")
            source = write_file(root, "src/sample.c", (
                "void example(void) {\n"
                "    sample_api();\n"
                "    local_api();\n"
                "}\n"
            ))
            result = run_checker(["--root", root], root)
            actual = rule_counts(result)
            self.assertEqual(actual["API-NEVER"], 1, result.stdout)
            self.assertEqual(actual["API-CBASE-ONLY"], 1, result.stdout)
            inside = write_file(root, "cbase/inside.c", (
                "void example(void) {\n"
                "    local_api();\n"
                "}\n"
            ))
            result = run_checker(["--file", inside], root)
            self.assertNotIn("API-CBASE-ONLY", rule_counts(result))

    def test_file_list_with_spaces_and_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/a name.c",
                                "#ifdef FEATURE\n#endif\n")
            other = write_file(root, "src/other.c", "#ifndef FEATURE\n#endif\n")
            listing = write_file(root, "list.txt", (
                f"{source}\n{source}\n{other}\n"
            ))
            result = run_checker(["--file-list", listing], root)
            self.assertEqual(rule_counts(result)["PP-DEFINED"], 2,
                             result.stdout)

    def test_selection_by_filter_exclusion_and_depth(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "src/first.c", "#ifdef FIRST\n#endif\n")
            write_file(root, "src/second.c", "#ifdef SECOND\n#endif\n")
            write_file(root, "examples/deep/third.c", "#ifdef THIRD\n#endif\n")
            all_files = run_checker(["--root", root], root)
            self.assertEqual(rule_counts(all_files)["PP-DEFINED"], 3,
                             all_files.stdout)
            selected = run_checker(["--root", root, "--filter", "first"], root)
            self.assertEqual(rule_counts(selected)["PP-DEFINED"], 1,
                             selected.stdout)
            excluded = run_checker(["--root", root, "--exclude", "second"],
                                   root)
            self.assertEqual(rule_counts(excluded)["PP-DEFINED"], 2,
                             excluded.stdout)
            shallow = run_checker(["--root", root, "--max-depth", "1"], root)
            self.assertLess(rule_counts(shallow)["PP-DEFINED"], 3,
                            shallow.stdout)

    def test_parse_issues_do_not_impersonate_rule_violations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/sample.c", "int32 foo(\n")
            result = run_checker(["--file", source, "--report-parse-issues"],
                                 root)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("note[PARSE-", result.stdout)
            self.assertEqual(rule_counts(result), collections.Counter())
            strict = run_checker(["--file", source, "--strict-parse"], root)
            self.assertEqual(strict.returncode, 1, strict.stdout)

    def test_usage_and_io_errors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for args in (["--unknown"], ["--max-depth", "not-a-number"],
                         ["--file-list", root / "nonexistent.list"]):
                with self.subTest(args=args):
                    result = run_checker(args, root)
                    self.assertEqual(result.returncode, 2,
                                     result.stdout + result.stderr)

    def test_environment_selection_and_symlink_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "cbase/functions_never_use.txt", "")
            write_file(root, "cbase/functions_allowed_cbase_only.txt",
                       "local_api\n")
            inside = write_file(root, "cbase/internal.c", (
                "void example(void) {\n"
                "    local_api();\n"
                "}\n"
            ))
            outside = write_file(root, "src/external.c", (
                "void example(void) {\n"
                "    local_api();\n"
                "}\n"
            ))
            (root / "src/internal_link.c").symlink_to(inside)
            (root / "cbase/external_link.c").symlink_to(outside)
            selected = run_checker(["--root", root], root,
                                   {"TEST_FILTER": "external"})
            self.assertIn("API-CBASE-ONLY", rule_counts(selected))
            excluded = run_checker(["--root", root], root,
                                   {"TEST_EXCLUDE_PATTERN": "external"})
            self.assertEqual(rule_counts(excluded).get("API-CBASE-ONLY", 0),
                             0, excluded.stdout)

    def test_generated_header_is_skipped_in_discovery(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "src/generated.meta.h", "#ifdef X\n#endif\n")
            write_file(root, "src/regular.h", "#ifdef X\n#endif\n")
            result = run_checker(["--root", root], root)
            self.assertEqual(rule_counts(result)["PP-DEFINED"], 1,
                             result.stdout)

    def test_status_use_in_if(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/sample.c", (
                "static int32\n"
                "example(void) {\n"
                "    int32 status;\n"
                "    status = operation();\n"
                "    if (status != 0) {\n"
                "        return 1;\n"
                "    }\n"
                "    return 0;\n"
                "}\n"
            ))
            result = run_checker(["--file", source], root)
            self.assertIn("STYLE-STATUS-TEMP", rule_counts(result))

    def test_status_in_nested_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/sample.c", (
                "static int32\n"
                "example(void) {\n"
                "    {\n"
                "        int32 status;\n"
                "        status = operation();\n"
                "        return status;\n"
                "    }\n"
                "}\n"
            ))
            result = run_checker(["--file", source], root)
            self.assertIn("STYLE-STATUS-TEMP", rule_counts(result))


if __name__ == "__main__":
    unittest.main()
