#!/usr/bin/env python3
"""Phase 5.2 control-flow and structural AST regression tests."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []


class CsaneControlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, code, *flags, filename="src/sample.c"):
        path = self.root / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(code, encoding="utf-8")
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(path), *flags],
            cwd=self.root, capture_output=True, text=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stdout)
        return result

    def expect(self, code, braces=0, switches=0, blocks=0, *flags,
               filename="src/sample.c"):
        result = self.check(code, *flags, filename=filename)
        counts = {
            "AST-BRACES": braces,
            "AST-SWITCH-ALIGN": switches,
            "AST-ARTIFICIAL-BLOCK": blocks,
        }
        for rule, expected in counts.items():
            actual = result.stdout.count(f"error[{rule}]")
            self.assertEqual(actual, expected, result.stdout)
        other = [line for line in result.stdout.splitlines()
                 if "error[" in line and not any(f"error[{r}]" in line
                                                 for r in counts)]
        self.assertEqual(other, [], result.stdout)
        self.assertEqual(result.returncode, int(any(counts.values())),
                         result.stdout)
        return result

    def test_control_kinds_require_braces(self):
        self.expect("void f(int x) {\n"
                    "    if (x) foo();\n"
                    "    while (x) foo();\n"
                    "    for (; x; ) foo();\n"
                    "    do foo(); while (x);\n"
                    "    switch (x) foo();\n"
                    "}\n", braces=5)

    def test_else_and_else_if(self):
        self.expect("void f(int x) {\n"
                    "    if (x) { foo(); } else foo();\n"
                    "}\n", braces=1)
        self.expect("void f(int x) {\n"
                    "    if (x) { foo(); }\n"
                    "    else if (x > 1) { bar(); }\n"
                    "    else { baz(); }\n"
                    "}\n")

    def test_empty_loop_exceptions(self):
        self.expect("void f(int x) {\n"
                    "    for (; x; );\n"
                    "    for (; x; ) ;\n"
                    "    while (x);\n"
                    "}\n", braces=1)
        self.expect("void f(int x) {\n"
                    "    do {} while (x);\n"
                    "}\n")

    def test_nested_dangling_else(self):
        self.expect("void f(int x) {\n"
                    "    if (x) if (x) foo(); else bar();\n"
                    "}\n", braces=3)

    def test_preprocessor_alternatives_are_independent(self):
        self.expect("void f(int x) {\n"
                    "#if defined(A)\n"
                    "    if (x) foo();\n"
                    "#else\n"
                    "    while (x) bar();\n"
                    "#endif\n"
                    "}\n", braces=2)

    def test_switch_labels_and_default(self):
        result = self.expect("void f(int x) {\n"
                             "    switch (x) {\n"
                             "        case 1: foo();\n"
                             "    case 2: foo();\n"
                             "        default: break;\n"
                             "    }\n"
                             "}\n", switches=2)
        self.assertIn("sample.c:3:9", result.stdout)
        self.assertIn("sample.c:5:9", result.stdout)

    def test_nested_switch_uses_nearest_parent(self):
        self.expect("void f(int x) {\n"
                    "    switch (x) {\n"
                    "    case 1: {\n"
                    "        switch (x) {\n"
                    "        case 2: break;\n"
                    "            default: break;\n"
                    "        }\n"
                    "    }\n"
                    "    default: break;\n"
                    "    }\n"
                    "}\n", switches=1)

    def test_scoped_case_label(self):
        self.expect("void f(int x) {\n"
                    "    switch (x) {\n"
                    "    case 1: {\n"
                    "        foo();\n"
                    "    }\n"
                    "    case 2: break;\n"
                    "    }\n"
                    "}\n")

    def test_deep_artificial_block(self):
        self.expect("void f(int x) {\n"
                    "    if (x) {\n"
                    "        if (x) {\n"
                    "            { foo(); }\n"
                    "        }\n"
                    "    }\n"
                    "}\n", blocks=1)
        self.expect("void f(void) {\n"
                    "    { foo(); }\n"
                    "}\n")

    def test_deep_control_and_label_bodies_not_artificial(self):
        self.expect("void f(int x) {\n"
                    "    if (x) {\n"
                    "        if (x) {\n"
                    "            while (x) { foo(); }\n"
                    "        }\n"
                    "    }\n"
                    "    switch (x) {\n"
                    "    case 1:\n"
                    "            { foo(); }\n"
                    "    }\n"
                    "}\n")

    def test_initializer_record_and_string_braces_not_blocks(self):
        self.expect("struct Item { int a; };\n"
                    "void f(int x) {\n"
                    "    if (x) {\n"
                    "        if (x) {\n"
                    "            int array[] = {1, 2};\n"
                    "            char *s = \"{ }\";\n"
                    "        }\n"
                    "    }\n"
                    "}\n")

    def test_generated_headers_exempt_structural_rules(self):
        source = ("void f(int x) {\n"
                  "    if (x) {\n"
                  "        if (x) {\n"
                  "            { foo(); }\n"
                  "            if (x) foo();\n"
                  "        }\n"
                  "    }\n"
                  "}\n")
        self.expect(source, filename="src/sample.meta.h")
        self.expect("void f(void) { int x, y; }\n",
                    filename="src/sample.meta.h")

    def test_ryu_and_fixture_exemptions(self):
        source = "void f(int x) { if (x) foo(); }\n"
        self.expect(source, filename="cbase/ryu/d2s.c")
        self.expect(source, filename="tests/fixtures/example.c")

    def test_policy_exemptions(self):
        config = self.root / "rules.conf"
        config.write_text("allow-rule AST-BRACES .*sample\\.c\n"
                          "allow-rule AST-SWITCH-ALIGN .*sample\\.c\n"
                          "allow-rule AST-ARTIFICIAL-BLOCK .*sample\\.c\n",
                          encoding="utf-8")
        self.expect("void f(int x) {\n"
                    "    if (x) foo();\n"
                    "    switch (x) {\n"
                    "        case 1: break;\n"
                    "    }\n"
                    "}\n", 0, 0, 0, "--config", str(config))

    def test_unsupported_syntax_does_not_suppress_source_rules(self):
        result = self.check("void f( {\n#ifdef X\n")
        self.assertIn("error[PP-DEFINED]", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
