#!/usr/bin/env python3
"""Conservative, complete-group postponed-initialization detection."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[STYLE-POSTPONED-INIT\]")


class PostponedInitializationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "fixture.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, policy=""):
        self.path.write_text(source, encoding="utf-8")
        config = self.path.parent / "csanecheck.conf"
        config.write_text(policy, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config", str(config),
             "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_requested_example_inside_loop(self):
        self.check("""void f(int32 count, Fit *fit, Result *result) {
    for (int32 i = 0; i < count; i += 1) {
        CyFitParamSelection *selection = &fit->params[i];
        CyFitValueResult *value = &result->params[i];
        char *unit;
        int32 unit_len;

        unit = cy_unit_name(&selection->ref.param->unit);
        unit_len = cy_unit_name_len(&selection->ref.param->unit);
        if (unit == NULL) {
            unit = "";
            unit_len = 0;
        }
    }
}
""", [])

    def test_one_declaration_followed_by_assignment(self):
        self.check("""void f(void) {
    int32 count;
    count = compute_count();
    consume(count);
}
""", [2])

    def test_complete_adjacent_group_only(self):
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = 1;
    second = first + 2;
}
""", [2])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = 1;
    use(first);
}
""", [])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    second = 2;
}
""", [])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = 1;
}
""", [])

    def test_no_reordering_or_intervening_work(self):
        self.check("""void f(void) {
    int32 first;
    int32 second;
    second = side_effect();
    first = side_effect();
}
""", [])
        self.check("""void f(void) {
    int32 first;
    action();
    first = 1;
}
""", [])
        self.check("""void f(void) {
    int32 first;
    if (condition) {
        first = 1;
    }
}
""", [])
        self.check("""void f(void) {
    int32 first;
    first += 1;
}
""", [])

    def test_unsupported_declaration_blocks_are_not_partially_reported(self):
        self.check("""void f(void) {
    int32 buffer[4];
    int32 count;
    count = 4;
}
""", [])
        self.check("""void f(void) {
    int32 first, second;
    int32 third;
    third = 3;
}
""", [])
        self.check("""void f(void) {
    static int32 persistent;
    persistent = 1;
}
""", [])
        self.check("""void f(void) {
    volatile int32 register_value;
    register_value = 1;
}
""", [])

    def test_initialized_declarations_are_boundaries(self):
        self.check("""void f(void) {
    int32 first = one();
    int32 second;
    second = two();
}
""", [3])
        self.check("""void f(void) {
    int32 first;
    int32 second = two();
    first = one();
}
""", [])

    def test_comments_do_not_count_as_execution(self):
        self.check("""void f(void) {
    int32 count;
    /* Describe the initialization. */
    count = 12;
}
""", [2])

    def test_disconnected_preprocessor_paths_remain_separate(self):
        self.check("""void f(void) {
    int32 count;
#if ENABLED
    count = 1;
#else
    count = 2;
#endif
}
""", [])
        self.check("""void f(void) {
    int32 count;
#define MULTIPLIER 2
    count = MULTIPLIER;
}
""", [])

    def test_multiline_assignment_suppresses_entire_group(self):
        self.check("""void f(Composite *comp, double *r, double *base_scales,
       double *w) {
    for (int32 i = 0; i < comp->top->nconns; i += 1) {
        double residual = fabs(r[i]);
        double scale = base_scales[i];
        double abs_w = fabs(w[i]);
        double allowed;
        double err;

        allowed = cy_tolerance_scale(abs_w, scale,
                                     comp->options.signal_rtol,
                                     comp->options.signal_atol);
        err = residual / allowed;
        if (err > max_error) {
            max_error = err;
        }
    }
}
""", [])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = 1;
    second = compute(
        first);
}
""", [])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = compute(
        second);
    second = 1;
}
""", [])
        self.check("""void f(void) {
    int32 value;
    value =
        compute();
}
""", [])

    def test_multiline_declaration_suppresses_entire_group(self):
        self.check("""void f(void) {
    int32 first;
    int32
        second;
    first = 1;
    second = 2;
}
""", [])
        self.check("""void f(void) {
    int32
        first;
    int32 second;
    first = 1;
    second = 2;
}
""", [])

    def test_initializer_must_fit_column_limit(self):
        self.check("""void f(void) {
    int32 value;
    value = fairly_long_function_name_with_many_arguments(first, second, third);
}
""", [])
        self.check("""void f(void) {
    int32 value;
    value = 2;
}
""", [2])

    def test_later_plain_reassignment_suppresses_rule(self):
        self.check("""void f(void) {
    int32 count;
    count = initial();
    use(count);
    count = updated();
}
""", [])

    def test_nested_reassignment_suppresses_rule(self):
        self.check("""void f(void) {
    int32 count;
    count = initial();
    if (condition()) {
        count = updated();
    }
}
""", [])
        self.check("""void f(void) {
    int32 count;
    count = initial();
    for (int32 i = 0; i < 3; i += 1) {
        count = i;
    }
}
""", [])

    def test_updates_suppress_rule(self):
        for update in ("count += 1;", "count -= 1;", "count *= 2;",
                       "count /= 2;", "count %= 2;", "count &= mask;",
                       "count |= mask;", "count ^= mask;",
                       "count <<= 1;", "count >>= 1;", "count++;",
                       "++count;", "count--;", "--count;"):
            with self.subTest(update=update):
                self.check("""void f(void) {
    int32 count;
    count = initial();
    %s
}
""" % update, [])

    def test_parenthesized_writes_suppress_rule(self):
        self.check("""void f(void) {
    int32 count;
    count = initial();
    ((count)) = next_value();
}
""", [])
        self.check("""void f(void) {
    int32 count;
    count = initial();
    (++(count));
}
""", [])

    def test_other_member_writes_do_not_count(self):
        self.check("""void f(Info *info) {
    int32 count;
    count = initial();
    info->count = updated();
    use(count);
}
""", [2])

    def test_reassignment_suppresses_entire_group(self):
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = one();
    second = two();
    second = three();
}
""", [])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = one();
    second = two();
    first = three();
}
""", [])

    def test_initialization_only_once_remains_flagged(self):
        self.check("""void f(void) {
    int32 count;
    count = compute();
    use(count);
    if (enabled()) {
        use(count);
    }
}
""", [2])
        self.check("""void f(void) {
    int32 first;
    int32 second;
    first = one();
    second = two();
    consume(first, second);
}
""", [2])

    def test_other_function_writes_do_not_count(self):
        self.check("""void f(void) {
    int32 count;
    count = compute();
}
void other(void) {
    int32 count;
    count = 1;
    count = 2;
}
""", [2])

    def test_config_exemption(self):
        self.check("""void f(void) {
    int32 count;
    count = 2;
}
""", [], policy="allow-rule STYLE-POSTPONED-INIT .*fixture\\.c$\n")


if __name__ == "__main__":
    unittest.main()
