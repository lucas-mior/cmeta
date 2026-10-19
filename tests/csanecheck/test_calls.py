#!/usr/bin/env python3
"""Phase 6.2 syntax-aware call/declaration layout regression tests."""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[1:] = []
RULE = re.compile(r"error\[((?!FMT-CALL-WRAP|FMT-ASSIGN-WRAP)[^]]+)\]")


class CsaneCallLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def check(self, source, expected=(), *, name="src/sample.c", flags=()):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run([str(EXECUTABLE), "--file", str(path),
                                 *flags], cwd=self.root, capture_output=True,
                                text=True, check=False, timeout=30)
        self.assertEqual(result.stderr, "", result.stderr)
        # Phase 6.3 wraps have separate group-sensitive regression tests.
        found = sorted(RULE.findall(result.stdout))
        self.assertEqual(found, sorted(expected), result.stdout)
        self.assertEqual(result.returncode,
                         int("error[" in result.stdout), result.stdout)
        return result

    def test_call_in_alternative_preprocessor_branch(self):
        self.check('#if FIRST\n'
                   'void unfinished(void) {\n'
                   '#else\n'
                   'void good(void) {\n'
                   '    run(\n'
                   '        value);\n'
                   '}\n'
                   '#endif\n', ["FMT-CALL-FIRST"])

    def test_contextual_call_in_conditional_branch(self):
        self.check('#if FIRST\n'
                   'void unfinished(void) {\n'
                   '#else\n'
                   'void good(void) {\n'
                   '    fmt_printf("%d %d", one,\n'
                   '               two);\n'
                   '}\n'
                   '#endif\n', ["FMT-FORMAT-LINE"])

    def test_macro_lowercase_parameter_suppresses_context(self):
        source = ('#define REPORT(value) fmt_printf("%d %d", value, 2, '
                  + chr(92) + '\n'
                  + '                                    3)\n')
        self.check(source)

    def test_macro_literal_only_context_is_safe(self):
        self.check('#define REPORT() fmt_printf("%d %d", 1, 2)\n')

    def test_macro_calls_do_not_trigger_contextual_false_positives(self):
        self.check('#define LOG(x, y) fmt_printf("%d %d", x, \
'
                   '                              y)\n')

    def test_contextual_rules_after_unrecognized_declaration(self):
        self.check("unrecognized_attribute int opaque(void);\n"
                   "void f(void) {\n"
                   "    fmt_printf(\"%d %d\", one,\n"
                   "               two);\n"
                   "    emit(first, x,\n"
                   "         y, other);\n"
                   "}\n",
                   ["FMT-FORMAT-LINE", "FMT-RELATED-ARGS"])

    def test_contextual_nested_argument_commas(self):
        self.check("void f(void) {\n"
                   "    fmt_printf(\"%d %d\", nested(a, b),\n"
                   "               last);\n"
                   "}\n", ["FMT-FORMAT-LINE"])

    def test_basic_call_after_unrecognized_declaration(self):
        self.check("unrecognized_attribute int opaque(void);\n"
                   "void f(void) {\n"
                   "    run(\n"
                   "        value);\n"
                   "}\n", ["FMT-CALL-FIRST"])

    def test_macro_fragments_do_not_create_literal_alignment(self):
        self.check("#define RED(x) x\n"
                   "void f(void) {\n"
                   "    run(\"left\" RED(\"mid\")\n"
                   "        \"right\");\n"
                   "}\n")

    def test_call_first(self):
        self.check("void f(void) {\n"
                   "    run(\n"
                   "        a);\n"
                   "}\n", ["FMT-CALL-FIRST"])

    def test_call_first_nested_and_initializer(self):
        self.check("void f(void) {\n"
                   "    int result = outer(inner(\n"
                   "        a));\n"
                   "}\n", ["FMT-CALL-FIRST"])

    def test_call_first_comment_is_not_whitespace(self):
        self.check("void f(void) {\n"
                   "    run(/* meaningful */\n"
                   "        value);\n"
                   "}\n")

    def test_header_is_not_call(self):
        self.check("int\n"
                   "function(int one,\n"
                   "         int two);\n")

    def test_function_header_misalignment(self):
        self.check("int\n"
                   "function(int one,\n"
                   "          int two) {\n"
                   "    return one;\n"
                   "}\n", ["FMT-FUNC-ALIGN"])

    def test_function_header_alignment(self):
        self.check("int\n"
                   "function(int one,\n"
                   "         int two) {\n"
                   "    return one;\n"
                   "}\n")

    def test_header_multiple_parameters_on_continuation_line(self):
        self.check("int\n"
                   "function(int first, int second,\n"
                   "         int third, int fourth);\n")

    def test_header_after_unrecognized_declaration(self):
        self.check("bool accepted = true;\n"
                   "int\n"
                   "function(int first,\n"
                   "          int second);\n", ["FMT-FUNC-ALIGN"])

    def test_function_pointer_is_not_header(self):
        self.check("int (*callback)(int one,\n"
                   "      int two);\n")

    def test_call_argument_misalignment(self):
        self.check("void f(void) {\n"
                   "    run(first,\n"
                   "         second);\n"
                   "}\n", ["FMT-ARG-ALIGN"])

    def test_call_argument_alignment(self):
        self.check("void f(void) {\n"
                   "    run(first,\n"
                   "        second);\n"
                   "}\n")

    def test_wrapped_expression_followed_on_same_line(self):
        self.check("void f(void) {\n"
                   "    run(nested(first,\n"
                   "               second) + 1, next);\n"
                   "}\n")

    def test_nested_multiline_argument_followed_by_same_line(self):
        self.check("void f(void) {\n"
                   "    run(nested(first,\n"
                   "               second), last);\n"
                   "}\n")

    def test_literal_alignment(self):
        self.check("void f(void) {\n"
                   "    run(\"first\"\n"
                   "         \"second\");\n"
                   "}\n", ["FMT-LITERAL-ALIGN"])

    def test_literal_correct_alignment(self):
        self.check("void f(void) {\n"
                   "    run(\"first\"\n"
                   "        \"second\");\n"
                   "}\n")

    def test_literal_with_comment_is_not_adjacent(self):
        self.check("void f(void) {\n"
                   "    run(\"first\" /* comment */\n"
                   "          \"second\");\n"
                   "}\n")

    def test_format_grouping(self):
        self.check("void f(void) {\n"
                   "    fmt_printf(\"x=%d y=%d\", x,\n"
                   "               y, other);\n"
                   "}\n", ["FMT-FORMAT-LINE", "FMT-RELATED-ARGS"])

    def test_format_grouping_entirely_on_next_line(self):
        self.check("void f(void) {\n"
                   "    fmt_printf(\"x=%d y=%d\",\n"
                   "               x, y);\n"
                   "}\n")

    def test_format_argument_is_alignment_anchor(self):
        self.check("void f(void) {\n"
                   "    fmt_printf(&str, \"x=%d\",\n"
                   "                     x);\n"
                   "}\n")

    def test_split_format_literal_uses_last_line(self):
        self.check("void f(void) {\n"
                   "    fmt_printf(&str, \"x=%d\"\n"
                   "                     \" y=%d\",\n"
                   "                     x, y);\n"
                   "}\n")

    def test_format_percent_in_character_is_not_an_anchor(self):
        self.check("void f(void) {\n"
                   "    run('%', text,\n"
                   "        other);\n"
                   "}\n")

    def test_related_args(self):
        self.check("void f(void) {\n"
                   "    run(x,\n"
                   "        y, other);\n"
                   "}\n", ["FMT-RELATED-ARGS"])

    def test_related_args_independent_lines(self):
        self.check("void f(void) {\n"
                   "    run(x,\n"
                   "        y,\n"
                   "        other);\n"
                   "}\n")

    def test_related_args_width_height_and_len(self):
        self.check("void f(void) {\n"
                   "    run(width,\n"
                   "        height, other);\n"
                   "}\n", ["FMT-RELATED-ARGS"])
        self.check("void f(void) {\n"
                   "    run(buffer,\n"
                   "        buffer_len, other);\n"
                   "}\n", ["FMT-RELATED-ARGS"])

    def test_macro_and_comment_ignored(self):
        self.check("#define CALL(x) run(\\\n"
                   "    x)\n"
                   "// run(\n"
                   "//   x);\n"
                   "void f(void) { run(first, second); }\n")

    def test_generated_header_exemption(self):
        self.check("void f(void) {\n"
                   "    run(\n"
                   "        a);\n"
                   "}\n", name="src/generated.meta.h")

    def test_configuration_exemption(self):
        policy = self.root / "policy.conf"
        policy.write_text("allow-rule FMT-CALL-FIRST .*sample\\.c\n",
                          encoding="utf-8")
        self.check("void f(void) {\n"
                   "    run(\n"
                   "        a);\n"
                   "}\n", flags=("--config", str(policy)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
