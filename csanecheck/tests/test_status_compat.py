#!/usr/bin/env python3
"""Compatibility tests for the legacy temporary-status checker."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin" / "csanecheck"
LEGACY = ROOT / "cbase" / "status_temporary_patterns.py"


def legacy_findings(path):
    result = subprocess.run(
        ["python3", str(LEGACY), "--no-cache", str(path)],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    pattern = re.compile(r":(\d+):inline the int32 status")
    return Counter(int(match.group(1)) for match in
                   pattern.finditer(result.stdout)), result


def csane_findings(path):
    result = subprocess.run(
        [str(CSANE), "--file", str(path)],
        cwd=path.parent,
        text=True,
        capture_output=True,
        check=False,
    )
    pattern = re.compile(
        r":(\d+):\d+: error\[STYLE-STATUS-TEMP\]"
    )
    return Counter(int(match.group(1)) for match in
                   pattern.finditer(result.stdout)), result


class StatusLegacyCompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        cbase = self.root / "cbase"
        cbase.mkdir(parents=True)
        for name in ("functions_never_use.txt",
                     "functions_allowed_cbase_only.txt"):
            (cbase / name).write_text(
                (ROOT / "cbase" / name).read_text(encoding="utf-8"),
                encoding="utf-8",
            )

    def write(self, source):
        path = self.root / "sample.c"
        path.write_text(source, encoding="utf-8")
        return path

    def assert_matches_legacy(self, source):
        path = self.write(source)
        legacy, legacy_result = legacy_findings(path)
        newer, newer_result = csane_findings(path)
        self.assertEqual(
            newer,
            legacy,
            f"legacy:\n{legacy_result.stdout}\ncsanecheck:\n{newer_result.stdout}",
        )

    def test_basic_and_multiple_pairs_match(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "\n"
            "    status = first();\n"
            "    ASSERT_LE(status, 0);\n"
            "    status = second();\n"
            "    ASSERT_EQ(status, 0);\n"
            "    return;\n"
            "}\n"
        )

    def test_blank_line_between_assignment_and_use_matches(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "\n"
            "    ASSERT_LE(status, 0);\n"
            "}\n"
        )

    def test_comment_handling_matches(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status; /* status */\n"
            "    char *text = \"status\";\n"
            "    status = operation(); /* comment */\n"
            "    ASSERT_LE(status, 0);\n"
            "    (void)text;\n"
            "}\n"
        )
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    // comment-only line\n"
            "    ASSERT_LE(status, 0);\n"
            "}\n"
        )

    def test_nested_block_matches(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    {\n"
            "        status = operation();\n"
            "        ASSERT_LE(status, 0);\n"
            "    }\n"
            "}\n"
        )

    def test_macro_use_and_directive_references_match(self):
        self.assert_matches_legacy(
            "#define CHECK(value) ASSERT_LE(value, 0)\n"
            "#define STATUS_WORD status\n"
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    CHECK(status);\n"
            "}\n"
        )

    def test_conditional_branches_match(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "#if defined(FEATURE)\n"
            "    status = first();\n"
            "    ASSERT_EQ(status, 0);\n"
            "#else\n"
            "    status = second();\n"
            "    ASSERT_EQ(status, 0);\n"
            "#endif\n"
            "}\n"
        )

    def test_early_return_use_matches(self):
        self.assert_matches_legacy(
            "static int32\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    return status;\n"
            "}\n"
        )

    def test_repeated_and_unpaired_uses_match(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    consume(status + status);\n"
            "}\n"
        )
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    consume(status);\n"
            "    ASSERT_EQ(status, 0);\n"
            "}\n"
        )

    def test_statement_before_assignment_on_same_line_matches(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    prepare(); status = operation();\n"
            "    ASSERT_EQ(status, 0);\n"
            "}\n"
        )

    def test_extra_statement_after_assignment_is_intentionally_safer(self):
        path = self.write(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation(); side_effect();\n"
            "    ASSERT_EQ(status, 0);\n"
            "}\n"
        )
        legacy, legacy_result = legacy_findings(path)
        newer, newer_result = csane_findings(path)
        self.assertEqual(legacy, Counter({3: 1}), legacy_result.stdout)
        self.assertFalse(newer, newer_result.stdout)

    def test_initialized_and_shadowed_status_match(self):
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status = operation();\n"
            "    ASSERT_LE(status, 0);\n"
            "}\n"
        )
        self.assert_matches_legacy(
            "static void\n"
            "example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    {\n"
            "        int32 status;\n"
            "    }\n"
            "}\n"
        )


if __name__ == "__main__":
    unittest.main()
