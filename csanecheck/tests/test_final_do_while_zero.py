#!/usr/bin/env python3
"""STYLE-FINAL-DO-WHILE-ZERO: no terminal one-shot loops in functions."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-FINAL-DO-WHILE-ZERO\]")


class FinalDoWhileZeroTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "terminal_do.c"
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

    def test_original_trailing_return_pattern(self):
        self.check("""void check_connection(void) {
    do {
        int32 inline_src_out = conn->src_out;
        int32 inline_dst_in = conn->dst_in;
        char *src_sys_name = "(null)";
        char *dst_sys_name = "(null)";
        result->warnings += 1;
        if (!print) {
            break;
        }
        if (src->name) {
            src_sys_name = src->name;
        }
        if (dst->sig->inputs[inline_dst_in]) {
            dst_sys_name = dst->sig->inputs[inline_dst_in];
        }
        error("Connection mismatch: %s -> %s\\n", src_sys_name,
              dst_sys_name);
    } while (0);
    return;
}
""", [(2, 5)])

    def test_last_statement_without_return(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
        update();
    } while (0);
}
""", [(2, 5)])

    def test_return_identifier_is_not_shared_work(self):
        self.check("""int32 f(void) {
    int32 status = 0;
    do {
        if (bad()) {
            status = -1;
            break;
        }
    } while (0);
    return status;
}
""", [(3, 5)])

    def test_return_literal_is_not_shared_work(self):
        self.check("""int32 f(void) {
    do {
        call();
    } while (0); // finished
    return 0;
}
""", [(2, 5)])

    def test_work_after_loop_prevents_diagnostic(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
    } while (0);
    release(resource);
    return;
}
""", [])

    def test_return_expression_with_call_is_shared_work(self):
        self.check("""int32 f(void) {
    do {
        if (bad()) {
            break;
        }
    } while (0);
    return finalize(resource);
}
""", [])

    def test_nonterminal_block_with_other_statements(self):
        self.check("""void f(void) {
    do {
        do_work();
    } while (0);
    if (failed()) {
        report();
    }
}
""", [])

    def test_variable_condition_is_not_a_single_pass_loop(self):
        self.check("""void f(void) {
    do {
        update();
    } while (again());
}
""", [])

    def test_while_false_not_a_literal_zero(self):
        self.check("""void f(void) {
    do {
        update();
    } while (false);
    return;
}
""", [])

    def test_nested_do_is_not_at_function_end(self):
        self.check("""void f(void) {
    if (ready) {
        do {
            work();
        } while (0);
    }
    return;
}
""", [])

    def test_do_inside_for_is_not_function_terminal(self):
        self.check("""void f(void) {
    for (int32 i = 0; i < 3; i += 1) {
        do {
            work(i);
        } while (0);
    }
}
""", [])

    def test_macro_definition_is_not_a_function_loop(self):
        self.check("""#define CHECK(x) do { check(x); } while (0)
void f(void) {
    CHECK(4);
}
""", [])

    def test_strings_and_comments_are_not_control_statements(self):
        self.check("""void f(void) {
    // do { break; } while (0);
    print("do { break; } while (0);");
    return;
}
""", [])

    def test_multiple_functions_only_terminal_case_reported(self):
        self.check("""void first(void) {
    do {
        work();
    } while (0);
    cleanup();
}
void second(void) {
    do {
        work();
    } while (0);
}
""", [(8, 5)])

    def test_preprocessor_after_loop_does_not_hide_code(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
#if FEATURE
    cleanup();
#endif
    return;
}
""", [])

    def test_conditional_do_is_not_treated_as_unconditional_end(self):
        self.check("""void f(void) {
#if FEATURE
    do {
        work();
    } while (0);
#endif
    cleanup();
}
""", [])

    def test_nonzero_literal_condition_is_allowed(self):
        self.check("""void f(void) {
    do {
        work();
    } while (1);
}
""", [])

    def test_rule_can_be_exempted(self):
        config = Path(self.temporary.name) / "csanecheck.conf"
        config.write_text("allow-rule STYLE-FINAL-DO-WHILE-ZERO .*\n",
                          encoding="utf-8")
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    return;
}
""", [], config=config)


if __name__ == "__main__":
    unittest.main()
