#!/usr/bin/env python3
"""Redundant NULL-entry guards in file-local static functions."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
FINDING = re.compile(r":(\d+):\d+: error\[STYLE-STATIC-NULL-GUARD\]")


class StaticNullGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected, name="sample.c"):
        path = Path(self.temp.name) / name
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        found = [int(line) for line in FINDING.findall(result.stdout)]
        self.assertEqual(found, expected, result.stdout + result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        return result.stdout

    def test_experimental_fit_regression(self):
        result = self.check("""typedef struct ExperimentalFitRun { int x; } ExperimentalFitRun;
static void experimental_fit_run_clear(ExperimentalFitRun *run) {
    if (run == NULL) {
        return;
    }
    *run = (ExperimentalFitRun){0};
}
void main_case(void) {
    ExperimentalFitRun direct = {0};
    ExperimentalFitRun refined = {0};
    experimental_fit_run_clear(&direct);
    experimental_fit_run_clear(&refined);
}
""", [3])
        self.assertIn("NULL guard for 'run' is redundant", result)
        self.assertIn("all direct calls pass &variable", result)

    def test_reverse_comparison_and_bare_return(self):
        self.check("""static void clear(int *item) {
    if (NULL == item) return;
    *item = 0;
}
void caller(void) { int x = 1; clear(&x); clear((&x)); }
""", [2])

    def test_multiple_pointer_parameters(self):
        self.check("""static void clear(int *first, int *second) {
    if (first == NULL) { return; }
    if (second == NULL) { return; }
    *first = *second;
}
void caller(void) { int a, b; clear(&a, &b); clear(&b, &a); }
""", [2, 3])

    def test_pointer_parameter_with_restrict(self):
        self.check("""static void clear(int *restrict item) {
    if (item == NULL) { return; }
    *item = 0;
}
void caller(void) { int a; clear(&a); clear(&a); }
""", [2])

    def test_pointer_value_at_one_call_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
void caller(int *ptr) { int x; clear(&x); clear(ptr); }
""", [])

    def test_any_uncertain_address_form_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
void caller(int *ptr) { int a[2]; clear(&a[0]); clear(ptr); }
""", [])
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
void caller(void) { int a[2]; clear(&a[0]); clear(&a[1]); }
""", [])

    def test_address_escape_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
static void (*callback)(int *) = clear;
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_macro_reference_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
#define OTHER() clear(NULL)
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_not_static_or_not_c_file(self):
        source = """void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
void caller(void) { int x; clear(&x); clear(&x); }
"""
        self.check(source, [])
        self.check(source.replace("void clear", "static void clear"), [],
                   name="sample.h")

    def test_unused_function_has_no_verified_calls(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
""", [])

    def test_no_early_return_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { log_failure(); return; }
    *item = 0;
}
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_guard_not_first_suppresses(self):
        self.check("""static void clear(int *item) {
    item = get_next(item);
    if (item == NULL) { return; }
    *item = 0;
}
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_guard_with_else_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; } else { report_good(); }
    *item = 0;
}
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_conditional_duplicate_definition_suppresses(self):
        self.check("""#if defined(OPTION)
static void clear(int *item) { if (item == NULL) { return; } }
#else
static void clear(int *item) { if (item == NULL) { return; } }
#endif
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_function_prototype_and_unrelated_member(self):
        self.check("""static void clear(int *item);
static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
struct Ops { void (*clear)(int *); };
void caller(struct Ops *ops) {
    int x;
    ops->clear(&x);
    clear(&x);
    clear(&x);
}
""", [3])

    def test_included_c_file_call_suppresses(self):
        root = Path(self.temp.name)
        definition = root / "definition.c"
        consumer = root / "consumer.c"
        definition.write_text("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
void local(void) { int x; clear(&x); clear(&x); }
""", encoding="utf-8")
        consumer.write_text("""#include "definition.c"
void foreign(int *ptr) { clear(ptr); }
""", encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(definition),
             "--file", str(consumer)],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        self.assertNotIn("STYLE-STATIC-NULL-GUARD", result.stdout,
                         result.stdout + result.stderr)
        self.assertIn(result.returncode, (0, 1), result.stderr)

    def test_missing_c_include_suppresses(self):
        self.check("""static void clear(int *item) {
    if (item == NULL) { return; }
    *item = 0;
}
#include "missing.c"
void caller(void) { int x; clear(&x); clear(&x); }
""", [])

    def test_rule_exemption(self):
        path = Path(self.temp.name) / "sample.c"
        path.write_text("""static void clear(int *x) {
    if (x == NULL) { return; }
    *x = 1;
}
void caller(void) { int a; clear(&a); clear(&a); }
""", encoding="utf-8")
        config = Path(self.temp.name) / "csanecheck.conf"
        config.write_text(
            r"allow-rule STYLE-STATIC-NULL-GUARD .*sample[.]c" + "\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--config", str(config),
             "--file", str(path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=10,
        )
        self.assertNotIn("STYLE-STATIC-NULL-GUARD", result.stdout,
                         result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0,
                         result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
