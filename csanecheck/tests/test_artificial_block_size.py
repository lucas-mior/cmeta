#!/usr/bin/env python3
"""AST-ARTIFICIAL-BLOCK-LARGE: a standalone scope cannot dominate a function."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[AST-ARTIFICIAL-BLOCK-LARGE\]")


class ArtificialBlockSizeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "large_artificial_block.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    @staticmethod
    def boundary_case(extra_bytes):
        """Place a scope at 80% of the braced function body, then adjust it."""
        block = "{\n        int32 value = 1;\n        use(value);\n        use(value);\n    }"
        while len(block) % 4:
            block = block[:-1] + " " + block[-1]
        prefix = "{\n    "
        suffix = "\n}"
        padding = len(block) * 5 // 4 - len(prefix) - len(block) - len(suffix)
        padding += extra_bytes
        assert padding >= 0
        body = prefix + block + "\n" + " " * padding + "}"
        assert len(body) == len(block) * 5 // 4 + extra_bytes
        return "void f(void) " + body + "\n"

    def test_block_larger_than_eighty_percent(self):
        self.check('''void f(void) {
    {
        int32 local = 1;
        consume(local);
        consume(local);
        consume(local);
        consume(local);
        consume(local);
    }
}
''', [(2, 5)])

    def test_exactly_eighty_percent_is_allowed(self):
        self.check(self.boundary_case(0), [])

    def test_one_byte_above_eighty_percent(self):
        self.check(self.boundary_case(-1), [(2, 5)])

    def test_one_byte_below_eighty_percent(self):
        self.check(self.boundary_case(1), [])

    def test_multiline_function_signature_is_not_part_of_body_ratio(self):
        source = '''void f(
    int32 one,
    int32 two,
    int32 three,
    int32 four
) {
    {
        int32 local = one;
        consume(local);
        consume(two);
        consume(three);
        consume(four);
        consume(local);
    }
}
'''
        self.check(source, [(7, 5)])

    def test_small_scopes_and_control_bodies_are_not_reported(self):
        source = '''void f(void) {
    {
        int32 temporary = 1;
        consume(temporary);
    }
    consume_more();
    consume_more();
    consume_more();
    consume_more();
    consume_more();
    consume_more();
    consume_more();
}
void controls(void) {
    if (active) {
        int32 temporary = 1;
        consume(temporary);
        consume(temporary);
        consume(temporary);
        consume(temporary);
    } else {
        consume(0);
    }
    while (active) {
        consume(0);
    }
    for (int32 i = 0; i < 1; i += 1) {
        consume(i);
    }
    do {
        consume(0);
    } while (active);
    switch (number) {
    case 0:
        consume(0);
        break;
    default:
        break;
    }
}
'''
        self.check(source, [])

    def test_report_without_declaration_as_well(self):
        self.check('''void f(void) {
    {
        consume(0);
        consume(0);
        consume(0);
        consume(0);
        consume(0);
        consume(0);
    }
}
''', [(2, 5)])

    def test_nesting_reports_each_large_artificial_block(self):
        source = '''void f(void) {
    {
        int32 outer = 1;
        {
            int32 inner = outer;
            consume(inner);
            consume(inner);
            consume(inner);
            consume(inner);
            consume(inner);
            consume(inner);
            consume(inner);
        }
    }
}
'''
        self.check(source, [(2, 5), (4, 9)])

    def test_shared_body_after_preprocessor_is_not_artificial(self):
        source = '''void f(void) {
#if OPTION
    if (condition)
#else
    while (condition)
#endif
    {
        int32 temporary = 1;
        consume(temporary);
        consume(temporary);
        consume(temporary);
        consume(temporary);
        consume(temporary);
    }
}
'''
        self.check(source, [])


if __name__ == "__main__":
    unittest.main()
