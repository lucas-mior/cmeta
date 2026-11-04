#!/usr/bin/env python3
"""FMT-IF-WRAP: single-comparison if headers should fit on one line."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"


class IfWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "if_wrap.c"

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(self.path)],
            cwd=ROOT, text=True, capture_output=True, check=False,
            timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line in re.findall(
            r":(\d+):\d+: error\[FMT-IF-WRAP\]", result.stdout,
        )]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_if_and_else_if_with_parenthesized_flags(self):
        self.check("""void f(int32 fields) {
    if (fields
        == (CY_SIM_TIME_FIELD_T1 | CY_SIM_TIME_FIELD_NSTEPS)) {
        work();
    } else if (fields
               == (CY_SIM_TIME_FIELD_T1 | CY_SIM_TIME_FIELD_NSTEPS)) {
        work();
    } else if (fields == CY_SIM_TIME_FIELD_T1) {
        work();
    }
}
""", [2, 5])

    def test_each_comparator_and_outer_parentheses(self):
        operators = ("==", "!=", "<", "<=", ">", ">=")
        for operator in operators:
            with self.subTest(operator=operator):
                self.check(
                    "void f(void) {\n"
                    f"    if (((value)\n        {operator} (limit))) {{\n"
                    "        work();\n"
                    "    }\n"
                    "}\n", [2],
                )

    def test_exact_column_limit_and_too_long(self):
        prefix = "    if ("
        suffix = " == target) {"
        name = "v" * (80 - len(prefix) - len(suffix))
        self.assertEqual(len(prefix + name + suffix), 80)
        self.check(
            "void f(void) {\n"
            f"    if ({name}\n        == target) {{\n"
            "        work();\n"
            "    }\n"
            "}\n", [2],
        )
        self.check(
            "void f(void) {\n"
            f"    if ({name}x\n        == target) {{\n"
            "        work();\n"
            "    }\n"
            "}\n", [],
        )

    def test_else_if_prefix_counts_toward_limit(self):
        prefix = "    } else if ("
        suffix = " == target) {"
        name = "v" * (80 - len(prefix) - len(suffix))
        self.check(
            "void f(void) {\n"
            "    if (ready) {\n"
            "        work();\n"
            f"    }} else if ({name}\n               == target) {{\n"
            "        work();\n"
            "    }\n"
            "}\n", [4],
        )
        self.check(
            "void f(void) {\n"
            "    if (ready) {\n"
            "        work();\n"
            f"    }} else if ({name}x\n               == target) {{\n"
            "        work();\n"
            "    }\n"
            "}\n", [],
        )

    def test_only_a_single_top_level_comparison(self):
        self.check("""void f(void) {
    if (a
        && b) {
        work();
    }
    if ((a == b)
        && (c == d)) {
        work();
    }
    if ((a == b)
        && enabled) {
        work();
    }
    if (a == (b > c)
        ? x : y) {
        work();
    }
    if (compare(a
                > b)) {
        work();
    }
    if ((a == b)
        == enabled) {
        work();
    }
    while (a
           == b) {
        work();
    }
}
""", [])

    def test_nested_operands_and_safe_breaks(self):
        self.check("""void f(void) {
    if (lookup(a, b)
        == expected) {
        work();
    }
    if ((a + b)
        >= limit) {
        work();
    }
    if (a /* reason */
        == b) {
        work();
    }
    if (a
        == b) { // trailing explanation
        work();
    }
    if (a
        == b)
    {
        work();
    }
}
""", [2, 6])

    def test_preprocessor_and_generated_code_are_excluded(self):
        self.check("""void f(void) {
    if (a
#if defined(FLAG)
        == b
#endif
    ) {
        work();
    }
}
""", [])
        generated = Path(self.temp.name) / "generated.meta.h"
        generated.write_text("""void f(void) {
    if (a
        == b) {
        work();
    }
}
""", encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--file", str(generated)],
            cwd=ROOT, text=True, capture_output=True, check=False,
            timeout=5,
        )
        self.assertNotIn("FMT-IF-WRAP", result.stdout)


if __name__ == "__main__":
    unittest.main()
