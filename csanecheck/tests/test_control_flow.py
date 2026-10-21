#!/usr/bin/env python3
"""Differential tests for control-flow and block diagnostics (Step 8)."""

from collections import Counter
from pathlib import Path
import tempfile
import unittest

from differential import load_rules, run_checkers


class ControlFlowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.categories = {self.rules[key].category for key in
                           ("P08", "P09", "P11")}

    def compare(self, name, source, *, file_list=False):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        comparison = run_checkers([path], self.root, file_list=file_list,
                                  rules=self.rules)
        legacy = Counter((item.line, item.category) for item in
                         comparison.legacy if item.category in
                         self.categories)
        newer = Counter((item.line, item.category) for item in
                        comparison.csane if item.category in
                        self.categories)
        self.assertEqual(newer, legacy, comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())
        return legacy

    def test_all_six_control_bodies_and_else_if(self):
        source = (
            "void f(int32 x) {\n"
            "    if (x) perform();\n"
            "    else perform();\n"
            "    for (int32 i = 0; i < x; i += 1) perform();\n"
            "    while (x) perform();\n"
            "    switch (x) perform();\n"
            "    do perform(); while (x);\n"
            "    if (x) do_it(); else if (x) do_it(); else do_it();\n"
            "}\n"
        )
        result = self.compare("src/unbraced.c", source)
        self.assertEqual(sum(count for (_, category), count in result.items()
                             if category == "control_braces"), 9)

    def test_do_while_and_empty_for_exemptions(self):
        source = (
            "void f(int32 x) {\n"
            "    for (;;);\n"
            "    if (x) {\n"
            "        perform();\n"
            "    } else if (x > 1) {\n"
            "        do {\n"
            "            perform();\n"
            "        } while (x);\n"
            "    } else {\n"
            "        for (;;) {\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/braced.c", source))

    def test_malformed_body_does_not_hide_missing_braces(self):
        source = (
            "void f(void) {\n"
            "    if (x) foo((;\n"
            "    if (y) call();\n"
            "    else bar((;\n"
            "    while (z) other((;\n"
            "}\n"
        )
        result = self.compare("src/malformed.c", source)
        self.assertEqual(result[(2, "control_braces")], 1)
        self.assertEqual(result[(3, "control_braces")], 1)
        self.assertEqual(result[(4, "control_braces")], 1)
        self.assertEqual(result[(5, "control_braces")], 1)

    def test_opaque_block_still_satisfies_braces_rule(self):
        source = (
            "void f(void) {\n"
            "    if (x) {\n"
            "        call((;\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/opaque_block.c", source)
        self.assertNotIn((2, "control_braces"), result)

    def test_nested_switch_alignment_and_scoped_cases(self):
        source = (
            "void f(int32 x) {\n"
            "    switch (x) {\n"
            "        case 0: {\n"
            "            switch (x) {\n"
            "                case 1:\n"
            "                    break;\n"
            "                default:\n"
            "                    break;\n"
            "            }\n"
            "            break;\n"
            "        }\n"
            "        default:\n"
            "            break;\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/switch.c", source)
        self.assertEqual(sum(count for (_, category), count in result.items()
                             if category == "switch_alignment"), 4)

    def test_switch_ternary_case_and_correct_scoped_alignment(self):
        source = (
            "void f(int32 x) {\n"
            "    switch (x) {\n"
            "    case x ? 1 : 2: {\n"
            "        switch (x) {\n"
            "        case 3:\n"
            "            break;\n"
            "        default:\n"
            "            break;\n"
            "        }\n"
            "        break;\n"
            "    }\n"
            "    default:\n"
            "        break;\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/scoped_switch.c", source))

    def test_deep_blocks_and_inline_blocks(self):
        source = (
            "static void\n"
            "process(void) {\n"
            "    if (ready) {\n"
            "        for (int i = 0; i < 2; i += 1) {\n"
            "            { use(i); }\n"
            "            if (valid) { { use(i); } }\n"
            "            use(i); { use(i); }\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/blocks.c", source)
        self.assertEqual(sum(count for (_, category), count in result.items()
                             if category == "artificial_block"), 3)

    def test_aggregate_and_control_braces_are_not_artificial(self):
        source = (
            "static void\n"
            "process(void) {\n"
            "    if (ready) {\n"
            "        while (running) {\n"
            "            if (valid)\n"
            "            { use(1); }\n"
            "            else\n"
            "            { use(2); }\n"
            "            int pairs[][2] = {\n"
            "                {1, 2}, {3, 4},\n"
            "            };\n"
            "            switch (value) {\n"
            "            case 1:\n"
            "            { use(3); break; }\n"
            "            default:\n"
            "            { use(4); break; }\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/not_artificial.c", source))

    def test_macro_directives_and_separate_branches(self):
        source = (
            "#define UNBRACED(x) if (x) call()\n"
            "#define TWO_LINES(x) \\\n"
            "    if (x) call()\n"
            "void f(int32 x) {\n"
            "#if FEATURE\n"
            "    if (x)\n"
            "#else\n"
            "    if (other(x))\n"
            "#endif\n"
            "    { call(); }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/branches.c", source))

    def test_deep_block_after_macro_continuation(self):
        source = (
            "static void\n"
            "process(void) {\n"
            "    if (ready) {\n"
            "        while (running) {\n"
            "#define BLOCK_MACRO() do { \\\n"
            "            { \\\n"
            "            } \\\n"
            "        } while (0)\n"
            "            {\n"
            "                use(1);\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/macro_block.c", source, file_list=True)
        self.assertEqual(result[(9, "artificial_block")], 1)

    def test_switch_labels_inside_conditional_branches(self):
        source = (
            "void f(int32 x) {\n"
            "    switch (x) {\n"
            "#if A\n"
            "        case 1:\n"
            "            break;\n"
            "#else\n"
            "        default:\n"
            "            break;\n"
            "#endif\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/branch_switch.c", source)
        self.assertEqual(result[(4, "switch_alignment")], 1)
        self.assertEqual(result[(7, "switch_alignment")], 1)

    def test_artificial_blocks_inside_conditional_branches(self):
        source = (
            "void f(void) {\n"
            "    if (x) {\n"
            "        while (y) {\n"
            "#if A\n"
            "            { call(); }\n"
            "#else\n"
            "            call();\n"
            "#endif\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/branch_blocks.c", source)
        self.assertEqual(result[(5, "artificial_block")], 1)

    def test_case_label_before_conditional_block_is_exempt(self):
        source = (
            "void f(void) {\n"
            "    switch (x) {\n"
            "    case 1:\n"
            "#if A\n"
            "            { work(); }\n"
            "#endif\n"
            "        break;\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/branch_case_block.c", source))

    def test_directive_does_not_hide_following_artificial_block(self):
        source = (
            "void f(void) {\n"
            "    if (x) {\n"
            "        while (y) {\n"
            "            call();\n"
            "#if A\n"
            "            { call(); }\n"
            "#endif\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/after_directive.c", source)
        self.assertEqual(result[(6, "artificial_block")], 1)

    def test_branch_nested_control_is_not_artificial(self):
        source = (
            "void f(void) {\n"
            "    if (x) {\n"
            "        while (y) {\n"
            "#if A\n"
            "            if (z) { call(); }\n"
            "            { call(); }\n"
            "#endif\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/nested_branch.c", source)
        self.assertEqual(result[(6, "artificial_block")], 1)
        self.assertNotIn((5, "artificial_block"), result)

    def test_shared_brace_after_preprocessor_control_headers(self):
        source = (
            "static void\n"
            "process(void) {\n"
            "    if (ready) {\n"
            "        while (running) {\n"
            "#if defined(USE_FAST)\n"
            "            if (fast)\n"
            "#else\n"
            "            if (slow)\n"
            "#endif\n"
            "            {\n"
            "                use(1);\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("src/shared_brace.c", source))

    def test_shared_artificial_block_after_endif_is_reported(self):
        source = (
            "void f(void) {\n"
            "    if (x) {\n"
            "        while (y) {\n"
            "#if A\n"
            "            call();\n"
            "#endif\n"
            "            { call(); }\n"
            "        }\n"
            "    }\n"
            "}\n"
        )
        result = self.compare("src/after_endif.c", source)
        self.assertEqual(result[(7, "artificial_block")], 1)

    def test_vendored_and_generated_exemptions(self):
        source = (
            "void f(void) {\n"
            "    if (x) call();\n"
            "    switch (x) {\n"
            "        case 1: break;\n"
            "    }\n"
            "}\n"
        )
        self.assertFalse(self.compare("cbase/ryu/sample.c", source))
        self.assertFalse(self.compare("src/sample.meta.h", source))


if __name__ == "__main__":
    unittest.main()
