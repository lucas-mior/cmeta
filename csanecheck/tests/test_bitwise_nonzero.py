#!/usr/bin/env python3
"""Detect redundant != 0 on parenthesized bitwise flag tests."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-BITWISE-NONZERO\]")


class BitwiseNonzeroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "bitwise_nonzero.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path),
             *extra],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=15,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        lines = source.splitlines()
        offsets = {}
        expected = []
        for line in expected_lines:
            offset = lines[line - 1].index("!=", offsets.get(line, 0))
            expected.append((line, offset + 1))
            offsets[line] = offset + 2
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_if_assignment_return_and_reversed(self):
        self.check("""void f(void) {
    if ((enum_value & ENUM_BITWISE_FLAG) != 0) { use(); }
    use_discrete_tree =
        (resolved.capabilities.required_model & CY_SIM_MODEL_DISCRETE) != 0;
    use_pre_output_tree =
        (resolved.capabilities.required_model & CY_SIM_MODEL_PRE_OUTPUT) != 0;
    bool present = ((flags & CY_FLAG)) != ((0));
    if (0 != (flags & CY_FLAG)) { use(); }
    return (0 != ((get_flags() & (FLAG_A | FLAG_B))));
}
""", [2, 4, 6, 7, 8, 9])

    def test_other_boolean_contexts_and_macros(self):
        self.check("""#define HAS_FLAG(f) (((f) & FLAG_A) != 0)
void f(void) {
    while ((flags & FLAG_A) != 0) { use(); }
    if (ok && ((flags & FLAG_B) != 0)) { use(); }
    bool enabled = ((flags & FLAG_A) != 0) || ready;
    if ((flags & FLAG_A & FLAG_B) != 0) { use(); }
    if ((flags & (FLAG_A | FLAG_B)) != 0) { use(); }
    if ((flags /*a*/ & /*b*/ FLAG_A) != (0)) { use(); }
}
""", [1, 3, 4, 5, 6, 7, 8])

    def test_no_false_positives(self):
        self.check("""void f(void) {
    if ((value & MASK) == 0) { use(); }
    if (status != 0) { use(); }
    if (flags != 0) { use(); }
    if ((value && ready) != 0) { use(); }
    if ((flags | MASK) != 0) { use(); }
    if ((flags ^ MASK) != 0) { use(); }
    if ((flags & MASK) != 1) { use(); }
    if (((flags & MASK) + 1) != 0) { use(); }
    if (1 + (flags & MASK) != 0) { use(); }
    if (0 != (flags & MASK) + 1) { use(); }
    if (0 != ((flags & MASK) * 2)) { use(); }
    if (flags & MASK != 0) { use(); }
    if (stat(path, &st) != 0) { use(); }
    if (0 != stat(path, &st)) { use(); }
    if ((&value) != 0) { use(); }
    if (0 != (value &)) { use(); }
    if ((&value & MASK) != 0) { use(); }
    // if ((flags & MASK) != 0) { use(); }
    char *example = "(flags & MASK) != 0";
}
""", [])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-BITWISE-NONZERO .*\\.c$\n",
                          encoding="utf-8")
        source = "void f(void) { if ((flags & MASK) != 0) { use(); } }\n"
        self.check(source, [1])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
