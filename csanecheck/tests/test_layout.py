#!/usr/bin/env python3
"""Token-only operator layout and local rule coverage tests."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r"error\[(FMT-[A-Z-]+)\]")
LAYOUT = re.compile(r"group=LAYOUT status=(\w+) pass=(\d+) "
                    r"eligible=(\d+) checked=(\d+) excluded=(\d+) "
                    r"unresolved=(\d+)")


class OperatorLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "operators.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, *, unresolved=0, require=False):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--file", str(self.path),
                   "--rule-coverage"]
        if require:
            command.append("--require-coverage")
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=5)
        self.assertEqual(DIAGNOSTIC.findall(result.stdout), expected,
                         result.stdout + result.stderr)
        match = LAYOUT.search(result.stdout)
        self.assertIsNotNone(match, result.stdout)
        status, pass_number, eligible, checked, excluded, missing = (
            match.groups())
        self.assertEqual(pass_number, "1")  # Before any AST parsing.
        self.assertEqual(int(missing), unresolved, result.stdout)
        self.assertEqual(int(eligible), int(checked) + int(excluded)
                         + int(missing))
        self.assertEqual(status, "partial" if unresolved else "checked")
        return result

    def test_multiplication_and_pointer_declarators(self):
        self.check("""typedef int32 MyType;
typedef struct NcScreen NcScreen;
typedef struct NcWindow NcWindow;
typedef struct Callbacks {
    NcWindow *(*active_window)(NcScreen *);
    char *(*title)(NcScreen *);
} Callbacks;
int32 *f(int32 *p, MyType *q) {
    int32 (*function)(int32) = 0;
    MyType *result = q;
    return *p + *result;
}
""", [])
        self.check("""int32 f(int32 a, int32 b) {
    int32 x = a * b;
    return a* b;
}
""", ["FMT-MULTIPLY", "FMT-MULTIPLY"])
        self.check("""typedef int32 Result;
int32 f(Result callback, int32 value) {
    return callback *(*function)(value);
}
""", ["FMT-MULTIPLY"])

    def test_pointer_declaration_in_for_initializer(self):
        self.check("""void f(Arena *arena) {
    for (Arena *item = arena; item; item = item->next) {
        use(item);
    }
}
""", [])
        self.check("""void f(int32 x, int32 y) {
    for (int32 product = x * y; product > 0; product -= 1) {
        use(product);
    }
}
""", ["FMT-MULTIPLY"])

    def test_call_multiplication_is_not_a_parameter_declarator(self):
        self.check("""double f(int32 i, double denominator) {
    double a2 = 0.5;
    return a2*cos(4.0*PI*(double)i /denominator);
}
""", [])

    def test_macro_generated_function_name_parameters(self):
        self.check("""static void
CAT(hash_zero_, HASH_TYPE)(struct Map *map,
                           HASH_KEY_TYPE *key
#if HASH_FIXED
                           , int32 values[count * 2]
#endif
                           ) {
    use(map, key);
}
""", ["FMT-MULTIPLY"], unresolved=1)

    def test_macro_annotated_pointer_return(self):
        self.check("""INLINE UNUSED char *
MEM_LITERAL_SHORT_FUNCTION(char *haystack) {
    return haystack;
}
""", [])

    def test_lowercase_typedef_pointer_and_same_named_variable(self):
        self.check("""typedef int32 custom_t;
custom_t *pointer;
int32 f(int32 custom_t, int32 p) {
    return custom_t *p;
}
""", ["FMT-MULTIPLY"])

    def test_unary_cast_and_binary_ops(self):
        self.check("""int32 f(int32 x, int32 y) {
    int32 *p = &x;
    int32 r = (int32)-x + (int32)*p;
    ldouble real = (ldouble)-1.25 + (ldouble)-0.0L;
    return r + y;
}
""", [])
        self.check("""int32 f(int32 x, int32 y) {
    return x+y + (x<<y) + (x&&y);
}
""", ["FMT-BINARY-SPACING"]*3)

    def test_assignment_and_initializers(self):
        self.check("""int32 f(void) {
    int32 a=1;
    a+=2;
    a *=3;
    return a;
}
""", ["FMT-ASSIGN-SPACING"]*3)
        self.check("""int32 f(void) {
    int32 a = 1;
    a += 2;
    return a;
}
""", [])

    def test_preprocessor_alternatives_and_macro_replacements(self):
        self.check("""#define ADD(a, b) ((a)+(b))
#if 1
int32 f(int32 x) { return x+x; }
#else
int32 f(int32 x) { return x*x; }
#endif
""", ["FMT-BINARY-SPACING"])

    def test_declaration_cast_vs_multiplication(self):
        self.check("""typedef int32 MyType;
int32 f(int32 x, int32 y) {
    MyType *p = (MyType*)&x;
    return (MyType)*p + x*y;
}
""", [])

    def test_lowercase_typedef_hints_resolve_pointer_declarations(self):
        self.check("""typedef int32 local_type;
