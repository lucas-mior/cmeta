#!/usr/bin/env python3
"""Native regression tests for fault-tolerant function/statement scanners."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "csanecheck/tests/structure_test.c"


class StructureTests(unittest.TestCase):
    def test_conditional_body_diagnostics_in_all_branches(self):
        source = ("int32 guarded(int32 x) {\n"
                  "#if ALTERNATIVE\n"
                  "    if (x)\n        return sizeof(x);\n"
                  "#else\n"
                  "    if (x)\n        return sizeof(x);\n"
                  "#endif\n    return 0;\n}\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "guarded.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--require-coverage",
                                     "--report-parse-issues"],
                                    cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(result.stdout.count("error[AST-BRACES]"), 2,
                         result.stdout)
        self.assertEqual(result.stdout.count("error[PP-SIZEOF]"), 2,
                         result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertIn("unresolved=0", result.stdout)

    def test_time_source_guards_have_no_artificial_externals(self):
        result = subprocess.run([str(ROOT / "bin/csanecheck"), "--file",
                                 "cbase/time.c", "--coverage",
                                 "--report-parse-issues", "--strict-parse"],
                                cwd=ROOT, capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertIn("structural_issues=0", result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)

    def test_construct_spanning_branches_is_recovered(self):
        # A brace that opens in either alternative closes after #endif.
        # Each alternative is analyzed with the common prefix and suffix.
        source = ("void guarded(void) {\n"
                  "#if A\n    if (a) {\n"
                  "#else\n    if (b) {\n"
                  "#endif\n        work();\n    }\n}\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "split.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--report-parse-issues",
                                     "--require-coverage"], cwd=ROOT,
                                    capture_output=True, text=True,
                                    check=False)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_conditional_path_structure_gaps_are_recovered(self):
        cases = (
            ("conditional_call_argument",
             "int32 example(int32 key, int32 length) {\n"
             "    return call(key\n"
             "#if WANT_LENGTH\n"
             "              , length\n"
             "#endif\n"
             "                , 1);\n"
             "}\n"),
            ("conditional_if_header",
             "bool example(char *path) {\n"
             "    struct stat st;\n"
             "#if OS_WINDOWS\n"
             "    if (stat(path, &st) != 0) {\n"
             "#else\n"
             "    if (lstat(path, &st) != 0) {\n"
             "#endif\n"
             "        return false;\n"
             "    }\n"
             "    return true;\n"
             "}\n"),
        )
        for name, source in cases:
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / f"{name}.c"
                    path.write_text(source, encoding="utf-8")
                    result = subprocess.run(
                        [str(ROOT / "bin/csanecheck"), "--file",
                         str(path), "--coverage", "--require-coverage",
                         "--report-parse-issues"],
                        cwd=ROOT, capture_output=True, text=True,
                        check=False)
                self.assertIn("opaque_statements=0", result.stdout)
                self.assertIn("opaque_externals=0", result.stdout)
                self.assertIn("unresolved=0", result.stdout)
                self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
                self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_macro_fragments_are_structurally_handled(self):
        cases = (
            ("initializer_macro",
             "static int32 values[] = {\n"
             "    ITEM(A),\n"
             "#if HAVE_B\n"
             "    ITEM(B),\n"
             "#endif\n"
             "    ITEM(C),\n"
             "};\n"),
            ("enum_macro",
             "#if ENABLE\n"
             "enum CAT(ENUM_NAME, _Indices) {\n"
             "#define XX(e) CAT(e, _INDEX),\n"
             "#define XX_2(e, x) CAT(e, _INDEX),\n"
             "#define XX(...) SELECT(XX_, __VA_ARGS__)\n"
             "\n    ENUM_FIELDS\n\n"
             "#undef XX\n"
             "#undef XX_2\n"
             "};\n"
             "#endif\n"),
            ("generated_statement_macro",
             "int32 example(int32 value) {\n"
             "#if ENABLE\n"
             "#if CHAR_REPR\n"
             "#define XX(e) static char e##_name[] = #e;\n"
             "#define XX_2(e, a) static char e##_name[] = {a, '\\0'};\n"
             "#define XX(...) SELECT(XX_, __VA_ARGS__)\n"
             "\n    ENUM_FIELDS\n\n"
             "#undef XX\n"
             "#undef XX_2\n"
             "#endif\n"
             "    switch (value) {\n"
             "    default:\n"
             "        return 0;\n"
             "    }\n"
             "#endif\n"
             "    return value;\n"
             "}\n"),
        )
        for name, source in cases:
            with self.subTest(name=name):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / f"{name}.c"
                    path.write_text(source, encoding="utf-8")
                    result = subprocess.run(
                        [str(ROOT / "bin/csanecheck"), "--file",
                         str(path), "--coverage", "--require-coverage",
                         "--report-parse-issues"],
                        cwd=ROOT, capture_output=True, text=True,
                        check=False)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn("opaque_statements=0", result.stdout)
                self.assertIn("opaque_externals=0", result.stdout)
                self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
                self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_unrelated_malformed_syntax_remains_opaque(self):
        source = ("int broken = (1;\n"
                  "static void okay(void) {\n"
                  "    int32 value = (2;\n"
                  "    return;\n"
                  "}\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "malformed.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [str(ROOT / "bin/csanecheck"), "--file", str(path),
                 "--coverage", "--require-coverage",
                 "--report-parse-issues"],
                cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_guarded_type_and_parameter_regions(self):
        source = ("typedef struct Box {\n"
                  "#if FIRST\n    int32 a;\n"
                  "#if INNER\n    int32 b;\n"
                  "#else\n    int32 c;\n#endif\n"
                  "#else\n    int32 d;\n#endif\n"
                  "} Box;\n"
                  "enum Tags {\n#if FIRST\n    TAG_A = 1,\n"
                  "#else\n    TAG_B = 2,\n#endif\n};\n"
                  "int32 test(int32 a\n#if FIRST\n"
                  "           , int32 b\n#else\n"
                  "           , int32 c\n#endif\n"
                  "           ) { return a; }\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "guarded.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--report-parse-issues",
                                     "--require-coverage"],
                                    cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertNotIn("COVERAGE-INCOMPLETE", result.stdout)
        self.assertIn("function_definitions=1", result.stdout)

    def test_guarded_member_rules_remain_active(self):
        source = ("struct Box {\n#if FIRST\n"
                  "    int32 one, two;\n#else\n"
                  "    int32 three;\n#endif\n};\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "members.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path), "--coverage",
                                     "--report-parse-issues"],
                                    cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(result.stdout.count("error[AST-DECL-ONE]"), 1,
                         result.stdout)
        self.assertNotIn("STRUCTURE-OPAQUE", result.stdout)

    def test_incomplete_type_fragment_remains_unresolved(self):
        source = ("struct Box {\n#if BAD\n"
                  "    int32 broken = (1;\n#else\n"
                  "    int32 okay;\n#endif\n};\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path),
                                     "--require-coverage",
                                     "--report-parse-issues"],
                                    cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_hash_guards_no_longer_create_top_level_opaque(self):
        result = subprocess.run([str(ROOT / "bin/csanecheck"), "--file",
                                 "cbase/hash.c", "--coverage",
                                 "--report-parse-issues"], cwd=ROOT,
                                capture_output=True, text=True,
                                check=False)
        self.assertIn("opaque_externals=0", result.stdout)
        self.assertIn("opaque_statements=0", result.stdout)

    def test_macro_headers_in_repository(self):
        sources = ("cbase/hash.c", "cbase/memory.c", "cbase/threads.c",
                   "cbase/mem_literal_short.h", "cbase/xenums.c",
                   "cbase/ryu/d2s.c", "cbase/ryu/ryu.h")
        for source in sources:
            with self.subTest(source=source):
                result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                         "--file", source, "--coverage",
                                         "--report-parse-issues"], cwd=ROOT,
                                        capture_output=True, text=True,
                                        check=False)
                self.assertNotIn("unrecognized top-level construct",
                                 result.stdout)
                self.assertIn("opaque_externals=0", result.stdout)

    def test_structural_coverage_reports_locations_and_stable_order(self):
        source = ("static void okay(void) {\n"
                  "    int32 value = (2;\n"
                  "    return;\n"
                  "}\n"
                  "int broken = (1;\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ordered.c"
            path.write_text(source, encoding="utf-8")
            command = [str(ROOT / "bin/csanecheck"), "--file", str(path),
                       "--coverage", "--require-coverage"]
            first = subprocess.run(command, cwd=ROOT, capture_output=True,
                                   text=True, check=False)
            second = subprocess.run(command, cwd=ROOT, capture_output=True,
                                    text=True, check=False)
        first_lines = [line for line in first.stdout.splitlines()
                       if line.startswith("coverage_unresolved ")]
        second_lines = [line for line in second.stdout.splitlines()
                        if line.startswith("coverage_unresolved ")]
        self.assertEqual(first.returncode, 1, first.stdout)
        self.assertEqual(first_lines, second_lines)
        self.assertEqual(len(first_lines), 2, first.stdout)
        self.assertIn("structural_issues=2", first.stdout)
        self.assertIn(
            "line=2 column=5 target=statement "
            "reason=\"unknown statement fragment\"", first_lines[0])
        self.assertIn(
            "line=5 column=1 target=top-level "
            "reason=\"ambiguous top-level construct\"", first_lines[1])

    def test_require_coverage_reports_conditional_structure_reason(self):
        source = ("#if defined(__cplusplus)\n"
                  "extern \"C\" {\n"
                  "#endif\n"
                  "int32 foo(void);\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "conditional.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [str(ROOT / "bin/csanecheck"), "--file", str(path),
                 "--require-coverage"], cwd=ROOT, capture_output=True,
                text=True, check=False)
        self.assertEqual(result.returncode, 1, result.stdout)
        lines = [line for line in result.stdout.splitlines()
                 if line.startswith("coverage_unresolved ")]
        self.assertEqual(len(lines), 1, result.stdout)
        self.assertIn(
            "line=2 column=1 target=top-level "
            "reason=\"conditional construct could not be merged\"",
            lines[0])
        self.assertNotIn("coverage file=", result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_unmatched_cpp_wrapper_reports_incomplete_coverage(self):
        source = ("#if defined(__cplusplus)\n"
                  "extern \"C\" {\n"
                  "#endif\n"
                  "int32 foo(void);\n")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing.c"
            path.write_text(source, encoding="utf-8")
            result = subprocess.run([str(ROOT / "bin/csanecheck"),
                                     "--file", str(path),
                                     "--require-coverage",
                                     "--report-parse-issues"], cwd=ROOT,
                                    capture_output=True, text=True,
                                    check=False)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("STRUCTURE-OPAQUE", result.stdout)
        self.assertIn("COVERAGE-INCOMPLETE", result.stdout)

    def test_native_structure_scanning(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "structure_test"
            command = [*compiler, "-std=c11", "-I.", "-Icbase", "-o",
                       str(binary), str(SOURCE), "-lm"]
            build = subprocess.run(command, cwd=ROOT, capture_output=True,
                                   text=True, check=False)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], cwd=ROOT,
                                    capture_output=True, text=True,
                                    check=False)
            self.assertEqual(result.returncode, 0,
                             result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
