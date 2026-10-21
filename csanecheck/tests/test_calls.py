#!/usr/bin/env python3
"""Perl-oracle tests for call and function-header rules (Step 10)."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from differential import CSANE, load_rules, run_checkers


class CallLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.categories = {self.rules[f"P{index:02d}"].category
                           for index in range(12, 18)}

    def compare(self, name, source, expected):
        path = self.root / "src" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        comparison = run_checkers([path], self.root, rules=self.rules)
        legacy = Counter((entry.line, entry.category) for entry in
                         comparison.legacy
                         if entry.category in self.categories)
        newer = Counter((entry.line, entry.category) for entry in
                        comparison.csane
                        if entry.category in self.categories)
        target = Counter({(line, self.rules[key].category): count
                          for (line, key), count in expected.items()})
        self.assertEqual(legacy, target, comparison.summary())
        self.assertEqual(newer, target, comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())

    def test_call_first_argument_and_comments(self):
        self.compare("first.c", (
            "void f(void) {\n"
            "    work(\n"
            "        a, b);\n"
            "    other(/* keep */\n"
            "          c, d);\n"
            "}\n"
        ), {(2, "P12"): 1})

    def test_local_prototype_is_not_an_invocation(self):
        self.compare("local_prototype.c", (
            "void f(void) {\n"
            "    int32 work(\n"
            "        int32 a, int32 b);\n"
            "    return_value(\n"
            "        a, b);\n"
            "}\n"
        ), {(4, "P12"): 1})

    def test_calls_in_assignments_and_return_expressions(self):
        self.compare("expressions.c", (
            "int32 f(void) {\n"
            "    result = work(\n"
            "        a, b);\n"
            "    return work(\n"
            "        c, d);\n"
            "}\n"
        ), {(2, "P12"): 1, (4, "P12"): 1})

    def test_function_header_alignment_and_prototypes(self):
        self.compare("headers.c", (
            "static void\n"
            "work(int32 a,\n"
            "    int32 b) {}\n"
            "int32\n"
            "other(int32 (*callback)(int32, int32),\n"
            "   int32 value);\n"
        ), {(3, "P13"): 1, (6, "P13"): 1})

    def test_correct_header_alignment_and_function_pointers(self):
        self.compare("correct_headers.c", (
            "void\n"
            "work(int32 a,\n"
            "     int32 b);\n"
            "typedef int32 (*Callback)(int32 x,\n"
            "    int32 y);\n"
            "void f(void) {\n"
            "    work(a, b);\n"
            "}\n"
        ), {})

    def test_literal_alignment_not_counted_twice_in_nested_calls(self):
        self.compare("nested_literals.c", (
            "void f(void) {\n"
            "    work(prefix(\"a\"\n"
            "      \"b\"), z);\n"
            "}\n"
        ), {(3, "P14"): 1})

    def test_literals_inside_expressions_are_not_adjacent_fragments(self):
        self.compare("expression_literals.c", (
            "void f(void) {\n"
            "    work(prefix + \"a\"\n"
            "       \"b\", z);\n"
            "}\n"
        ), {})

    def test_adjacent_literal_alignment(self):
        self.compare("literal.c", (
            "void f(void) {\n"
            "    work(\"aa\"\n"
            "      \"bb\", arg);\n"
            "}\n"
        ), {(3, "P14"): 1})

    def test_argument_alignment_with_comments_and_nested_calls(self):
        self.compare("arguments.c", (
            "void f(void) {\n"
            "    work(a, /* comment */\n"
            "      nested(x, y), c);\n"
            "}\n"
        ), {(3, "P15"): 1})

    def test_correct_argument_alignment(self):
        self.compare("correct_args.c", (
            "void f(void) {\n"
            "    work(a,\n"
            "         nested(b, c), d);\n"
            "}\n"
        ), {})

    def test_format_string_all_or_none(self):
        self.compare("format.c", (
            "void f(void) {\n"
            "    log(\"value: %d\", a,\n"
            "        b);\n"
            "}\n"
        ), {(2, "P16"): 1})

    def test_format_string_with_arguments_on_other_lines(self):
        self.compare("format_ok.c", (
            "void f(void) {\n"
            "    log(\"value: %d\",\n"
            "        a, b);\n"
            "}\n"
        ), {})

    def test_related_arguments_must_occupy_single_argument_lines(self):
        self.compare("related.c", (
            "void f(void) {\n"
            "    work(buf,\n"
            "         buf_len, other);\n"
            "}\n"
        ), {(3, "P17"): 1})

    def test_related_arguments_on_separate_lines(self):
        self.compare("related_ok.c", (
            "void f(void) {\n"
            "    work(buf,\n"
            "         buf_len,\n"
            "         other);\n"
            "}\n"
        ), {})

    def test_member_access_and_conditional_compilation(self):
        self.compare("branches.c", (
            "void f(void) {\n"
            "#if defined(FEATURE)\n"
            "    object.work(a,\n"
            "      b);\n"
            "#else\n"
            "    object->work(c,\n"
            "      d);\n"
            "#endif\n"
            "}\n"
        ), {(4, "P15"): 1, (7, "P15"): 1})

    def test_macro_replacement_and_invocation(self):
        self.compare("macro.c", (
            "#define FORWARD(x, y) work(x, \\\n"
            "    y)\n"
            "void f(void) {\n"
            "    FORWARD(\n"
            "        a, b);\n"
            "}\n"
        ), {(4, "P12"): 1})

    def test_generated_header_call_exemption(self):
        self.compare("generated.meta.h", (
            "void f(void) {\n"
            "    work(\n"
            "        a, b);\n"
            "}\n"
        ), {})

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


if __name__ == "__main__":
    unittest.main()
