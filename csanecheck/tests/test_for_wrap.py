#!/usr/bin/env python3
"""FMT-FOR-WRAP: do not split a for header that fits on one line."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[FMT-FOR-WRAP\]")


class ForWrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "for_wrap.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, config=None):
        self.path.write_text(source, encoding="utf-8")
        command = [str(CHECKER), "--no-project-config"]
        if config is not None:
            command.extend(["--config", str(config)])
        command.extend(["--file", str(self.path)])
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False, timeout=10)
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_reported_prediction_horizon_example(self):
        self.check("""void f(void) {
    for (int32 i = 0; i < nmpc->prediction_horizon_steps;
         i += 1) {
        run_step(i);
    }
}
""", [(2, 5)])

    def test_already_flat_and_necessary_wrap(self):
        self.check("""void f(void) {
    for (int32 i = 0; i < nmpc->prediction_horizon_steps; i += 1) {
        work(i);
    }
    for (int32 i = 0; i < some_very_long_identifier_name_longer_than_limit;
         i += 1) {
        work(i);
    }
}
""", [])

    def test_original_sixteen_space_indentation(self):
        self.check("""void f(void) {
    if (a) {
        if (b) {
            if (c) {
                for (int32 i = 0; i < nmpc->prediction_horizon_steps;
                     i += 1) {
                    work(i);
                }
            }
        }
    }
}
""", [(5, 17)])

    def test_first_and_second_clause_wrap_and_nested_expression(self):
        self.check("""void f(void) {
    for (int32 i = 0;
         i < size; i += 1) {
        work(i);
    }
    for (int32 i = 0; check(i, count)
         && active(i); i += 1) {
        work(i);
    }
    for (int32 i = 0; i < size;
         i += step(i)) {
        work(i);
    }
}
""", [(2, 5), (6, 5), (10, 5)])

    def test_exactly_eighty_columns_including_indentation_and_brace(self):
        prefix = "    for (int32 i = 0; i < "
        suffix = "; i += 1) {"
        name = "n" * (80 - len(prefix) - len(suffix))
        self.assertEqual(len(prefix + name + suffix), 80)
        self.check(
            "void f(void) {\n"
            f"    for (int32 i = 0; i < {name};\n"
            "         i += 1) {\n"
            "        work(i);\n"
            "    }\n"
            "}\n", [(2, 5)],
        )
        self.check(
            "void f(void) {\n"
            f"    for (int32 i = 0; i < {name}x;\n"
            "         i += 1) {\n"
            "        work(i);\n"
            "    }\n"
            "}\n", [],
        )

    def test_nested_indentation_counts(self):
        prefix = "        for (int32 i = 0; i < "
        suffix = "; i += 1) {"
        name = "x" * (80 - len(prefix) - len(suffix))
        self.check(
            "void f(void) {\n"
            "    if (ready) {\n"
            f"        for (int32 i = 0; i < {name};\n"
            "             i += 1) {\n"
            "            work(i);\n"
            "        }\n"
            "    }\n"
            "}\n", [(3, 9)],
        )
        self.check(
            "void f(void) {\n"
            "    if (ready) {\n"
            f"        for (int32 i = 0; i < {name}x;\n"
            "             i += 1) {\n"
            "            work(i);\n"
            "        }\n"
            "    }\n"
            "}\n", [],
        )

    def test_comments_keep_the_break(self):
        self.check("""void f(void) {
    for (int32 i = 0; /* explanation */
         i < limit; i += 1) {
        work(i);
    }
    for (int32 j = 0; j < limit; // explanation
         j += 1) {
        work(j);
    }
    for (int32 k = 0; k < limit;
         k += 1) { // explanation
        work(k);
    }
}
""", [])

    def test_directive_and_macro_continuations_are_not_joined(self):
        self.check("""#define EACH_STEP(n) \\
    for (int32 i = 0; i < (n); \\
         i += 1)
void f(void) {
#if SELECT_PATH
    for (int32 i = 0; i < limit;
#else
    for (int32 i = 0; i < other_limit;
#endif
         i += 1) {
        work(i);
    }
}
""", [])

    def test_no_brace_and_empty_loop(self):
        self.check("""void f(void) {
    for (int32 i = 0; i < size;
         i += 1)
        work(i);
    for (int32 i = 0; i < size;
         i += 1);
}
""", [(2, 5)])

    def test_multiple_loops_each_report_once(self):
        self.check("""void f(void) {
    for (int32 i = 0; i < limit;
         i += 1) {
        for (int32 j = 0; j < limit;
             j += 1) {
            work(i, j);
        }
    }
}
""", [(2, 5), (4, 9)])

    def test_policy_rule_exemption(self):
        config = Path(self.temporary.name) / "policy.conf"
        config.write_text("allow-rule FMT-FOR-WRAP .*\n", encoding="utf-8")
        self.check("""void f(void) {
    for (int32 i = 0; i < limit;
         i += 1) {
        work(i);
    }
}
""", [], config)

    def test_literals_and_parenthesized_expressions(self):
        self.check('''void f(void) {
    for (int32 i = 0; i < (limit + 1);
         i += 1) {
        work(i);
    }
    for (int32 i = 0; i < count("\\n");
         i += 1) {
        work(i);
    }
}
''', [(2, 5), (6, 5)])


if __name__ == "__main__":
    unittest.main()
