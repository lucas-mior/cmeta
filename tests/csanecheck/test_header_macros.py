#!/usr/bin/env python3
"""Imported macros and bounded multi-token syntax projections."""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

EXECUTABLE = pathlib.Path(sys.argv[1]).resolve()
sys.argv[1:] = []


class HeaderMacroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def scan(self, source):
        path = self.write("sample.c", source)
        run = subprocess.run(
            [str(EXECUTABLE), "--file", str(path), "--coverage",
             "--report-parse-issues"],
            capture_output=True, text=True, check=False, timeout=30,
        )
        self.assertIn(run.returncode, (0, 1), run)
        self.assertEqual(run.stderr, "")
        return run.stdout

    def count(self, report):
        return len(re.findall(r"error\[STYLE-STRLIT-LEN\]", report))

    def test_direct_header_alias(self):
        self.write("check.h", "#ifndef CHECK_H\n#define CHECK_H\n"
                  "#define CHECK STRLIT_LEN\n#endif\n")
        report = self.scan('#include "check.h"\n'
                           'void f(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("parse_issues=0", report)
        self.assertIn("projected_macros=1", report)
        self.assertRegex(report, r"sample.c:2:16: error\[STYLE-STRLIT-LEN\]")

    def test_multitoken_wrapper_from_header(self):
        self.write("wrap.h", "#define CALL(x) STRLIT_LEN(x)\n")
        report = self.scan('#include "wrap.h"\n'
                           'void f(void) { CALL("bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("parse_issues=0", report)
        self.assertIn("projected_macros=1", report)

    def test_transitive_header_and_pasted_function_name(self):
        self.write("inc/second.h", "#define NAME final\n")
        self.write("inc/first.h", '#include "second.h"\n')
        report = self.scan('#include "inc/first.h"\n'
                           'int32 CAT(function_, NAME)(void) { return 1; }\n')
        self.assertIn("function_definitions=1", report)
        self.assertIn("parse_issues=0", report)
        self.assertIn("projected_macros=1", report)

    def test_header_order_and_local_override(self):
        self.write("old.h", "#define CHECK BAD\n")
        self.write("new.h", "#undef CHECK\n#define CHECK STRLIT_LEN\n")
        report = self.scan('void early(void) { CHECK("bad"); }\n'
                           '#include "old.h"\n'
                           'void middle(void) { CHECK("bad"); }\n'
                           '#include "new.h"\n'
                           'void late(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("function_definitions=3", report)

    def test_local_undef_disables_imported_alias(self):
        self.write("check.h", "#define CHECK STRLIT_LEN\n")
        report = self.scan('#include "check.h"\n'
                           '#undef CHECK\n'
                           'void f(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 0)
        self.assertIn("projected_macros=0", report)

    def test_feature_conditional_import_is_not_assumed(self):
        self.write("check.h", "#define CHECK STRLIT_LEN\n")
        report = self.scan('#if FEATURE\n#include "check.h"\n#endif\n'
                           'void f(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 0)
        self.assertIn("projected_macros=0", report)

    def test_feature_conditional_redefinition_is_not_assumed(self):
        self.write("check.h", "#define CHECK STRLIT_LEN\n"
                  "#if FEATURE\n#define CHECK OTHER\n#endif\n")
        report = self.scan('#include "check.h"\n'
                           'void f(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 0)
        self.assertIn("projected_macros=0", report)

    def test_multitoken_macro_defined_locally(self):
        report = self.scan('#define CALL(f, x) f(x)\n'
                           'void f(void) { CALL(STRLIT_LEN, "bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("parse_issues=0", report)
        self.assertIn("projected_macros=1", report)

    def test_multitoken_object_macro_without_slots_is_not_projected(self):
        report = self.scan('#define TWO int32 name\n'
                           'TWO;\nvoid f(void) { return; }\n')
        self.assertIn("projected_macros=0", report)
        self.assertIn("function_definitions=1", report)

    def test_nested_wrapper_preserves_rule_target(self):
        self.write("check.h", "#define STRLIT_LEN(x) (sizeof(x) - 1)\n"
                  "#define WRAP(x) STRLIT_LEN(x)\n")
        report = self.scan('#include "check.h"\n'
                           'void f(void) { STRLIT_LEN("bad"); WRAP("bad"); }\n')
        self.assertEqual(self.count(report), 2)
        self.assertIn("parse_issues=0", report)
        self.assertIn("projected_macros=1", report)

    def test_multitoken_paste_from_header(self):
        self.write("paste.h", "#define PASTE(a, b) a##b\n")
        report = self.scan('#include "paste.h"\n'
                           'void f(void) { PASTE(STRLIT_, LEN)("bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("projected_macros=1", report)

    def test_later_header_redefinition_overrides_earlier(self):
        self.write("first.h", "#define CHECK STRLIT_LEN\n")
        self.write("second.h", "#undef CHECK\n#define CHECK OTHER\n")
        report = self.scan('#include "first.h"\n'
                           'void first(void) { CHECK("bad"); }\n'
                           '#include "second.h"\n'
                           'void second(void) { CHECK("bad"); }\n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("projected_macros=2", report)

    def test_generated_operator_not_judged_by_source_spacing(self):
        report = self.scan('#define MULTIPLY(a, b) a*b\n'
                           'void f(void) {\n'
                           '    int32 x = MULTIPLY(1,2);\n'
                           '    (void)x;\n}\n')
        self.assertIn("projected_macros=1", report)
        self.assertNotIn("error[FMT-MULTIPLY]", report)
        self.assertIn("parse_issues=0", report)

    def test_include_cycle_stops_and_source_rules_preserved(self):
        self.write("a.h", '#include "b.h"\n#define CHECK STRLIT_LEN\n')
        self.write("b.h", '#include "a.h"\n')
        report = self.scan('#include "a.h"\n'
                           'void f(void) { CHECK("bad"); }  \n')
        self.assertEqual(self.count(report), 1)
        self.assertIn("error[SRC-TRAILING]", report)


if __name__ == "__main__":
    unittest.main(verbosity=2)
