#!/usr/bin/env python3
"""Step 7: conditional macro definitions require conservative recovery."""

from pathlib import Path
import tempfile
import unittest

from test_compatibility import run_checker, write_file


class ConditionalMacroTests(unittest.TestCase):
    def scan(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = write_file(root, "sample.c", source)
            return run_checker(["--file", path, "--coverage",
                                "--report-parse-issues"], root)

    def test_conditional_define_is_not_assumed_absent(self):
        result = self.scan(
            "#if FLAG\n#define MAKE(X) int32 v_##X;\n#endif\n"
            "MAKE(alpha)\nint32 ordinary;\n")
        self.assertIn("unsupported_externals=1", result.stdout)
        self.assertIn("malformed_issues=0", result.stdout)

    def test_conditional_undef_does_not_cancel_certain_define(self):
        result = self.scan(
            "#define MAKE(X) int32 v_##X;\n"
            "#if FLAG\n#undef MAKE\n#endif\n"
            "MAKE(alpha)\nint32 ordinary;\n")
        self.assertIn("unsupported_externals=1", result.stdout)

    def test_unconditional_undef_clears_conditional_definition(self):
        result = self.scan(
            "#if FLAG\n#define MAKE(X) int32 v_##X;\n#endif\n"
            "#undef MAKE\nMAKE(alpha)\nint32 ordinary;\n")
        self.assertIn("malformed_issues=1", result.stdout)


if __name__ == "__main__":
    unittest.main()
