#!/usr/bin/env python3
"""FMT042: multiple struct initializer entries require separate lines."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[FMT-STRUCT-INITIALIZER\]")


class StructInitializerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "initializers.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True,
            check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_only_designated_struct_initializers(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S a = { .x = 1 };
S b = { 1, 2 };
struct T { int32 x; };
struct T c = {1};
void f(void) {
    S local = { .x = 5, .y = 6 };
    struct T tag = {.x = 2};
    S compound = (S){.x = 1};
    use(a, b, c, local, tag, compound);
}
""", [(7, 15)])

    def test_valid_zero_multiline_arrays_and_non_struct(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S empty = {0};
S vertical = {
    .x = 1,
    .y = 2,
};
int32 array[] = {1, 2, 3, 4,};
int32 indexed[3] = {[0] = 1, [1] = 2};
int32 scalar = {1};
int32 matrix[][2] = {{1, 2}, {3, 4}};
typedef struct S StructArray[2];
typedef struct S *StructPointer;
StructArray array_alias = {{1, 2}, {3, 4}};
StructPointer pointer_alias = {0};
struct T { int32 x; };
struct T good = {0};
""", [])

    def test_compound_literals_and_nested_designators(self):
        self.check("""typedef struct S { int32 x; } S;
S a = (S){.x = 2};
S b = (S){0};
S c = (S){
    .x = 2,
};
S d[] = {{.x = 1}, {.x = 2}};
int32 e[][2] = {{1, 2}, {3, 4}};
""", [])

    def test_members_must_not_share_brace_lines(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S a = { .x = 1,
    .y = 2,
};
S b = {
    .x = 1,
    .y = 2, };
S c = { 
    .x = 1,
    .y = 2,
};
""", [(2, 7), (5, 7)])

    def test_preprocessor_and_macro_body_are_separate(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
#define MAKE_S (S){.x = 3, .y = 4}
#if USE_FIRST
S a = {.x = 1, .y = 2};
#else
S b = {.x = 2, .y = 3};
#endif
// S c = {.x = 5};
""", [(4, 7), (6, 7)])

    def test_ordered_struct_initializers_and_literals_are_exempt(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S a = {1, 2};
S b = (S){1, 2};
S c = {
    1,
    2,
};
struct T { int32 x; int32 y; };
struct T d = {1, 2};
struct T e = (struct T){1, 2};
void f(void) {
    S s = {1, 2};
    use((S){3, 4});
}
""", [])

    def test_mixed_positional_and_designated_initializers(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S a = {1, .y = 2};
S b = (S){1, .y = 2};
S c = {1, 2};
""", [(2, 7), (3, 10)])

    def test_designated_compound_literals_in_expressions(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
struct T { int32 x; };
S f(void) {
    return (S){.x = 1};
}
void g(S *pointer, bool test) {
    *pointer = (S){.x = 2};
    use((S){.x = 3});
    *pointer = test ? (S){.x = 4} : (S){0};
    *pointer = (S){
        .x = 5,
    };
    use((struct T){.x = 6});
}
""", [])

    def test_nested_literals_and_array_designators(self):
        self.check("""typedef struct S { int32 x; } S;
typedef struct Outer { S child; } Outer;
Outer a = {.child = (S){.x = 1}};
Outer b = {
    .child = (S){
        .x = 2,
    },
};
S array[] = {[0].x = 1};
S array2[] = {[0] = {.x = 2}};
S array3[] = {[0] = {1}};
int32 numbers[] = {[0] = 1};
""", [])

    def test_single_field_with_nested_commas_and_trailing_comma(self):
        self.check("""typedef struct S { int32 x; int32 y; } S;
S a = {.x = 1};
S b = {.x = 2,};
S c = (S){.x = call(1, 2)};
S d = {.x = (int32[]){1, 2}[0]};
S e = {.x = call((S){.x = 1, .y = 2}, 3)};
S f = {.x = call(1, 2), .y = 3};
S g = {.x = 1, .y = call(2, 3)};
S h = {.x = 1, 2};
""", [(6, 21), (7, 7), (8, 7), (9, 7)])

    def test_single_field_multiline_still_requires_separate_brace_lines(self):
        self.check("""typedef struct S { int32 x; } S;
S a = {.x = 1,
};
S b = {
    .x = 1};
S c = {
    .x = 1,
};
""", [(2, 7), (4, 7)])

    def test_similar_expressions_are_not_initializers(self):
        self.check("""typedef struct S { int32 x; } S;
int32 f(S *s) {
    if (s->x) { s->x = 1; }
    for (int32 i = 0; i < 3; i += 1) { use(i); }
    return s->x;
}
""", [])


if __name__ == "__main__":
    unittest.main()
