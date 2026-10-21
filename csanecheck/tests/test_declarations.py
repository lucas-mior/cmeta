#!/usr/bin/env python3
"""Differential tests for local declaration recognition (Step 9)."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest

from differential import load_rules, run_checkers


class DeclarationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.category = self.rules["P10"].category

    def compare(self, name, source, expected):
        path = self.root / "src" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        comparison = run_checkers([path], self.root, rules=self.rules)
        legacy = Counter(item.line for item in comparison.legacy
                         if item.category == self.category)
        newer = Counter(item.line for item in comparison.csane
                        if item.category == self.category)
        self.assertEqual(newer, legacy, comparison.summary())
        self.assertEqual(newer, Counter(expected), comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())

    def test_comma_declarators_and_nested_initializer_commas(self):
        self.compare("comma.c", (
            "int32 a, b;\n"
            "double x = foo(1, 2), y = 3;\n"
            "char *one, *two;\n"
            "int32 first[2], second[3];\n"
            "int32 (*first_fn)(int32, int32), (*second_fn)(int32);\n"
            "unsigned long long big, bigger;\n"
            "int32 one[] = {1, 2}, two[] = {3, 4};\n"
            "void f(void) {\n"
            "    for (int32 i = 0, j = 1; i < j; i += 1) {\n"
            "    }\n"
            "}\n"
        ), [1, 2, 3, 4, 5, 6, 7, 9])

    def test_initializer_call_followed_by_same_line_declaration(self):
        self.compare("initializer_call.c", (
            "void f(void) {\n"
            "    int32 a = call(x, y); int32 b;\n"
            "    int32 c = (*fn)(x, y); int32 d;\n"
            "    int32 e = thing.member(x); int32 f;\n"
            "}\n"
        ), [2, 3, 4])

    def test_separate_declarations_and_lexical_blocks(self):
        self.compare("blocks.c", (
            "void f(void) {\n"
            "    int32 a; int32 b;\n"
            "    { int32 c; int32 d; }\n"
            "    int32 e;\n"
            "    { int32 f; } { int32 g; }\n"
            "    int32 h = 2; int32 i;\n"
            "}\n"
        ), [2, 3, 5, 6])

    def test_struct_and_union_members_in_nested_aggregate_types(self):
        self.compare("members.c", (
            "typedef struct Outer {\n"
            "    struct Inner { int32 x; int32 y; } inner;\n"
            "    union { int32 a; int32 b; } value;\n"
            "    int32 first; int32 second;\n"
            "} Outer;\n"
        ), [2, 3, 4])

    def test_function_pointer_variables_but_not_parameters(self):
        self.compare("callbacks.c", (
            "int32 api(int32 (*callback)(int32, int32), int32 x);\n"
            "int32 ordinary(int32 x, int32 y);\n"
            "void f(void) {\n"
            "    int32 (*cb)(int32, int32); int32 (*next)(void);\n"
            "    int32 (*left)(int32), (*right)(int32);\n"
            "}\n"
        ), [4, 5])

    def test_multiple_prototypes_and_typedef_exemption(self):
        self.compare("prototypes.c", (
            "int32 a(int32 x), b(int32 x);\n"
            "typedef int32 (*First)(int32), (*Second)(int32);\n"
            "typedef struct S { int32 a; int32 b; } S;\n"
            "int32 ordinary(int32 x, int32 y);\n"
        ), [1, 3])

    def test_comma_expressions_and_calls_are_not_declarations(self):
        self.compare("expressions.c", (
            "void f(int32 x, int32 y) {\n"
            "    call(x, y);\n"
            "    x = (x, y);\n"
            "    return x, y;\n"
            "    for (x = (x, y); x < y; x += 1) {\n"
            "        call(x, y);\n"
            "    }\n"
            "}\n"
        ), [])

    def test_initializer_blocks_and_compound_literals(self):
        self.compare("initializers.c", (
            "void f(void) {\n"
            "    int32 a[] = {1, 2, 3};\n"
            "    Struct s = (Struct){.x = 1, .y = 2};\n"
            "    Struct t = (Struct){.x = 1}; int32 value;\n"
            "    struct S first = {.x = 1}, second = {.x = 2};\n"
            "}\n"
        ), [4, 5])

    def test_preprocessor_branches(self):
        self.compare("branches.c", (
            "void f(void) {\n"
            "#if defined(A)\n"
            "    int32 a, b;\n"
            "#else\n"
            "    int32 c, d;\n"
            "#endif\n"
            "    int32 e; int32 f;\n"
            "}\n"
        ), [3, 5, 7])

    def test_no_spurious_declaration_in_expression_statements(self):
        self.compare("not_declarations.c", (
            "void f(void) {\n"
            "    return (x, y);\n"
            "    goto label;\n"
            "label:\n"
            "    call(a, b);\n"
            "    Type(x, y);\n"
            "    int32 a = call(x, y);\n"
            "}\n"
        ), [])


if __name__ == "__main__":
    unittest.main()
