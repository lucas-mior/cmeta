#!/usr/bin/env python3
"""Differential regression checks for Step 7 source and directive policies."""

from collections import Counter
from pathlib import Path
import subprocess
import tempfile
import unittest

from differential import CSANE, load_rules, run_checkers


class SourcePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rules = load_rules()
        self.source_keys = {f"P{i:02d}" for i in range(1, 8)}
        self.categories = {self.rules[key].category for key in
                           self.source_keys}

    def compare(self, relative, contents, *, file_list=False,
                no_column_limit=None):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents.encode("utf-8"))
        comparison = run_checkers([path], self.root, file_list=file_list,
                                  no_column_limit=no_column_limit,
                                  rules=self.rules)
        legacy = Counter((finding.line, finding.category) for finding in
                         comparison.legacy if finding.category in
                         self.categories)
        new = Counter((finding.line, finding.category) for finding in
                      comparison.csane if finding.category in
                      self.categories)
        self.assertEqual(new, legacy, comparison.summary())
        self.assertFalse(comparison.legacy_unknown, comparison.summary())
        self.assertFalse(comparison.csane_unknown, comparison.summary())
        return legacy

    def test_source_lines_and_utf8_columns(self):
        cases = [
            ("src/plain.c", "a" * 80 + "\n", {}),
            ("src/plain.c", "a" * 81 + "\n", {(1, "line_columns"): 1}),
            ("src/unicode.c", "à" * 80 + "\n", {}),
            ("src/unicode.c", "à" * 81 + "\n", {(1, "line_columns"): 1}),
            ("src/tabs.c", "\t\tint32 x;\n", {(1, "tab_character"): 1}),
            ("src/trail.c", "int32 x; \t\n",
             {(1, "trailing_whitespace"): 1, (1, "tab_character"): 1}),
            ("src/clean.c", "int32 x;\n", {}),
        ]
        for relative, source, expected in cases:
            with self.subTest(path=relative, source=source[:24]):
                result = self.compare(relative, source)
                self.assertEqual(result, Counter(expected))

    def test_whitespace_on_overlong_lines(self):
        cases = [
            ("// " + "x" * 81 + "\t  \n",
             {(1, "line_columns"): 1, (1, "tab_character"): 1,
              (1, "trailing_whitespace"): 1}),
            ("// " + "é" * 81 + "\t  \n",
             {(1, "line_columns"): 1, (1, "tab_character"): 1,
              (1, "trailing_whitespace"): 1}),
            ("// " + "x" * 81 + "\n", {(1, "line_columns"): 1}),
            ("// " + "x" * 76 + "\t\n",
             {(1, "tab_character"): 1, (1, "trailing_whitespace"): 1}),
            ("// " + "é" * 74 + "\t \n",
             {(1, "tab_character"): 1, (1, "trailing_whitespace"): 1}),
        ]
        for index, (source, expected) in enumerate(cases):
            with self.subTest(case=index):
                result = self.compare(f"src/overlong_{index}.c", source)
                self.assertEqual(result, Counter(expected))

    def test_ryu_raw_endif_and_final_newline(self):
        cases = [
            ("#endif // marker\n", {(1, "ryu_endif_comment"): 1}),
            ("#endif /* marker */\n", {}),
            ("# /* macro comment */ endif // marker\n", {}),
            ("# endif /* // marker */\n", {(1, "ryu_endif_comment"): 1}),
            ("#endif // marker", {(1, "ryu_endif_comment"): 1,
                                   (1, "ryu_final_newline"): 1}),
            ("int32 x;", {(1, "ryu_final_newline"): 1}),
            ("", {}),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.compare("cbase/ryu/sample.h", source,
                                      file_list=True)
                self.assertEqual(result, Counter(expected))

    def test_preprocessor_directives_across_comments_and_macros(self):
        cases = [
            ("#ifdef A\n#endif\n", {(1, "preprocessor_defined"): 1}),
            ("#  ifndef B\n", {(1, "preprocessor_defined"): 1}),
            ("# /* comment */ ifdef C\n", {(1, "preprocessor_defined"): 1}),
            ("#define X(x) \\\n#ifdef HIDDEN\n",
             {(2, "preprocessor_defined"): 1}),
            ("#if defined(A)\n#elif !defined(B)\n#endif\n", {}),
            ("// #ifdef A\n/* #ifndef B */\n", {}),
            ('char *x = "#ifdef A";\n', {}),
            ("/*\n#ifdef SKIP\n*/\n", {}),
            ("#define X(x) \"#ifdef\"\n", {}),
            ("#ifdefine INVALID\n", {}),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.compare("src/preproc.c", source)
                self.assertEqual(result, Counter(expected))

    def test_sizeof_directives_and_macro_exemptions(self):
        cases = [
            ("int32 n = sizeof(int32);\n", {(1, "builtin_sizeof"): 1}),
            ("int32 n = sizeof *p;\n", {(1, "builtin_sizeof"): 1}),
            ("#if sizeof(int32)\n#endif\n", {(1, "builtin_sizeof"): 1}),
            ("#pragma message sizeof(int32)\n", {(1, "builtin_sizeof"): 1}),
            ("#define sizeof(X) X\n", {(1, "builtin_sizeof"): 1}),
            ("#define VALUE(X) sizeof(X)\n", {(1, "builtin_sizeof"): 1}),
            ("#define SIZEOF(X) ((int64)sizeof(X))\n", {}),
            ("#define SIZEOF(X) \\\n    ((int64)sizeof(X))\n", {}),
            ("// sizeof(int32)\n", {}),
            ('char *x = "sizeof(int32)";\n', {}),
            ("int32 my_sizeof = 1;\n", {}),
            ("XX(C_KEYWORD_SIZEOF, sizeof)\n", {}),
            ("#include <sizeof.h>\n", {}),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                result = self.compare("src/sizeof.c", source)
                self.assertEqual(result, Counter(expected))

    def test_generated_headers_and_fixtures_are_not_checked(self):
        violation = "#ifdef BAD\n\tint32 x = sizeof(int32);  \n#endif\n"
        for relative in ("src/sample.meta.h", "tests/fixtures/bad.c"):
            with self.subTest(path=relative):
                self.assertFalse(self.compare(relative, violation,
                                              file_list=True))

    def test_ryu_exemption_is_direct_only(self):
        source = "int32 n = sizeof(int32);\n"
        self.assertFalse(self.compare("cbase/ryu/direct.c", source))
        self.assertEqual(self.compare("cbase/ryu/nested/file.c", source),
                         Counter({(1, "builtin_sizeof"): 1}))

    def test_cecup_100_column_limit(self):
        self.assertFalse(self.compare("cecup/src/limit.c", "x" * 100 + "\n"))
        self.assertEqual(self.compare("cecup/src/limit.c",
                                      "x" * 101 + "\n"),
                         Counter({(1, "line_columns"): 1}))

    def test_cecup_project_relative_sources_use_100_columns(self):
        project = self.root / "cecup"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 98 + "\n"
                          + "//" + "x" * 99 + "\n", encoding="utf-8")
        file_list = project / "files.txt"
        file_list.write_text("./src/limit.c\n", encoding="utf-8")
        commands = [
            ["--root", "."],
            ["--file", "./src/limit.c"],
            ["--file-list", "files.txt"],
            ["./src/limit.c"],
        ]
        for arguments in commands:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(CSANE), *arguments], cwd=project,
                                        capture_output=True, text=True,
                                        check=False)
                self.assertEqual(result.returncode, 1, result.stderr)
                columns = [line for line in result.stdout.splitlines()
                           if "error[SRC-COLUMNS]" in line]
                self.assertEqual(len(columns), 1, result.stdout)
                self.assertIn("limit.c:2:1:", columns[0])

    def test_non_cecup_relative_sources_keep_80_columns(self):
        project = self.root / "other"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 79 + "\n", encoding="utf-8")
        result = subprocess.run([str(CSANE), "--root", "."], cwd=project,
                                capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("limit.c:1:1: error[SRC-COLUMNS]", result.stdout)

    def test_cecup_config_column_override_takes_precedence(self):
        project = self.root / "cecup"
        source = project / "src/limit.c"
        source.parent.mkdir(parents=True)
        source.write_text("//" + "x" * 109 + "\n", encoding="utf-8")
        config = project / "custom.conf"
        config.write_text("columns ^src/limit\\.c$ 120\n",
                          encoding="utf-8")
        result = subprocess.run([str(CSANE), "--file", "./src/limit.c",
                                 "--config", str(config)], cwd=project,
                                capture_output=True, text=True,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_column_limit_in_file_list(self):
        relative = "src/my config.h"
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        self.assertFalse(self.compare(relative, "x" * 101 + "\n",
                                      file_list=True,
                                      no_column_limit=path))

    def test_empty_no_column_limit_is_ignored(self):
        path = self.root / "src/limit.c"
        path.parent.mkdir(parents=True)
        path.write_text("x" * 81 + "\n", encoding="utf-8")
        command = [str(CSANE), "--file", str(path)]
        normal = subprocess.run(command, cwd=self.root, capture_output=True,
                                text=True, check=False)
        empty = subprocess.run([str(CSANE), "--no-column-limit", "",
                                *command[1:]], cwd=self.root,
                               capture_output=True, text=True, check=False)
        self.assertEqual(normal.returncode, 1, normal.stderr)
        self.assertIn("error[SRC-COLUMNS]", normal.stdout)
        self.assertEqual(empty.returncode, normal.returncode, empty.stderr)
        self.assertEqual(empty.stdout, normal.stdout)
        self.assertEqual(empty.stderr, normal.stderr)

    def test_root_discovery_skips_generated_headers_and_stc(self):
        source = "#ifdef BAD\n#endif\n"
        for relative in ("src/visible.c", "stc/hidden.c",
                         "src/generated.meta.h", "tests/fixtures/ignored.c"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
        result = subprocess.run([str(CSANE), "--root", str(self.root)],
                                cwd=self.root, capture_output=True,
                                text=True, check=False)
        self.assertEqual(result.returncode, 1, result.stderr)
        findings = [line for line in result.stdout.splitlines()
                    if "error[PP-DEFINED]" in line]
        self.assertEqual(len(findings), 1, result.stdout)
        self.assertIn("src/visible.c", findings[0])

    def test_piscou_config_exemption_matches_wrapper(self):
        (self.root / "piscou.1").write_text("", encoding="utf-8")
        relative = "config.h"
        path = self.root / relative
        self.assertFalse(self.compare(relative, "x" * 101 + "\n",
                                      no_column_limit=path))


if __name__ == "__main__":
    unittest.main()
