#!/usr/bin/env python3
"""STYLE-DO-WHILE-ZERO-COND-BREAK: one-shot loops need guarded exits."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-DO-WHILE-ZERO-COND-BREAK\]"
)


class DoWhileZeroCondBreakTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "one_shot.c"
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

    def test_original_unconditional_break(self):
        self.check("""void f(CySystemRuntime *top) {
    do {
        CySystemRuntime *sys = top;
        BoilerModelMetadata *meta = &cy_meta_boiler_toplevel_nmpc;

        ASSERT_EQ(cy_set_id(sys, meta->id), 0);
        ASSERT_EQ(cy_set_name(sys, meta->name), 0);
        cy_set_description(sys, meta->description);
        cy_set_sim_defaults(sys, meta->simulation_defaults);
        cy_set_sim_defaults_resolver(sys, meta->simulation_defaults_resolver);
        break;
    } while (0);
    finish();
}
""", [(2, 5)])

    def test_no_break(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_unconditional_break_after_unrelated_if(self):
        self.check("""void f(void) {
    do {
        if (ready()) {
            work();
        }
        break;
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_guarded_break_in_if(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
        work();
    } while (0);
    cleanup();
}
""", [])

    def test_guarded_break_in_else(self):
        self.check("""void f(void) {
    do {
        if (ready()) {
            work();
        } else {
            break;
        }
    } while (0);
    cleanup();
}
""", [])

    def test_guarded_break_without_braces(self):
        self.check("""void f(void) {
    do {
        if (bad())
            break;
        work();
    } while (0);
    cleanup();
}
""", [])

    def test_switch_break_does_not_count(self):
        self.check("""void f(void) {
    do {
        switch (state()) {
        case 0:
            if (bad()) {
                break;
            }
        default:
            break;
        }
        work();
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_nested_for_break_does_not_count(self):
        self.check("""void f(void) {
    do {
        for (int32 i = 0; i < 10; i += 1) {
            if (bad()) {
                break;
            }
        }
        work();
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_inner_do_break_does_not_count_for_outer(self):
        self.check("""void f(void) {
    do {
        do {
            if (bad()) {
                break;
            }
        } while (0);
        work();
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_conditional_and_unconditional_break(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
        work();
        break;
    } while (0);
    cleanup();
}
""", [])

    def test_other_do_while_conditions(self):
        self.check("""void f(void) {
    do {
        break;
    } while (1);
    do {
        break;
    } while (false);
}
""", [])

    def test_macro_wrapper_ignored(self):
        self.check("""#define CALL_ONCE(x) do { x; } while (0)
#define CALL_TWICE(x) do { x; break; } while (0)
void f(void) {
    CALL_ONCE(work());
}
""", [])

    def test_comment_and_string_not_a_break(self):
        self.check("""void f(void) {
    do {
        // if (bad()) { break; }
        print("if (bad()) { break; }");
    } while (0);
    cleanup();
}
""", [(2, 5)])

    def test_two_independent_loops(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    do {
        if (bad()) {
            break;
        }
    } while (0);
    do {
        break;
    } while (0);
    cleanup();
}
""", [(2, 5), (10, 5)])

    def test_rule_exemption(self):
        config = Path(self.temporary.name) / "policy.conf"
        config.write_text(
            "allow-rule STYLE-DO-WHILE-ZERO-COND-BREAK .*\\.c$\n",
            encoding="utf-8"
        )
        self.check("""void f(void) {
    do {
        break;
    } while (0);
    cleanup();
}
""", [], config)


if __name__ == "__main__":
    unittest.main()
