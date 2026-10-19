#!/usr/bin/env python3
"""Step 4 regressions for imported types and preprocessing boundaries."""

from pathlib import Path
import re
import tempfile
import unittest

from test_compatibility import rule_counts, run_checker, write_file


STATS = re.compile(r"\bparse_issues=(\d+)\b")
PROJECTIONS = re.compile(r"\bprojected_macros=(\d+)\b")


class CsanecheckStep4Tests(unittest.TestCase):
    def check_source(self, root, source, *args):
        result = run_checker(
            ["--file", source, "--stats", "--coverage",
             "--report-parse-issues", *args], root
        )
        self.assertNotEqual(result.returncode, 2,
                            result.stdout + result.stderr)
        stats = STATS.search(result.stdout)
        projected = PROJECTIONS.search(result.stdout)
        self.assertIsNotNone(stats, result.stdout)
        self.assertIsNotNone(projected, result.stdout)
        return result, int(stats.group(1)), int(projected.group(1))

    def test_imported_struct_typedef_from_project_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "common.h", (
                "#if !defined(COMMON_H)\n"
                "#define COMMON_H\n"
                "typedef struct Item { int32 value; } Item;\n"
                "#endif\n"
            ))
            source = write_file(root, "src/example.c", (
                '#include "common.h"\n'
                "void\n"
                "example(Item *item) {\n"
                "    Item local = {0};\n"
                "    local.value = item->value;\n"
                "}\n"
            ))
            result, issues, projected = self.check_source(root, source)
            self.assertEqual(issues, 0, result.stdout)
            self.assertEqual(projected, 0, result.stdout)
            self.assertIn("syntax=checked", result.stdout)

    def test_macro_alias_imported_from_project_root_is_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "common.h", (
                "#ifndef COMMON_H\n#define COMMON_H\n"
                "#define CHECK ASSERT\n#endif\n"
            ))
            source = write_file(root, "src/example.c", (
                '#include "common.h"\n'
                "void\nexample(int32 x) {\n"
                "    CHECK(x >= 1);\n}\n"
            ))
            result, issues, projected = self.check_source(root, source)
            self.assertEqual(issues, 0, result.stdout)
            self.assertEqual(projected, 1, result.stdout)
            self.assertEqual(rule_counts(result)["STYLE-ASSERT-REL"], 1,
                             result.stdout)

    def test_including_directory_takes_precedence_over_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "common.h", "#define CHECK ASSERT\n")
            write_file(root, "src/common.h", "#define CHECK OTHER\n")
            source = write_file(root, "src/example.c", (
                '#include "common.h"\n'
                "void\nexample(int32 x) {\n"
                "    CHECK(x >= 1);\n}\n"
            ))
            result, issues, projected = self.check_source(root, source)
            self.assertEqual(issues, 0, result.stdout)
            self.assertEqual(projected, 1, result.stdout)
            self.assertEqual(rule_counts(result)["STYLE-ASSERT-REL"], 0,
                             result.stdout)

    def test_transitive_project_header_types_and_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "detail.h", (
                "#ifndef DETAIL_H\n#define DETAIL_H\n"
                "typedef struct Detail { int32 n; } Detail;\n"
                "#define CHECK ASSERT\n#endif\n"
            ))
            write_file(root, "common.h", (
                "#ifndef COMMON_H\n#define COMMON_H\n"
                '#include "detail.h"\n#endif\n'
            ))
            source = write_file(root, "src/example.c", (
                '#include "common.h"\n'
                "void\nexample(Detail *detail) {\n"
                "    Detail local = {0};\n"
                "    CHECK(local.n >= 1);\n"
                "    (void)detail;\n}\n"
            ))
            result, issues, projected = self.check_source(root, source)
            self.assertEqual(issues, 0, result.stdout)
            self.assertEqual(projected, 1, result.stdout)
            self.assertIn("syntax=checked", result.stdout)

    def test_stdbool_alias_in_source_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/example.c", (
                "void\nexample(void) {\n"
                "    int32 x = 0;\n"
                "    bool is_zero = x == 0;\n"
                "    (void)is_zero;\n}\n"
            ))
            result, issues, _ = self.check_source(root, source)
            self.assertEqual(issues, 0, result.stdout)
            self.assertIn("syntax=checked", result.stdout)

    def test_conditional_include_does_not_imply_unconditional_macro(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_file(root, "extra.h", "#define CHECK ASSERT\n")
            source = write_file(root, "src/example.c", (
                "#if defined(FEATURE)\n"
                '#include "extra.h"\n'
                "#endif\n"
                "void\nexample(int32 x) {\n"
                "    CHECK(x >= 1);\n}\n"
            ))
            result, _, projected = self.check_source(root, source)
            self.assertEqual(projected, 0, result.stdout)
            self.assertNotIn("STYLE-ASSERT-REL", rule_counts(result))

    def test_preprocessor_alternatives_remain_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/example.c", (
                "void\nexample(int32 x) {\n"
                "#if defined(FEATURE)\n"
                "    ASSERT_GE(x, 0);\n"
                "    ASSERT_LT(x, 10);\n"
                "#else\n"
                "    ASSERT_GE(x, 0);\n"
                "    ASSERT_LT(x, 20);\n"
                "#endif\n}\n"
            ))
            result, _, _ = self.check_source(root, source)
            self.assertEqual(rule_counts(result)["STYLE-ASSERT-BOUNDS"], 2,
                             result.stdout)

    def test_malformed_source_is_not_declared_fully_parsed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = write_file(root, "src/example.c", (
                "void\nexample(void) {\n"
                "    int32 x = ;\n}\n"
            ))
            result, issues, _ = self.check_source(root, source)
            self.assertGreater(issues, 0, result.stdout)
            self.assertNotIn("syntax=checked", result.stdout)
            strict = run_checker(["--file", source, "--strict-parse"], root)
            self.assertEqual(strict.returncode, 1, strict.stdout)


if __name__ == "__main__":
    unittest.main()
