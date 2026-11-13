#!/usr/bin/env python3
"""FMT-IF-COMPARATOR-WRAP: join a short final comparison to its LHS."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[FMT-IF-COMPARATOR-WRAP\]")


class IfComparatorWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "if_comparator_wrap.c"

    def check(self, source, expected, name=None):
        path = self.path if name is None else self.path.with_name(name)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(path)],
            cwd=ROOT, text=True, capture_output=True, check=False,
            timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        return result.stdout

    def test_reported_case_and_preferred_replacement(self):
        self.check('''void f(void) {
    if ((status = CY_OPTIONS_GET_FOR_GROUP(generic, "composite", name,
                                           &integer))
        < 0) {
        work();
    }
}
''', [4])
        self.check('''void f(void) {
    if ((status = CY_OPTIONS_GET_FOR_GROUP(generic, "composite", name,
                                           &integer)) < 0) {
        work();
    }
}
''', [])

    def test_all_comparators(self):
        for operator in ("==", "!=", "<", "<=", ">", ">="):
            with self.subTest(operator=operator):
                self.check(
                    "void f(void) {\n"
                    "    if ((result = get_long_named_result(first_argument, second_argument,\n"
                    "                                         third, fourth))\n"
                    f"        {operator} 0) {{\n"
                    "        work();\n"
                    "    }\n"
                    "}\n", [4],
                )

    def test_else_if_and_other_non_assignment_lhs(self):
        self.check('''void f(void) {
    if (ready) {
        work();
    } else if (calculate_long_result(first_argument, second_argument,
                                     third, fourth)
               >= 0) {
        work();
    }
    if ((a + long_expression(first_argument, second_argument,
                             third, fourth))
        != limit) {
        work();
    }
}
''', [6, 11])

    def test_only_join_if_the_last_two_lines_fit(self):
        prefix = "                                           &integer))"
        suffix = " <= 0) {"
        filler = "x" * (80 - len(prefix) - len(suffix))
        self.assertEqual(len(prefix + filler + suffix), 80)
        self.check(
            "void f(void) {\n"
            "    if ((status = a_really_long_call(first, second, third,\n"
            f"{prefix}{filler}\n"
            "        <= 0) {\n"
            "        work();\n"
            "    }\n"
            "}\n", [4],
        )
        self.check(
            "void f(void) {\n"
            "    if ((status = a_really_long_call(first, second, third,\n"
            f"{prefix}{filler}x\n"
            "        <= 0) {\n"
            "        work();\n"
            "    }\n"
            "}\n", [],
        )

    def test_whole_header_fit_is_reported_only_by_existing_rule(self):
        output = self.check('''void f(void) {
    if ((status = call())
        < 0) {
        work();
    }
}
''', [])
        self.assertIn("error[FMT-IF-WRAP]", output)

    def test_compound_nested_and_non_if_are_excluded(self):
        self.check('''void f(void) {
    if ((status = call(first, second, third, fourth))
        < 0 && ready) {
        work();
    }
    if ((status = call(first, second, third, fourth))
        < 0 || ready) {
        work();
    }
    if ((a == b) && (status = call(first, second, third,
                                   fourth))
        < 0) {
        work();
    }
    if (check(first, second, third, fourth,
              value < 0)) {
        work();
    }
    if ((status = call(first, second, third, fourth))
        ? ready : 0) {
        work();
    }
    if ((status = compare(first, second, third,
                          value > 0))
        < 0) {
        work();
    }
    while ((status = call(first, second, third, fourth))
           < 0) {
        work();
    }
    for (; (status = call(first, second, third, fourth))
           < 0;) {
        work();
    }
}
''', [])

    def test_comments_directives_trailers_and_multiline_rhs(self):
        self.check('''void f(void) {
    if ((status = long_call(first, second, third,
                            fourth)) /* keep */
        < 0) {
        work();
    }
    if ((status = long_call(first, second, third,
                            fourth))
        < 0) { // keep
        work();
    }
    if ((status = long_call(first, second, third,
                            fourth))
#if defined(ENABLED)
        < 0) {
#endif
        work();
    }
    if ((status = long_call(first, second, third,
                            fourth))
        < another_long_call(first, second, third,
                            fourth)) {
        work();
    }
    if ((status = long_call(first, second, third,
                            fourth))
        < 0)
    {
        work();
    }
}
''', [])

    def test_generated_headers_are_excluded(self):
        self.check('''void f(void) {
    if ((status = CY_OPTIONS_GET_FOR_GROUP(generic, "composite", name,
                                           &integer))
        < 0) {
        work();
    }
}
''', [], name="generated.meta.h")


if __name__ == "__main__":
    unittest.main()
