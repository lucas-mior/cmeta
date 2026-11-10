#!/usr/bin/env python3
"""FMT-IF-ASSIGN-WRAP: prefer call arguments to breaks after = in if."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):\d+: error\[FMT-IF-ASSIGN-WRAP\]")


class IfAssignmentWrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "if_assignment_wrap.c"

    def check(self, source, expected, name=None):
        path = self.path if name is None else self.path.with_name(name)
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(path)],
            cwd=ROOT, text=True, capture_output=True, check=False,
            timeout=5,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [int(line) for line in DIAGNOSTIC.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_reported_case_and_preferred_replacement(self):
        self.check("""void f(void) {
            if ((status =
                     cy_fit_result_sample_signal(fit, result,
                                                 CY_SIGNAL_KIND_OUTPUT, i,
                                                 CY_INPUT_LINEAR, t,
                                                 &time_cursor, destination))
                                                     < 0) {
                goto fail;
            }
}
""", [2])
        self.check("""void f(void) {
            if ((status = cy_fit_result_sample_signal(fit, result,
                                                      CY_SIGNAL_KIND_OUTPUT, i,
                                                      CY_INPUT_LINEAR, t,
                                                      &time_cursor,
                                                      destination)) < 0) {
                goto fail;
            }
}
""", [])

    def test_else_if_and_single_assignment_condition(self):
        self.check("""void f(void) {
    if (ready) {
        work();
    } else if ((result =
                    long_function_name(first, second, third,
                                       fourth, fifth, sixth))) {
        work();
    }
    if (value =
            compute_next_item(first, second, third,
                              fourth, fifth, sixth)) {
        work();
    }
}
""", [4])

    def test_only_if_conditions(self):
        self.check("""void f(void) {
    if ((status = call(first, second, third, fourth, fifth))) {
        work();
    }
    while ((status =
                call(first, second, third, fourth, fifth))) {
        work();
    }
    for (; (status =
                call(first, second, third, fourth, fifth));) {
        work();
    }
    status =
        call(first, second, third, fourth, fifth);
}
""", [])

    def test_must_have_a_named_call_after_assignment(self):
        self.check("""void f(void) {
    if ((status =
             another_status + something) < 0) {
        work();
    }
    if ((status =
             value) < 0) {
        work();
    }
    if ((status =
             (compute(a, b))) < 0) {
        work();
    }
    if ((status =
             call()) < 0) {
        work();
    }
}
""", [])

    def test_long_name_or_argument_requires_assignment_break(self):
        self.check("""void f(void) {
    if ((status =
             function_with_an_extremely_long_name_that_cannot_fit_with_arg(
                 first, second, third)) < 0) {
        work();
    }
    if ((status =
             call(first_argument_that_is_far_too_long_for_this_location_and_keeps_going,
                  second)) < 0) {
        work();
    }
    if ((status =
             longer_named_call(first,
                               a_following_argument_that_cannot_be_aligned_because_it_is_too_long,
                               third)) < 0) {
        work();
    }
}
""", [])

    def test_comments_preprocessor_and_multiple_assignments(self):
        self.check("""void f(void) {
    if ((status = /* keep this break */
             call(first, second, third, fourth, fifth)) < 0) {
        work();
    }
    if ((status =
#if defined(FLAG)
             call(first, second, third, fourth, fifth)
#else
             call(first, second)
#endif
            ) < 0) {
        work();
    }
    if (((status =
              call(first, second, third, fourth, fifth)) < 0)
        && (value = call(other))) {
        work();
    }
}
""", [])

    def test_generated_headers_are_excluded(self):
        self.check("""void f(void) {
    if ((status =
             some_call(first, second, third, fourth, fifth)) < 0) {
        work();
    }
}
""", [], name="generated.meta.h")


if __name__ == "__main__":
    unittest.main()
