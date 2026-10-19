#!/usr/bin/env python3
"""Policy parity regressions for file-scope wrapping."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import rule_counts, run_checker, write_file


class CsanecheckStep6Tests(unittest.TestCase):
    def check_calls(self, source, count):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "src/sample.c", source)
            result = run_checker(["--file", path], root)
            self.assertEqual(rule_counts(result)["FMT-CALL-WRAP"], count,
                             result.stdout + result.stderr)

    def test_file_scope_call_initializer_wrap(self):
        self.check_calls(
            "static int value = compute(\n"
            "    argument);\n", 1)

    def test_file_scope_call_initializer_fits(self):
        self.check_calls("static int value = compute(argument);\n", 0)

    def test_file_scope_call_initializer_long(self):
        self.check_calls(
            "static int value = enormously_long_function_name_with_many_"
            "extra_characters(\n"
            "    extremely_long_argument_name_with_many_extra_characters);\n",
            0)

    def test_file_scope_call_initializer_macro_continuation(self):
        self.check_calls(
            "#define INITIAL_VALUE compute( \\\n"
            "    arg)\n", 0)

    def test_file_scope_call_initializer_with_braces(self):
        self.check_calls(
            "static int values[] = {compute(\n"
            "    arg)};\n", 0)


if __name__ == "__main__":
    unittest.main()
