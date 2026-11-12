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

    def test_final_fixed_member_assignment_and_return(self):
        self.check("""void boiler_nmpc_job_commit(BoilerNmpcJob *job) {
    do {
        CySimulationResult prediction = {0};
        if (!cy_plot_data_has_callback()) {
            break;
        }
        if (cy_simulate(&prediction) < 0) {
            cy_simulation_result_clear(&prediction);
            break;
        }
        cy_plot_data_emit(&prediction);
        cy_simulation_result_clear(&prediction);
        break;
    } while (0);
    job->committed = true;
    return;
}
""", [(2, 5)])

    def test_final_fixed_nested_member_assignment(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
        work();
    } while (0);
    job->state.committed = false;
    return;
}
""", [(2, 5)])

    def test_final_zero_member_assignment(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    job->committed = 0;
    return;
}
""", [(2, 5)])

    def test_computed_member_assignment_keeps_loop(self):
        self.check("""void f(void) {
    do {
        if (bad()) {
            break;
        }
        status = compute();
    } while (0);
    job->committed = status;
    return;
}
""", [])

    def test_call_assignment_keeps_loop(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    job->committed = commit_result();
    return;
}
""", [])

    def test_cleanup_before_final_fixed_set_keeps_loop(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    release(resource);
    job->committed = true;
    return;
}
""", [])

    def test_work_after_final_fixed_set_keeps_loop(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    job->committed = true;
    notify();
    return;
}
""", [])

    def test_final_fixed_set_then_side_effecting_return_keeps_loop(self):
        self.check("""int32 f(void) {
    do {
        work();
    } while (0);
    job->committed = true;
    return finalize(job);
}
""", [])

    def test_local_fixed_assignment_is_not_a_member_flag(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    committed = true;
    return;
}
""", [])

    def test_final_member_set_without_return_keeps_loop(self):
        self.check("""void f(void) {
    do {
        work();
    } while (0);
    job->committed = true;
    cleanup();
}
""", [])

    def test_nonliteral_condition_keeps_loop_with_fixed_set(self):
        self.check("""void f(void) {
    do {
        work();
    } while (again());
    job->committed = true;
    return;
}
""", [])

    def test_nested_loop_not_terminal_with_fixed_set(self):
        self.check("""void f(void) {
    if (enabled) {
        do {
            work();
        } while (0);
    }
    job->committed = true;
    return;
}
""", [])

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

    def test_flag_break_followed_by_return_from_user_example(self):
        self.check("""void compare_units(void) {
    char *previous_name;
    bool symbols_equal = false;
    do {
        char *src_name;
        char *dst_name;

        ASSERT(src_unit);
        ASSERT(dst_unit);

        src_name = cy_unit_name(src_unit);
        dst_name = cy_unit_name(dst_unit);

        if (src_name == NULL || dst_name == NULL) {
            symbols_equal = src_name == dst_name;
            break;
        }
        symbols_equal = STREQUAL(src_name, strlen32(src_name), dst_name,
                                 strlen32(dst_name));
    } while (0);
    if (symbols_equal) {
        return;
    }
    previous_name = cy_unit_name(other_unit);
    use(previous_name);
}
""", [(4, 5)])

    def test_negated_flag_guard(self):
        self.check("""int32 f(void) {
    bool ok = true;
    do {
        if (bad()) {
            ok = false;
            break;
        }
        ok = check();
    } while (0);
    if (!ok) {
        return -1;
    }
    return 0;
}
""", [(3, 5)])

    def test_nested_if_break_inside_outer_do(self):
        self.check("""void f(void) {
    bool found = false;
    if (ready) {
        do {
            if (missing) {
                found = true;
                break;
            }
            work();
        } while (0);
        if (found) {
            return;
        }
        continue_work();
    }
}
""", [(4, 9)])

    def test_unrelated_statement_before_guard(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            break;
        }
    } while (0);
    cleanup();
    if (flag) {
        return;
    }
}
""", [])

    def test_shared_work_after_loop_is_not_flag_guard(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            break;
        }
    } while (0);
    if (flag) {
        cleanup();
        return;
    }
}
""", [])

    def test_nested_for_break_does_not_exit_do(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        for (int32 i = 0; i < 10; i += 1) {
            if (bad(i)) {
                flag = true;
                break;
            }
        }
        after_for();
    } while (0);
    if (flag) {
        return;
    }
}
""", [])

    def test_flag_assignment_not_before_break(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            report();
            break;
        }
    } while (0);
    if (flag) {
        return;
    }
}
""", [])

    def test_flag_guard_with_else_is_not_a_pure_return(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            break;
        }
    } while (0);
    if (flag) {
        return;
    } else {
        cleanup();
    }
}
""", [])

    def test_break_and_guard_must_use_same_flag(self):
        self.check("""void f(void) {
    bool flag = false;
    bool other = false;
    do {
        if (bad()) {
            other = true;
            break;
        }
    } while (0);
    if (flag) {
        return;
    }
}
""", [])

    def test_ordinary_do_while_is_not_flagged(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            break;
        }
    } while (again());
    if (flag) {
        return;
    }
}
""", [])

    def test_nonreturn_flag_guard_is_not_flagged(self):
        self.check("""void f(void) {
    bool flag = false;
    do {
        if (bad()) {
            flag = true;
            break;
        }
    } while (0);
    if (flag) {
        report();
    }
    cleanup();
}
""", [])

    def test_flag_break_with_return_guard_can_be_exempted(self):
        config = Path(self.temporary.name) / "csanecheck.conf"
        config.write_text("allow-rule STYLE-FINAL-DO-WHILE-ZERO .*\n",
                          encoding="utf-8")
        self.check("""void f(void) {
    bool found = false;
    do {
        if (bad()) {
            found = true;
            break;
        }
    } while (0);
    if (found) {
        return;
    }
    other_work();
}
""", [], config=config)

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
