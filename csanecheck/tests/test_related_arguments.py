#!/usr/bin/env python3
"""Standalone regressions for FMT-RELATED-ARGS naming conventions."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


CSANE = Path(__file__).resolve().parents[2] / "bin" / "csanecheck"


class RelatedArgumentsTests(unittest.TestCase):
    def test_related_arguments(self):
        cases = {
            "n_prefix_shared_first_line": (
                "    work(prefix, properties,\n"
                "         nproperties);\n", [3]),
            "n_prefix_shared_second_line": (
                "    work(prefix,\n"
                "         properties,\n"
                "         nproperties, tail);\n", [4]),
            "address_of_shared_first_line": (
                "    work(prefix, &properties,\n"
                "         &nproperties);\n", [3]),
            "address_of_shared_second_line": (
                "    work(prefix,\n"
                "         &properties,\n"
                "         &nproperties, tail);\n", [4]),
            "mixed_address_of": (
                "    work(prefix, properties,\n"
                "         &nproperties);\n", [3]),
            "n_prefix_same_line": (
                "    work(prefix, properties, nproperties);\n", []),
            "address_of_same_line": (
                "    work(prefix, &properties, &nproperties);\n", []),
            "n_prefix_one_argument_per_line": (
                "    work(prefix,\n"
                "         properties,\n"
                "         nproperties);\n", []),
            "address_of_one_argument_per_line": (
                "    work(prefix,\n"
                "         &properties,\n"
                "         &nproperties);\n", []),
            "two_arguments": (
                "    work(properties,\n"
                "         nproperties);\n", []),
            "cystemas_example": (
                "    status = cy_fit_declaration_property_schema(kind, &prefix,\n"
                "                                                &properties,\n"
                "                                                &nproperties);\n",
                []),
            "existing_len_pair": (
                "    work(prefix, properties,\n"
                "         properties_len);\n", [3]),
            "existing_len_pair_with_address_of": (
                "    work(prefix, &properties,\n"
                "         &properties_len);\n", [3]),
            "existing_xy_pair": (
                "    work(prefix, axis_x,\n"
                "         axis_y);\n", [3]),
            "existing_width_height_pair": (
                "    work(prefix, box_width,\n"
                "         box_height);\n", [3]),
            "length_shared_first_line": (
                "    work(prefix, items,\n"
                "         LENGTH(items));\n", [3]),
            "length_shared_second_line": (
                "    work(prefix,\n"
                "         items,\n"
                "         LENGTH(items), tail);\n", [4]),
            "length_same_line": (
                "    work(prefix, items, LENGTH(items));\n", []),
            "length_separate_lines": (
                "    work(prefix,\n"
                "         items,\n"
                "         LENGTH(items));\n", []),
            "length_two_arguments": (
                "    work(items,\n"
                "         LENGTH(items));\n", []),
            "length_member_access": (
                "    work(prefix, holder->items,\n"
                "         LENGTH(holder->items));\n", [3]),
            "length_indexed_array": (
                "    work(prefix, matrix[i],\n"
                "         LENGTH(matrix[i]));\n", [3]),
            "length_parenthesized_operand": (
                "    work(prefix, items,\n"
                "         LENGTH((items)));\n", [3]),
            "length_with_comment": (
                "    work(prefix, items,\n"
                "         LENGTH(/* count */ items));\n", [3]),
            "length_different_array": (
                "    work(prefix, items,\n"
                "         LENGTH(other_items));\n", []),
            "length_has_extra_argument": (
                "    work(prefix, items,\n"
                "         LENGTH(items, extra));\n", []),
            "length_has_extra_operation": (
                "    work(prefix, items,\n"
                "         LENGTH(items) + 1);\n", []),
            "length_nested_in_other_call": (
                "    work(prefix, items,\n"
                "         consume(LENGTH(items)));\n", []),
            "length_nested_array_name_differs": (
                "    work(prefix, holder->items,\n"
                "         LENGTH(other->items));\n", []),
            "length_not_first_argument": (
                "    work(prefix, &items,\n"
                "         LENGTH(items));\n", []),
            "unrelated_names": (
                "    work(prefix, properties,\n"
                "         notproperties, tail);\n", []),
            "unrelated_address_of_names": (
                "    work(prefix, &properties,\n"
                "         &nother, tail);\n", []),
            "array_element_is_not_plain_variable": (
                "    work(prefix, &properties[0],\n"
                "         &nproperties, tail);\n", []),
        }

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "src" / "related_args.c"
            path.parent.mkdir()
            for name, (body, expected_lines) in cases.items():
                with self.subTest(case=name):
                    path.write_text("void f(void) {\n" + body + "}\n",
                                    encoding="utf-8")
                    result = subprocess.run(
                        [str(CSANE), "--no-project-config", "--file", str(path)],
                        cwd=tmp, text=True, capture_output=True,
                        check=False,
                    )
                    self.assertIn(result.returncode, (0, 1), result.stderr)
                    found = [int(line) for line in re.findall(
                        r":(\d+):\d+: error\[FMT-RELATED-ARGS\]",
                        result.stdout,
                    )]
                    self.assertEqual(found, expected_lines,
                                     result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
