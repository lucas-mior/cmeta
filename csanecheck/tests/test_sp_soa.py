#!/usr/bin/env python3
"""STYLE-SP-SOA: use SP for matching parallel string arrays in %.*s."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-SP-SOA\]")


class SpanOfArraysTests(unittest.TestCase):
    def test_format_argument_mapping(self):
        cases = {
            "one_line": ('fmt_printf("text: %.*s", s->strings_lens[i], '
                         's->strings[i]);', 1),
            "multiline": ('fmt_printf("text: %.*s",\n'
                          '               s->strings_lens[i], s->strings[i]);', 1),
            "dot_member": ('fmt_printf("text: %.*s", s.strings_lens[i], '
                           's.strings[i]);', 1),
            "local_arrays": ('fmt_printf("text: %.*s", names_lens[i], '
                             'names[i]);', 1),
            "standalone_arrays_exact": (
                'fmt_printf("%.*s", strings_lens[i], strings[i]);', 1),
            "standalone_arrays_after_conversion": (
                'fmt_printf("n=%d: %.*s", n, strings_lens[i + 1], '
                'strings[i + 1]);', 1),
            "standalone_arrays_with_comment": (
                'fmt_printf("%.*s", strings_lens[i], '
                'strings[/* matching index */ i]);', 1),
            "standalone_arrays_index_mismatch": (
                'fmt_printf("%.*s", strings_lens[i], strings[j]);', 0),
            "standalone_arrays_name_mismatch": (
                'fmt_printf("%.*s", strings_lens[i], others[i]);', 0),
            "standalone_vs_member": (
                'fmt_printf("%.*s", s->strings_lens[i], strings[i]);', 0),
            "standalone_already_sp": (
                'fmt_printf("%.*s", SP(strings, i));', 0),
            "nested_owner": ('fmt_printf("text: %.*s", '
                             'owners[k].names_lens[i + 1], '
                             'owners[k].names[i + 1]);', 1),
            "format_after_initial_arg": (
                'str_printf(&out, "text: %.*s", s->strings_lens[i], '
                's->strings[i]);', 1),
            "preceded_by_other_conversions": (
                'fmt_printf("x=%d, label=%.*s", x, '
                's->strings_lens[i], s->strings[i]);', 1),
            "preceded_by_stars": (
                'fmt_printf("x=%*.*f, label=%.*s", width, digits, value, '
                's->strings_lens[i], s->strings[i]);', 1),
            "escaped_percent": (
                'fmt_printf("%% x=%d, %.*s", x, '
                's->strings_lens[i], s->strings[i]);', 1),
            "concatenated_literals": (
                'fmt_printf("x=%d " "label=%.*s", x, '
                's->strings_lens[i], s->strings[i]);', 1),
            "two_spans": (
                'fmt_printf("a=%.*s b=%.*s", s->strings_lens[i], '
                's->strings[i], t->names_lens[j], t->names[j]);', 2),
            "different_indices": (
                'fmt_printf("text: %.*s", s->strings_lens[i], '
                's->strings[j]);', 0),
            "different_objects": (
                'fmt_printf("text: %.*s", s->strings_lens[i], '
                't->strings[i]);', 0),
            "different_arrays": (
                'fmt_printf("text: %.*s", s->names_lens[i], '
                's->strings[i]);', 0),
            "different_owner_index": (
                'fmt_printf("text: %.*s", owners[i].names_lens[j], '
                'owners[k].names[j]);', 0),
            "different_index_expression": (
                'fmt_printf("text: %.*s", s->strings_lens[i + 1], '
                's->strings[i]);', 0),
            "not_plural": (
                'fmt_printf("text: %.*s", name_lens[i], name[i]);', 0),
            "singular_len": (
                'fmt_printf("text: %.*s", names_len[i], names[i]);', 0),
            "not_a_format": (
                'fmt_printf("text: %s", s->strings_lens[i], s->strings[i]);', 0),
            "already_sp": (
                'fmt_printf("text: %.*s", SP(s->strings, i));', 0),
            "different_conversion_arguments": (
                'fmt_printf("text: %d %.*s", s->strings_lens[i], '
                's->strings[i], len, string);', 0),
            "non_literal_format": (
                'fmt_printf(format, s->strings_lens[i], s->strings[i]);', 0),
            "unrelated_call": (
                'work("text", s->strings_lens[i], s->strings[i]);', 0),
            "comment_in_index": (
                'fmt_printf("text: %.*s", s->strings_lens[i], '
                's->strings[/*same*/ i]);', 1),
            "comment_in_prefix": (
                'fmt_printf("text: %.*s", s->strings_lens[i], '
                's /*same*/ ->strings[i]);', 1),
            "escaped_percent_only": (
                'fmt_printf("text: %%.*s", s->strings_lens[i], '
                's->strings[i]);', 0),
        }
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "src" / "soa.c"
            path.parent.mkdir()
            for name, (body, expected) in cases.items():
                with self.subTest(case=name):
                    source = "void test(void) {\n    " + body + "\n}\n"
                    path.write_text(source, encoding="utf-8")
                    result = subprocess.run(
                        [str(CHECKER), "--no-project-config", "--file", str(path)],
                        cwd=ROOT, text=True, capture_output=True,
                        check=False,
                    )
                    self.assertIn(result.returncode, (0, 1), result.stderr)
                    found = DIAGNOSTIC.findall(result.stdout)
                    self.assertEqual(len(found), expected,
                                     result.stdout + result.stderr)
                    expected_line = "3" if name == "multiline" else "2"
                    self.assertTrue(all(line == expected_line for line in found))


if __name__ == "__main__":
    unittest.main()
