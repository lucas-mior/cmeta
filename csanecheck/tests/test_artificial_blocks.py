#!/usr/bin/env python3
"""AST-ARTIFICIAL-BLOCK: standalone braces need a direct local variable."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[AST-ARTIFICIAL-BLOCK\]")


class ArtificialBlockTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "artificial_blocks.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_original_case_without_a_declaration(self):
        self.check('''void f(void) {
    {
        fmt_printf("using default: ");
        switch (option.type) {
        case TYPE_BOOL:
            fmt_printf("%s\\n", bool_str(option.boolean));
            break;
        case TYPE_INT:
            fmt_printf("%d\\n", option.integer);
            break;
        default:
            UNREACHABLE();
        }
    }
}
''', [(2, 5)])

    def test_flattened_case_has_no_diagnostic(self):
        self.check('''void f(void) {
    fmt_printf("using default: ");
    switch (option.type) {
    case TYPE_BOOL:
        fmt_printf("%s\\n", bool_str(option.boolean));
        break;
    default:
        UNREACHABLE();
    }
}
''', [])

    def test_direct_local_variables_preserve_standalone_scope(self):
        self.check('''typedef int32 LocalType;
void f(void) {
    {
        int32 scalar = 1;
        use(scalar);
    }
    {
        LocalType typed = 2;
        use(typed);
    }
    {
        struct Value object;
        use(object);
    }
    {
        int32 (*callback)(int32) = select_callback();
        use(callback);
    }
    {
        struct Value {
            int32 x;
        } value;
        use(value);
    }
}
''', [])

    def test_file_pointer_from_header_preserves_scope(self):
        self.check('''#include "cbase.h"
void f(void) {
    {
        FILE *save;

        if (!(save = fopen(path, "w"))) {
            report_failure();
            return;
        }
        if (fmt_fprintf(save, "%d\\n", value) < 0) {
            report_failure();
        }
        XFCLOSE(save, path);
    }
}
''', [])

    def test_unknown_uppercase_multiplication_is_not_a_declaration(self):
        self.check('''void f(void) {
    {
        SIZE *factor;
    }
}
''', [(2, 5)])

    def test_declarations_in_child_control_do_not_count(self):
        self.check('''void f(void) {
    {
        if (condition) {
            int32 nested = 1;
            consume(nested);
        }
    }
    {
        for (int32 i = 0; i < 2; i += 1) {
            consume(i);
        }
    }
}
''', [(2, 5), (8, 5)])

    def test_control_bodies_and_function_body_are_necessary(self):
        self.check('''void f(void) {
    if (condition) {
        use_value();
    } else {
        use_other();
    }
    while (condition) {
        use_value();
    }
    do {
        use_value();
    } while (condition);
    switch (value) {
    case 1:
        use_value();
        break;
    default:
        break;
    }
}
''', [])

    def test_nested_and_case_label_blocks(self):
        self.check('''void f(void) {
    {
        {
            perform();
        }
    }
    switch (choice) {
    case 1:
        {
            perform();
        }
        break;
    case 2:
        {
            int32 required_scope = choice;
            perform(required_scope);
        }
        break;
    default:
        break;
    }
}
''', [(2, 5), (3, 9), (9, 9)])

    def test_typedef_is_not_a_variable(self):
        self.check('''void f(void) {
    {
        typedef int32 Alias;
        use_value();
    }
}
''', [(2, 5)])

    def test_expression_is_not_a_pointer_declaration(self):
        self.check('''void f(void) {
    {
        first *second;
    }
}
''', [(2, 5)])

    def test_nested_conditional_branches(self):
        self.check('''void f(void) {
#if ENABLED
    {
        use_enabled();
    }
#else
    {
        int32 local = 1;
        use(local);
    }
#endif
}
''', [(3, 5)])


if __name__ == "__main__":
    unittest.main()
