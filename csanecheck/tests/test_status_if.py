#!/usr/bin/env python3
"""Regression cases for status assignment followed by a negative check."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CSANE = ROOT / "bin/csanecheck"


class StatusIfTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def check_findings(self, source, expected):
        path = self.root / "sample.c"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CSANE), "--file", str(path)],
            cwd=self.root, text=True, capture_output=True, check=False,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        lines = [int(number) for number in re.findall(
            r":(\d+):\d+: error\[STYLE-STATUS-IF\]", result.stdout,
        )]
        self.assertEqual(lines, expected, result.stdout + result.stderr)

    def test_exact_case_and_reused_status(self):
        self.check_findings(
            "int32 example(void) {\n"
            "    int32 status;\n"
            "    status = cy_option_registry_init(&combined);\n"
            "    if (status < 0) {\n"
            "        goto cleanup;\n"
            "    }\n"
            "    status = cy_option_registry_add_registry(&combined, &other);\n"
            "    if (status < 0) {\n"
            "        goto cleanup;\n"
            "    }\n"
            "cleanup:\n"
            "    return status;\n"
            "}\n",
            [3, 7],
        )

    def test_multiline_calls_comments_and_nested_blocks(self):
        self.check_findings(
            "void example(void) {\n"
            "    int32 status;\n"
            "    {\n"
            "        status = operation(\n"
            "            first, second); // retain note\n"
            "\n"
            "        if (((status < 0))) {\n"
            "            goto cleanup;\n"
            "        }\n"
            "    }\n"
            "cleanup:\n"
            "    use(status);\n"
            "}\n",
            [4],
        )

    def test_other_variables_and_conditions_are_not_flagged(self):
        self.check_findings(
            "void example(void) {\n"
            "    int32 result;\n"
            "    result = operation();\n"
            "    if (result < 0) { return; }\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    if (status == 0) { return; }\n"
            "    status = operation();\n"
            "    if (status <= 0) { return; }\n"
            "    status = operation();\n"
            "    if ((status < 0) && retry()) { return; }\n"
            "    status = 1;\n"
            "    if (status < 0) { return; }\n"
            "}\n",
            [],
        )

    def test_intervening_actions_and_different_scopes_not_flagged(self):
        self.check_findings(
            "void example(void) {\n"
            "    int32 status;\n"
            "    status = operation();\n"
            "    side_effect();\n"
            "    if (status < 0) { return; }\n"
            "    { status = operation(); }\n"
            "    if (status < 0) { return; }\n"
            "    status = operation();\n"
            "#if defined(FEATURE)\n"
            "    if (status < 0) { return; }\n"
            "#endif\n"
            "}\n",
            [],
        )

    def test_conditional_branch_pair_and_nonstatement_usage(self):
        self.check_findings(
            "void example(void) {\n"
            "    int32 status;\n"
            "#if defined(FEATURE)\n"
            "    status = operation();\n"
            "    if (status < 0) { return; }\n"
            "#endif\n"
            "    object.status = operation();\n"
            "    if (status < 0) { return; }\n"
            "    consume(status = operation());\n"
            "    if (status < 0) { return; }\n"
            "}\n",
            [4],
        )


if __name__ == "__main__":
    unittest.main()
