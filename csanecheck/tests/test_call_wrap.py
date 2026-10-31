#!/usr/bin/env python3
"""Regression tests for neighboring-call grouping in FMT-CALL-WRAP."""

from collections import Counter
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"

RELATED_CASES = {
    "same_function": (
        "void f(void) {\n    work(a,\n         b);\n"
        "    work(this_argument_makes_the_call_long,\n"
        "         a_second_argument_that_cannot_fit_on_the_first_line);\n}\n",
        ()),
    "same_first_argument": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(context, argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
    "different_first_argument": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(other, argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        (2,)),
    "nested_same_first_argument": (
        "void f(void) {\n    first(get(a, b),\n          b);\n"
        "    second(get(a, b), argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
    "nested_different_first_argument": (
        "void f(void) {\n    first(get(a, b),\n          b);\n"
        "    second(get(a, c), argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        (2,)),
    "assert_different_inner_calls": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(&registry, &args.options,\n"
        "                                    &configured, &generic));\n"
        "    ASSERT(!cy_fit_resolve_options(fit, &registry, &generic,\n"
        "                                   &resolved));\n}\n",
        (4,)),
    "assert_same_inner_function": (
        "void f(void) {\n"
        "    ASSERT(!cy_fit_resolve_options(fit,\n"
        "                                   &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(other, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        ()),
    "assert_same_inner_first_argument": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(fit,\n"
        "                                    &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(fit, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        ()),
    "assert_different_inner_first_argument": (
        "void f(void) {\n"
        "    ASSERT(!cy_args_resolve_options(fit,\n"
        "                                    &registry));\n"
        "    ASSERT(!cy_fit_resolve_options(other, &registry,\n"
        "                                   argument_that_is_much_too_long));\n}\n",
        (2,)),
    "related_chain": (
        "void f(void) {\n    first(context,\n          b);\n"
        "    second(context, b);\n"
        "    second(argument_that_makes_the_call_too_long,\n"
        "           and_this_extra_argument_exceeds_eighty_columns);\n}\n",
        ()),
}


class CallWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_related_neighbor_exception(self):
        for name, (source, lines) in RELATED_CASES.items():
            with self.subTest(case=name):
                path = self.root / "src" / f"related_{name}.c"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
                result = subprocess.run(
                    [str(CSANE), "--file", str(path)], cwd=self.root,
                    text=True, capture_output=True, check=False,
                )
                self.assertIn(result.returncode, (0, 1), result.stderr)
                found = Counter(int(line) for line in re.findall(
                    r":(\d+):\d+: error\[FMT-CALL-WRAP\]", result.stdout))
                self.assertEqual(found, Counter(lines), result.stdout)


if __name__ == "__main__":
    unittest.main()
