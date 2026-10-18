#!/usr/bin/env python3
"""Header typedef discovery and conservative unknown-type inference."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class CsaneTypeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def write(self, name, source):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        return path

    def scan(self, name, *flags):
        result = subprocess.run(
            [str(EXECUTABLE), "--file", str(self.root / name),
             "--coverage", "--report-parse-issues", *flags],
            cwd=self.root, text=True, capture_output=True,
            check=False, timeout=30,
        )
        self.assertEqual(result.stderr, "", result.stderr)
        self.assertNotEqual(result.returncode, 2, result.stdout)
        return result

    def test_transitive_local_includes_and_pointer_declarations(self):
        self.write("inc/types.h", "#if !defined(TYPES_H)\n"
                   "#define TYPES_H\n"
                   "typedef struct Arena { int value; } Arena;\n"
                   "typedef unsigned int Count;\n"
                   "#endif\n")
        self.write("inc/common.h", "#ifndef COMMON_H\n"
                   "#define COMMON_H\n"
                   '#include "types.h"\n'
                   "#endif\n")
        self.write("main.c", '#include "inc/common.h"\n'
                   'static Arena *global_arena = NULL;\n'
                   'Arena *arena_create(Arena *input, Count count) {\n'
                   '    Arena *local = input;\n'
                   '    int len = STRLIT_LEN("abc");\n'
                   '    return local;\n}\n')
        result = self.scan("main.c")
        self.assertIn("function_definitions=1", result.stdout)
        self.assertIn("unsupported_externals=0", result.stdout)
        self.assertIn("parse_issues=0", result.stdout)
        self.assertIn("error[STYLE-STRLIT-LEN]", result.stdout)

    def test_function_pointer_array_and_multiple_typedef_aliases(self):
        self.write("api.h", "typedef int Item, OtherItem;\n"
                   "typedef int Items[4];\n"
                   "typedef int (*Callback)(int);\n")
        self.write("src.c", '#include "api.h"\n'
                   'static Item item;\n'
                   'static OtherItem other;\n'
                   'static Items items;\n'
                   'static Callback callback;\n'
                   'int foo(void) {\n'
                   '    Callback cb = callback;\n'
                   '    return cb(item);\n}\n')
        result = self.scan("src.c")
        self.assertIn("unsupported_externals=0", result.stdout)
        self.assertIn("parse_issues=0", result.stdout)

    def test_conditional_typedef_does_not_become_certain(self):
        self.write("feature.h", "#ifndef FEATURE_H\n"
                   "#define FEATURE_H\n"
                   "#if SOME_FEATURE\n"
                   "typedef int FeatureOnly;\n"
                   "#endif\n"
                   "#endif\n")
        self.write("src.c", '#include "feature.h"\n'
                   "void f(void) {\n"
                   "    FeatureOnly *unknown;\n"
                   "}\n")
        result = self.scan("src.c")
        self.assertIn("unsupported_statements=1", result.stdout)

    def test_infer_types_when_header_missing(self):
        self.write("alone.c", "static MissingType *pointer = NULL;\n"
                   "MissingReturn f(MissingArg *arg) {\n"
                   "    return STRLIT_LEN(\"abc\");\n"
                   "}\n")
        result = self.scan("alone.c")
        self.assertIn("function_definitions=1", result.stdout)
        self.assertIn("unsupported_externals=0", result.stdout)
        self.assertIn("error[STYLE-STRLIT-LEN]", result.stdout)

    def test_real_multiplication_and_shadowing(self):
        self.write("api.h", "typedef int Number;\n")
        self.write("src.c", '#include "api.h"\n'
                   'void f(int a, int b) {\n'
                   '    int c = a * b;\n'
                   '    int Number = 2;\n'
                   '    Number * c;\n'
                   '}\n')
        result = self.scan("src.c")
        self.assertEqual(result.stdout.count("error[FMT-MULTIPLY]"), 2,
                         result.stdout)

    def test_type_declared_later_does_not_leak_backwards(self):
        self.write("src.c", "int f(int x, int y) {\n"
                   "    x * y;\n"
                   "    return 0;\n}\n"
                   "typedef int x;\n")
        result = self.scan("src.c")
        self.assertIn("error[FMT-MULTIPLY]", result.stdout)

    def test_missing_include_is_not_fatal(self):
        self.write("src.c", '#include "unavailable.h"\n'
                   'static Imported *value = NULL;\n')
        result = self.scan("src.c")
        self.assertIn("unsupported_externals=0", result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
