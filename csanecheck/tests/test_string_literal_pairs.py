#!/usr/bin/env python3
"""Check formatting for adjacent string-literal call arguments."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


CSANE = Path(__file__).resolve().parents[2] / "bin" / "csanecheck"

CASES = {
    "guide_bad": (
        '    if ((status = cy_example_options_set_double(configured, "fit",\n'
        '                                                "lambda_up",\n'
        '                                                options->lm.lambda_up))\n'
        '            < 0) {\n'
        '        goto cleanup;\n'
        '    }\n', "lambda_up", True),
    "guide_good": (
        '    if ((status = cy_example_options_set_double(configured,\n'
        '                                                "fit", "lambda_up",\n'
        '                                                options->lm.lambda_up))\n'
        '            < 0) {\n'
        '        goto cleanup;\n'
        '    }\n', "lambda_up", False),
    "both_on_same_line": (
        '    work(prefix,\n'
        '         "alpha", "beta",\n'
        '         value);\n', "beta", False),
    "both_alone_on_different_lines": (
        '    work(prefix,\n'
        '         "alpha",\n'
        '         "beta",\n'
        '         value);\n', "beta", False),
    "first_shares_with_preceding": (
        '    work(prefix, "alpha",\n'
        '         "beta");\n', "beta", True),
    "second_shares_with_following": (
        '    work(prefix,\n'
        '         "alpha",\n'
        '         "beta", value);\n', "beta", True),
    "second_shares_at_start": (
        '    work("alpha",\n'
        '         "beta", value);\n', "beta", True),
    "first_shares_at_end": (
        '    work(prefix, value, "alpha",\n'
        '         "beta");\n', "beta", True),
    "last_line_shares_with_preceding": (
        '    work(prefix,\n'
        '         "alpha", value, "beta");\n', "beta", False),
    "literal_pair_not_adjacent": (
        '    work("alpha", value,\n'
        '         "beta", suffix);\n', "beta", False),
    "only_two_literals": (
        '    work("alpha",\n'
        '         "beta");\n', "beta", False),
    "three_literals": (
        '    work(prefix, "alpha",\n'
        '         "beta", "gamma", value);\n', "beta", False),
    "three_literals_spaced": (
        '    work("alpha", "gamma",\n'
        '         value, "beta");\n', "beta", False),
    "char_literal_not_a_string": (
        '    work(prefix, \'a\',\n'
        '         "beta", value);\n', "beta", False),
    "string_expression_not_plain_literal": (
        '    work(prefix, (char *)"alpha",\n'
        '         "beta", value);\n', "beta", False),
    "nested_call": (
        '    work(prefix, nested(value, "alpha",\n'
        '                        "beta", suffix));\n', "beta", True),
    "nested_strings_not_arguments": (
        '    work(prefix, nested("alpha",\n'
        '                        "beta"), value);\n', "beta", False),
    "prefixed_literals": (
        '    work(prefix, L"alpha",\n'
        '         L"beta", value);\n', "beta", True),
    "first_line_contains_previous_argument_end": (
        '    work(nested(prefix,\n'
        '                suffix), "alpha",\n'
        '         "beta", value);\n', "beta", True),
}


class StringLiteralPairTests(unittest.TestCase):
    def test_string_literal_pair_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "src" / "string_pairs.c"
            path.parent.mkdir()
            for name, (body, second, bad) in CASES.items():
                with self.subTest(case=name):
                    source = "void f(void) {\n" + body + "}\n"
                    path.write_text(source, encoding="utf-8")
                    result = subprocess.run(
                        [str(CSANE), "--file", str(path)],
                        cwd=tmp, text=True, capture_output=True,
                        check=False,
                    )
                    self.assertIn(result.returncode, (0, 1), result.stderr)
                    found = [int(line) for line in re.findall(
                        r":(\d+):\d+: error\[FMT-STRING-PAIR\]",
                        result.stdout,
                    )]
                    expected = []
                    if bad:
                        expected = [source[:source.index('"' + second + '"')]
                                    .count("\n") + 1]
                    self.assertEqual(found, expected,
                                     result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
