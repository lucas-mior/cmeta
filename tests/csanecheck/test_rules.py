#!/usr/bin/env python3
"""Phase 4.2 end-to-end rule checks against source files and policy lists."""

import pathlib
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []
BASE = pathlib.Path(__file__).resolve().parents[2]


class CsaneRuleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def write(self, name, source):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        return path

    def check(self, name, source, *args):
        path = self.write(name, source)
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(path), *args],
            capture_output=True, text=True, cwd=self.root,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stdout)
        self.assertNotEqual(result.returncode, 2, result.stderr)
        return result

    def expect(self, source, expected, name="src/check.c", *args):
        result = self.check(name, source, *args)
        found = [line.split("error[", 1)[1].split("]", 1)[0]
                 for line in result.stdout.splitlines() if "error[" in line]
        self.assertEqual(sorted(found), sorted(expected), result.stdout)
        self.assertEqual(result.returncode, int(bool(expected)), result.stdout)
        return result

    def test_clean_code(self):
        self.expect("#if defined(X)\n#endif\n"
                    "int f(void) { return SIZEOF(int); }\n", [])

    def test_direct_calls_versus_declarations_and_comments(self):
        self.expect('void strcpy(void);\n'
                    'struct Obj { void (*strcpy)(void); };\n'
                    'void f(struct Obj *obj) {\n'
                    '    obj->strcpy();\n'
                    '    /* strcpy(); */\n'
                    '    char *literal = "strcpy(";\n'
                    '    strcpy();\n'
                    '}\n', ["API-NEVER"])

    def test_nested_and_parenthesized_calls(self):
        self.expect("void f(void) { strcpy(memcpy(0,0,0));\n"
                    "    (strchr)(0,0);\n}\n",
                    ["API-CBASE-ONLY", "API-NEVER", "API-NEVER"])

    def test_call_named_member_not_a_direct_function(self):
        self.expect("void f(void) { obj.memcpy(0); obj->strcpy(0); }\n", [])

    def test_non_call_uses_are_not_forbidden(self):
        self.expect("void memcpy(void *, void *, int);\n"
                    "void (*strcpy)(void);\n"
                    "int f(void) { return 0; }\n", [])

    def test_declaration_initializers_and_array_bounds(self):
        self.expect("void *p = memcpy(0, 0, 1);\n"
                    "int arr[strlen(0)];\n"
                    "int values[] = {strcpy(0, 0)};\n",
                    ["API-CBASE-ONLY", "API-CBASE-ONLY", "API-NEVER"])

    def test_statement_macro_body(self):
        self.expect("#define BODY() do { strcpy(0, 0); } while (0)\n",
                    ["API-NEVER"])
        self.expect("#define OTHER(x) _Generic(x, int: 1)\n"
                    "void f(void) { return; }\n", [])

    def test_macro_expression_call(self):
        self.expect("#define COPY(d, s) memcpy(d, s, 1)\n"
                    "#define BAD() strcpy(0, 0)\n",
                    ["API-NEVER", "API-CBASE-ONLY"])

    def test_api_scanning_is_independent_of_translation_unit_ast(self):
        self.expect("int broken( {\n"
                    "void f(void) { strcpy(0, 0); }\n"
                    "#define COPY(x) memcpy(x, x, 1)\n",
                    ["API-NEVER", "API-CBASE-ONLY"])

    def test_api_nested_macro_calls_and_no_declaration_false_positives(self):
        self.expect("#define BODY() do { strcpy(0, 0); } while (0)\n"
                    "void memcpy(void *, void *, int);\n"
                    "void (*strcpy)(void);\n"
                    "void f(void) { memcpy(0, strlen(0), 1); }\n",
                    ["API-NEVER", "API-CBASE-ONLY", "API-CBASE-ONLY"])

    def test_never_use_names_complete(self):
        names = (BASE / "cbase/functions_never_use.txt").read_text().split()
        source = "void f(void) {\n" + "\n".join(
            f"    {name}();" for name in names) + "\n}\n"
        result = self.expect(source, ["API-NEVER"] * len(names))
        self.assertEqual(result.stdout.count("error[API-NEVER]"), len(names))

    def test_cbase_only_names_complete(self):
        names = (BASE / "cbase/functions_allowed_cbase_only.txt")
        names = names.read_text().split()
        source = "void f(void) {\n" + "\n".join(
            f"    {name}();" for name in names) + "\n}\n"
        self.expect(source, ["API-CBASE-ONLY"] * len(names))
        self.expect(source, [], "cbase/inside.c")

    def test_conditions_and_macro_string_not_directives(self):
        self.expect('#define TEXT "#ifdef X"\n#if defined(X)\n'
                    '#else\n#endif\n', [])
        self.expect('#ifdef X\n#ifndef Y\n#endif\n#endif\n',
                    ["PP-DEFINED", "PP-DEFINED"])

    def test_sizeof_source_and_macro(self):
        self.expect("#define SIZEOF(t) ((int64)sizeof(t))\n"
                    "#define OTHER(t) sizeof(t)\n"
                    "int f(void) { return sizeof(int) + SIZEOF(int); }\n",
                    ["PP-SIZEOF", "PP-SIZEOF"])

    def test_sizeof_keyword_table_is_not_an_operator(self):
        self.expect("#define WORDS X(SIZEOF, sizeof)\n"
                    "#define RAW_SIZEOF sizeof\n"
                    "int after = 1;\n", [])

    def test_sizeof_text_in_comments_and_literals(self):
        self.expect('char *text = "sizeof(int)"; /* sizeof(int) */\n', [])

    def test_whitespace_violations_and_aggregation(self):
        result = self.expect("int a;\t \n" + "x" * 81 + "\n",
                             ["SRC-TABS", "SRC-TRAILING", "SRC-COLUMNS"],
                             "src/whitespace.c", "--stats")
        self.assertIn("violations=3", result.stdout)
        self.assertRegex(result.stdout, r"whitespace.c:1:7: error\[SRC-TABS\]")

    def test_utf8_column_count(self):
        self.expect("// " + "é" * 77 + "\n", [])
        self.expect("// " + "é" * 78 + "\n", ["SRC-COLUMNS"])

    def test_cecup_100_columns(self):
        line = "// " + "x" * 88 + "\n"
        self.expect(line, ["SRC-COLUMNS"])
        self.expect(line, [], "cecup/src/a.c")

    def test_ryu_exceptions(self):
        self.expect("#if 1\n#endif // why\nint n = sizeof(int);",
                    ["RYU-ENDIF-COMMENT", "RYU-FINAL-NEWLINE"],
                    "cbase/ryu/f2s.c")
        self.expect("#ifdef X\n#endif\n", ["PP-DEFINED"],
                    "cbase/ryu/f2s.h")

    def test_fixture_exclusion_keeps_api_checks(self):
        self.expect("#ifdef X\nvoid f(void) { strcpy(); }\n"
                    "#endif\n\t\n", ["API-NEVER"],
                    "tests/fixtures/case.c")

    def test_parser_failure_does_not_prevent_source_rules(self):
        result = self.expect("int broken( {\n#ifdef X\n", ["PP-DEFINED"])
        self.assertNotIn("note[", result.stdout)
        self.expect("int broken( {\n#ifdef X\n", ["PP-DEFINED"],
                    "src/partial.c", "--report-parse-issues")

    def test_code_location(self):
        result = self.expect("void f(void) {\n    strcpy(0, 0);\n}\n",
                             ["API-NEVER"])
        self.assertRegex(result.stdout, r"check.c:2:5: error\[API-NEVER\]")


if __name__ == "__main__":
    unittest.main(verbosity=2)
