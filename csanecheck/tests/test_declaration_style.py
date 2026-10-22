#!/usr/bin/env python3
"""Step 18: shallow declaration and type-style checks."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r"error\[([A-Z][A-Z0-9-]+)\]")
RULES = frozenset({
    "DECL-AFTER-STMT",
    "TYPE-CONST-SIGNATURE",
    "TYPE-ENUM-TYPEDEF",
    "TYPE-POINTER-TYPEDEF",
    "TYPE-SIZE-T",
    "TYPE-STDINT-T",
    "TYPE-STRUCT-TYPEDEF",
    "TYPE-UNION-TYPEDEF",
    "TYPE-VLA",
})


class DeclarationStyleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "style.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, name="style.c"):
        self.path = self.root / name
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        found = [rule for rule in DIAGNOSTIC.findall(result.stdout)
                 if rule in RULES]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        return result

    def test_const_is_forbidden_only_in_function_signatures(self):
        self.check("""static const int32 data = 1;
int32 declared(const char *text);
int32 defined(int32 *const pointer) {
    int32 value = *(const int32 *)pointer;
    return value;
}
""", ["TYPE-CONST-SIGNATURE", "TYPE-CONST-SIGNATURE"])

    def test_size_types_ignore_interface_casts(self):
        self.check("""void f(void) {
    size_t count;
    ptrdiff_t difference;
    ssize_t signed_size;
    int64 converted = (size_t)count;
}
""", ["TYPE-SIZE-T", "TYPE-SIZE-T", "TYPE-SIZE-T"])

    def test_standard_fixed_width_integer_types(self):
        self.check("""void f(void) {
    int8_t a;
    uint16_t b;
    int32_t c;
    uint64_t d;
    int32 good;
    uint64 also_good;
}
""", ["TYPE-STDINT-T"] * 4)

    def test_legacy_integer_types_are_not_checked(self):
        self.check("""void f(void) {
    short a;
    int b;
    long c;
    long long d;
    signed e;
    unsigned f;
    long double precise;
    unsigned char byte;
}
""", [])

    def test_vla_only_when_dynamic_bound_is_syntactically_clear(self):
        self.check("""int32 count(void);
void f(void) {
    int32 fixed[16];
    int32 macro_bound[COUNT];
    int32 ambiguous[count_value];
    int32 dynamic[count()];
    int32 size_bound[SIZEOF(int32)];
}
""", ["TYPE-VLA"])

    def test_struct_enum_union_and_pointer_typedef_policy(self):
        self.check("""typedef struct Good {
    int32 value;
    int32 *pointer_member;
} Good;
struct Bad {
    int32 value;
};
struct ForwardBad;
typedef enum Kind {
    KIND_A = 1,
    KIND_COUNT,
} Kind;
typedef union Value {
    int32 integer;
    double real;
} Value;
typedef int32 *IntPointer;
typedef int32 (*FunctionPointer)(int32);
typedef int32 (*ArrayPointer)[4];
""", ["TYPE-ENUM-TYPEDEF", "TYPE-UNION-TYPEDEF",
       "TYPE-POINTER-TYPEDEF", "TYPE-POINTER-TYPEDEF",
       "TYPE-STRUCT-TYPEDEF", "TYPE-STRUCT-TYPEDEF"])

    def test_xenums_template_enum_typedef_exception(self):
        self.check("""typedef enum ENUM_NAME ENUM_PREFIX_;
""", [], name="xenums.c")

    def test_function_type_typedef_with_pointer_return_is_not_pointer_typedef(self):
        self.check("""typedef void *Allocator(int64 size, void *context);
""", [])

    def test_main_uses_externally_imposed_integer_signature(self):
        self.check("""int
main(int argc, char **argv) {
    return argc == 0 || argv == 0;
}
""", [])

    def test_declaration_after_statement_is_scope_local(self):
        self.check("""void use(int32 value);
void f(void) {
    int32 first;
    int32 second;
    use(first);
    int32 late;
    {
        int32 nested;
        use(nested);
        int32 nested_late;
    }
    use(second);
    {
        int32 fresh_scope;
        use(fresh_scope);
    }
}
""", ["DECL-AFTER-STMT", "DECL-AFTER-STMT"])

    def test_for_counter_and_top_of_loop_body_are_allowed(self):
        self.check("""void use(int32 value);
void f(void) {
    for (int32 i = 0; i < 4; i += 1) {
        int32 value;
        use(value);
    }
}
""", [])

    def test_opaque_predecessor_does_not_force_mixed_declaration_diagnostic(self):
        self.check("""void f(void) {
    mystery_type *broken
    int32 value;
}
""", [])

    def test_unknown_type_declaration_does_not_count_as_code(self):
        self.check("""void f(void) {
    external_type *external;
    int32 value;
    use(external, value);
}
""", [])

    def test_pascal_case_call_counts_as_code_not_declaration(self):
        self.check("""void f(char *path) {
    DeleteFileA(path);
    int32 value;
    use(value);
}
""", ["DECL-AFTER-STMT"])

    def test_step18_rules_are_valid_policy_exemptions(self):
        self.path.write_text("void f(void) {\n    int value;\n}\n",
                             encoding="utf-8")
        policy = self.root / "policy.conf"
        policy.write_text("".join(
            f"allow-rule {rule} .*style\\.c$\n"
            for rule in sorted(RULES)
        ), encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--config", str(policy),
             "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0,
                         result.stdout + result.stderr)
        self.assertNotIn("invalid csanecheck policy directive", result.stderr)
        found = [rule for rule in DIAGNOSTIC.findall(result.stdout)
                 if rule in RULES]
        self.assertEqual(found, [], result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
