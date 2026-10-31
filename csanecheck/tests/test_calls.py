#!/usr/bin/env python3
"""Standalone tests for call and function-header rules."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"


class CallLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_function_headers_wrap_only_when_needed(self):
        path = self.root / "src" / "header_wrap.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text((
            "int32\n"
            "cy_fit_add_initial_state_guess(CyFitProblem *fit, char *name,\n"
            "                               double guess) { return 0; }\n"
            "void\n"
            "short_prototype(int32 first,\n"
            "                int32 second);\n"
            "int32\n"
            "single_line(int32 first, int32 second) { return 0; }\n"
            "void\n"
            "very_long_function_name_that_cannot_fit_on_one_line(\n"
            "    int32 first_argument_with_a_long_name,\n"
            "    int32 second_argument_with_a_long_name\n"
            ");\n"
            "void\n"
            "commented_header(int32 first, /* keep this split */\n"
            "                 int32 second);\n"
        ), encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", str(path)],
                                cwd=self.root, text=True,
                                capture_output=True, check=False)
        findings = re.findall(r":(\d+):\d+: error\[FMT-FUNC-WRAP\]",
                              result.stdout)
        self.assertEqual(findings, ["2", "5"],
                         result.stdout + result.stderr)

    def test_format_arguments_unnecessarily_wrap(self):
        path = self.root / "src" / "format_args_wrap.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text((
            "void f(void) {\n"
            "            error2(\"missing option '%.*s.%.*s'\\n\",\n"
            "                   spec->group_len, spec->group,\n"
            "                   spec->name_len, spec->name);\n"
            "}\n"
        ), encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", str(path)],
                                cwd=self.root, text=True,
                                capture_output=True, check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        findings = re.findall(
            r":(\d+):\d+: error\[(FMT-FORMAT-ARGS-WRAP)\]",
            result.stdout,
        )
        self.assertEqual(findings, [("3", "FMT-FORMAT-ARGS-WRAP")],
                         result.stdout)

    def test_format_arguments_wrap_only_when_they_fit(self):
        path = self.root / "src" / "format_args_long.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text((
            "void f(void) {\n"
            "    log(\"values: %d %d %d\",\n"
            "        first_argument_with_a_very_long_descriptive_name,\n"
            "        second_argument_with_a_very_long_descriptive_name,\n"
            "        third_argument_with_a_very_long_descriptive_name);\n"
            "}\n"
        ), encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", str(path)],
                                cwd=self.root, text=True,
                                capture_output=True, check=False)
        self.assertNotIn("FMT-FORMAT-ARGS-WRAP", result.stdout)

    def test_forbidden_api_only_direct_calls(self):
        path = self.root / "src/api_calls.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text((
            "void f(void) {\n"
            "    strcpy(dst, src);\n"
            "    object.strcpy(dst, src);\n"
            "    object->strcpy(dst, src);\n"
            "    int32 strcpy(int32 dst, int32 src);\n"
            "    memcpy(dst, src, n);\n"
            "    object.memcpy(dst, src, n);\n"
            "}\n"
            "#define COPY(dst, src, n) memcpy(dst, src, n)\n"
        ), encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", str(path)],
                                cwd=self.root, text=True, capture_output=True,
                                check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        findings = Counter((int(line), rule) for line, rule in
                           re.findall(r":(\d+):\d+: error\[(API-[A-Z-]+)\]",
                                      result.stdout))
        self.assertEqual(findings, Counter({
            (2, "API-NEVER"): 1,
            (6, "API-CBASE-ONLY"): 1,
            (9, "API-CBASE-ONLY"): 1,
        }), result.stdout)

    def test_forbidden_api_direct_call_edge_cases(self):
        path = self.root / "src/api_edge_cases.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text((
            "typedef int32 local_type;\n"
            "void f(void) {\n"
            "    (strcpy)(dst, src);\n"
            "    local_type strcpy(local_type value);\n"
            "    void (*strcpy)(void);\n"
            "    object->strcpy(dst, src);\n"
            "    // strcpy(dst, src);\n"
            "    char *text = \"strcpy(dst, src)\";\n"
            "}\n"
            "#define PASS(strcpy) strcpy\n"
            "#define COPY(dst, src) strcpy(dst, src)\n"
        ), encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", str(path)],
                                cwd=self.root, text=True, capture_output=True,
                                check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        findings = Counter((int(line), rule) for line, rule in
                           re.findall(r":(\d+):\d+: error\[(API-[A-Z-]+)\]",
                                      result.stdout))
        self.assertEqual(findings, Counter({
            (3, "API-NEVER"): 1,
            (11, "API-NEVER"): 1,
        }), result.stdout)


if __name__ == "__main__":
    unittest.main()