void f(void) {
    local_type *pointer;
    {
        typedef int32 nested_type;
        nested_type *nested;
    }
}
""", [])

    def test_aggregate_tags_and_typedef_aliases_are_type_hints(self):
        self.check("""struct lower_tag { int32 value; };
typedef struct lower_tag lower_type;
union lower_union { int32 value; double other; };
enum lower_enum { LOWER_VALUE = 1 };
void f(void) {
    struct lower_tag *tag_pointer;
    lower_type *alias_pointer;
    union lower_union *union_pointer;
    enum lower_enum *enum_pointer;
}
""", [])

    def test_type_hints_do_not_turn_expressions_into_declarations(self):
        self.check("""typedef int32 local_type;
int32 f(int32 local_type, int32 value) {
    return local_type * value;
}
""", ["FMT-MULTIPLY"])

    def test_unknown_bare_type_remains_ambiguous(self):
        self.check("""void f(void) {
    int32 known = 1 + 2;
    mystery_type *pointer;
}
""", [], unresolved=1)

    def test_unknown_pointer_types_do_not_look_like_multiplication(self):
        self.check("""void f(void) {
    DIR *processes;
    uchar *h = haystack;
    uchar *n = needle;
    uchar *end;
}
""", [], unresolved=2)

    def test_declaration_boundaries_vs_real_multiplication(self):
        self.check("""typedef int32 custom_t;
custom_t *global_pointer;
int32 f(int32 a, int32 b) {
    custom_t *local_pointer = global_pointer;
    int32 product = a * b;
    return a* b;
}
""", ["FMT-MULTIPLY", "FMT-MULTIPLY"])
        self.check("""int32 f(int32 a, int32 b) {
    return a *b;
}
""", ["FMT-MULTIPLY"])

    def test_parameter_context_distinguishes_pointers_from_array_math(self):
        self.check("""void f(char *, int32 *named,
       int32 (*compare)(void *, const char *));
""", [])
        self.check("""typedef void *AllocatorMalloc(int64 size,
                                       void *context);
typedef void AllocatorFree(void *pointer, int64 size, void *context);
typedef void *AllocatorRealloc(void *pointer,
                              int64 old_size, int64 new_size,
                              void *context);
""", [])
        self.check("""typedef bool (*RunFunction)(void *backend,
                                    ModelInput *input,
                                    Emissions *emissions,
                                    int32 values[count * 2],
                                    Result *result);
""", ["FMT-MULTIPLY"])
        self.check("""typedef struct Callback {
    void (*function)(union Arg *arg);
} Callback;
""", [])
        self.check("""void f(int32 count, int32 values[count * 2]);
""", ["FMT-MULTIPLY"])

    def test_typedef_pointer_after_preprocessor_branch_finishes(self):
        self.check("""typedef struct Pattern {
#if OS_UNIX
    int32 compiled;
#endif
    char *rule_id;
} Pattern;
""", [])

    def test_type_arguments_casts_and_initializers(self):
        self.check("""void f(va_list args, int32 value) {
    int32 *pointer = va_arg(args, int32 *);
    const int32 *other = (const int32 *)pointer;
    char *character = *(char **)pointer;
    int32 product = value * 2;
}
""", ["FMT-MULTIPLY"])

    def test_capitalized_unknown_local_type_stays_ambiguous(self):
        self.check("""void f(void) {
    int32 known = 1 + 2;
    UnknownType *pointer;
}
""", [], unresolved=1)

    def test_unknown_pointer_parameters_and_casts(self):
        self.check("""void f(DIR *processes, uchar *h) {
    uchar *n = h;
}
""", [])
        self.check("""void f(void) {
    uchar *pointer = (uchar*)other;
    uchar *q = (uchar*)*source;
}
""", [])

    def test_cbase_util_pointer_regression(self):
        util = ROOT / "cbase/util.c"
        result = subprocess.run([str(CHECKER), "--file", str(util)],
                                cwd=ROOT, capture_output=True, text=True,
                                check=False)
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        self.assertNotIn("FMT-MULTIPLY", result.stdout)

    def test_parenthesized_multiplication_is_not_a_pointer_cast(self):
        self.check("""int32 f(int32 a, int32 b, int32 c) {
    return (a * b) * c;
}
""", ["FMT-MULTIPLY", "FMT-MULTIPLY"])

    def test_ambiguous_expression_statement_is_unresolved(self):
        self.check("""void f(void) {
    int32 known = 1 + 2;
    a *b;
}
""", [], unresolved=1)

    def test_local_incomplete_expression_is_unresolved(self):
        result = self.check("""int32 f(void) {
    int32 x = (1 +;
    return x;
}
""", [], unresolved=1, require=True)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_ast_parse_error_does_not_poison_layout(self):
        self.check("""#if 0
int32 f(int32 a) { return a+a; }
#else
int32 f(int32 a) { return a*a; }
#endif
""", ["FMT-BINARY-SPACING"])


if __name__ == "__main__":
    unittest.main()
