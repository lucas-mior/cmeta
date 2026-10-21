#!/usr/bin/env python3
"""Compatibility tests for the legacy forbidden-function grep checks."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin" / "csanecheck"
NEVER_LIST = ROOT / "cbase" / "functions_never_use.txt"
CBASE_LIST = ROOT / "cbase" / "functions_allowed_cbase_only.txt"


def load_names(path):
    names = []
    for line in path.read_text(encoding="utf-8").splitlines():
        word = line.strip()
        if word and not word.startswith("#"):
            names.append(word.split()[0])
    return names


def legacy_pattern(names):
    escaped = [re.escape(name) for name in names]
    return re.compile(r"(^|[^A-Za-z0-9_])(" + "|".join(escaped)
                      + r")[ \t\r\f\v]*\(")


def legacy_grep_lines(path, names):
    pattern = legacy_pattern(names)
    findings = Counter()
    for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), 1):
        if pattern.search(line):
            findings[line_number] += 1
    return findings


def csane_findings(path, rule):
    result = subprocess.run([str(CSANE), "--file", str(path)], cwd=path.parent,
                            text=True, capture_output=True, check=False)
    pattern = re.compile(r":(\d+):\d+: error\[" + re.escape(rule) + r"\]")
    findings = Counter(int(match.group(1)) for match in
                       pattern.finditer(result.stdout))
    return findings, result


class ApiLegacyCompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.never = load_names(NEVER_LIST)
        self.cbase_only = load_names(CBASE_LIST)
        cbase = self.root / "cbase"
        cbase.mkdir(parents=True)
        (cbase / "functions_never_use.txt").write_text(
            NEVER_LIST.read_text(encoding="utf-8"), encoding="utf-8")
        (cbase / "functions_allowed_cbase_only.txt").write_text(
            CBASE_LIST.read_text(encoding="utf-8"), encoding="utf-8")

    def write(self, relative, source):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        return path

    def test_checked_in_name_lists_are_all_enforced(self):
        source = "void f(void) {\n"
        for name in self.never:
            source += f"    {name}(0);\n"
        for name in self.cbase_only:
            source += f"    {name}(0);\n"
        source += "}\n"
        path = self.write("src/all_names.c", source)
        never, result = csane_findings(path, "API-NEVER")
        restricted, _ = csane_findings(path, "API-CBASE-ONLY")
        self.assertEqual(sum(never.values()), len(self.never), result.stdout)
        self.assertEqual(sum(restricted.values()), len(self.cbase_only),
                         result.stdout)

    def test_meaningful_direct_calls_match_legacy_grep(self):
        path = self.write("src/direct.c", (
            "void f(void) {\n"
            "    strcpy(dst, src);\n"
            "    strcpy (dst, src);\n"
            "#if defined(FEATURE)\n"
            "    strcpy(dst, src);\n"
            "#else\n"
            "    strcpy(dst, other);\n"
            "#endif\n"
            "}\n"
            "#define COPY(dst, src) strcpy(dst, src)\n"
        ))
        legacy = legacy_grep_lines(path, self.never)
        newer, result = csane_findings(path, "API-NEVER")
        self.assertEqual(newer, legacy, result.stdout)

    def test_legacy_textual_false_positives_are_intentionally_ignored(self):
        path = self.write("src/textual.c", (
            "int32 strcpy(int32 dst, int32 src);\n"
            "void f(void) {\n"
            "    object.strcpy(dst, src);\n"
            "    object->strcpy(dst, src);\n"
            "    // strcpy(dst, src);\n"
            "    char *text = \"strcpy(dst, src)\";\n"
            "}\n"
        ))
        legacy = legacy_grep_lines(path, self.never)
        newer, result = csane_findings(path, "API-NEVER")
        self.assertEqual(legacy, Counter({1: 1, 3: 1, 4: 1, 5: 1, 6: 1}))
        self.assertFalse(newer, result.stdout)

    def test_token_aware_calls_detected_beyond_line_grep(self):
        path = self.write("src/token_calls.c", (
            "void f(void) {\n"
            "    (strcpy)(dst, src);\n"
            "    strcpy\n"
            "        (dst, src);\n"
            "}\n"
        ))
        legacy = legacy_grep_lines(path, self.never)
        newer, result = csane_findings(path, "API-NEVER")
        self.assertFalse(legacy)
        self.assertEqual(newer, Counter({2: 1, 3: 1}), result.stdout)

    def test_cbase_only_calls_match_outside_and_are_allowed_inside(self):
        external = self.write("src/restricted.c", (
            "void f(void) {\n"
            "    memcpy(dst, src, size);\n"
            "}\n"
        ))
        inside = self.write("cbase/restricted.c", (
            "void f(void) {\n"
            "    memcpy(dst, src, size);\n"
            "}\n"
        ))
        legacy_external = legacy_grep_lines(external, self.cbase_only)
        new_external, result = csane_findings(external, "API-CBASE-ONLY")
        self.assertEqual(new_external, legacy_external, result.stdout)
        inside_findings, result = csane_findings(inside, "API-CBASE-ONLY")
        self.assertFalse(inside_findings, result.stdout)

    def test_cbase_only_member_and_prototype_false_positives_are_ignored(self):
        path = self.write("src/restricted_textual.c", (
            "void *memcpy(void *dst, const void *src, int32 size);\n"
            "void f(void) {\n"
            "    object.memcpy(dst, src, size);\n"
            "    object->memcpy(dst, src, size);\n"
            "    // memcpy(dst, src, size);\n"
            "    char *text = \"memcpy(dst, src, size)\";\n"
            "}\n"
        ))
        legacy = legacy_grep_lines(path, self.cbase_only)
        newer, result = csane_findings(path, "API-CBASE-ONLY")
        self.assertEqual(legacy, Counter({1: 1, 3: 1, 4: 1, 5: 1, 6: 1}))
        self.assertFalse(newer, result.stdout)

    def test_cbase_only_symlink_uses_target_directory_membership(self):
        target = self.write("cbase/linked_impl.c", (
            "void f(void) {\n"
            "    memcpy(dst, src, size);\n"
            "}\n"
        ))
        link = self.root / "src" / "linked_impl.c"
        link.parent.mkdir(parents=True, exist_ok=True)
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(f"cannot create symlink: {error}")
        findings, result = csane_findings(link, "API-CBASE-ONLY")
        self.assertFalse(findings, result.stdout)

    def test_macro_parameter_is_not_a_call_but_replacement_call_is(self):
        path = self.write("src/macros.c", (
            "#define PASS(strcpy) strcpy\n"
            "#define COPY(dst, src) strcpy(dst, src)\n"
        ))
        legacy = legacy_grep_lines(path, self.never)
        newer, result = csane_findings(path, "API-NEVER")
        self.assertEqual(legacy, Counter({2: 1}))
        self.assertEqual(newer, Counter({2: 1}), result.stdout)


if __name__ == "__main__":
    unittest.main()
