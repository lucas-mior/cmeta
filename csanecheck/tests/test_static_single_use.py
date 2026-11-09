#!/usr/bin/env python3
"""Regression contract for zero-or-one-call, same-file static functions."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
FINDING = re.compile(r":(\d+):\d+: error\[STYLE-STATIC-ONCE\]")
FILE_FINDING = re.compile(
    r"(?m)^(.+?):(\d+):\d+: error\[STYLE-STATIC-ONCE\]"
)


class StaticSingleUseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "single_use.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, name="single_use.c"):
        self.path = Path(self.temp.name) / name
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
        lines = [int(match) for match in FINDING.findall(result.stdout)]
        self.assertEqual(lines, expected_lines, result.stdout + result.stderr)
        self.assertLessEqual(result.returncode, 1, result.stderr)
        return result.stdout

    def check_files(self, sources, expected):
        """Check findings across physical .c files in one invocation."""
        paths = {}
        for name, source in sources.items():
            path = Path(self.temp.name) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            paths[name] = path
        command = [str(CHECKER), "--no-project-config"]
        for path in paths.values():
            command.extend(("--file", str(path)))
        result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                text=True, check=False)
        found = sorted(
            (str(Path(name).resolve()), int(line))
            for name, line in FILE_FINDING.findall(result.stdout)
        )
        wanted = sorted(
            (str(paths[name].resolve()), line)
            for name, lines in expected.items()
            for line in lines
        )
        self.assertEqual(found, wanted, result.stdout + result.stderr)
        self.assertLessEqual(result.returncode, 1, result.stderr)
        return result.stdout

    def test_diagnostic_identifies_function(self):
        long_name = "single_use_" + "long_identifier_" * 30
        output = self.check(f"""static int first(void) {{ return 1; }}
static int {long_name}(void) {{ return 2; }}
int caller(void) {{ return first() + {long_name}(); }}
""", [1, 2])
        self.assertIn("static function 'first' called only once", output)
        self.assertIn(
            f"static function '{long_name}' called only once", output,
        )
        self.assertIn(
            "inline its body at the call site and remove the function "
            "definition and any prototype", output,
        )

    def test_one_call_and_forward_declaration(self):
        self.check("""static int once(int value);
static int once(int value) { return value + 1; }
int caller(void) { return once(2); }
""", [2])

    def test_multiple_calls_are_not_reported(self):
        self.check("""static int twice(int value) { return value; }
int caller(void) { return twice(1) + twice(2); }
""", [])

    def test_unused_function_is_reported(self):
        output = self.check("""static void unused(void) { return; }
int caller(void) { return 0; }
""", [1])
        self.assertIn("static function 'unused' never called", output)
        self.assertIn("remove the function definition", output)

    def test_unused_with_forward_declaration(self):
        self.check("""static int unused(void);
static int unused(void) { return 1; }
int caller(void) { return 0; }
""", [2])

    def test_indirect_references_prevent_finding(self):
        self.check("""static int target(void) { return 1; }
static int (*callback)(void) = target;
int caller(void) { return target(); }
""", [])

    def test_self_recursive_without_external_calls_is_reported(self):
        self.check("""static int recurse(int n) {
    return n ? recurse(n - 1) : 0;
}
""", [1])

    def test_recursive_only_finding_recommends_removal(self):
        output = self.check("""static int recurse(int n) {
    return n ? recurse(n - 1) : 0;
}
""", [1])
        self.assertIn("static function 'recurse' calls only itself", output)
        self.assertIn("remove the function definition", output)
        self.assertNotIn("inline its body", output)

    def test_recursive_function_prevents_finding(self):
        self.check("""static int recurse(int n) {
    return n ? recurse(n - 1) : 0;
}
int caller(void) { return recurse(3); }
""", [])

    def test_macros_and_conditional_calls_prevent_finding(self):
        self.check("""static int macro_called(void) { return 1; }
#define CALLIT() macro_called()
int caller(void) { return macro_called(); }
static int conditional(void) { return 2; }
int other(void) {
#if defined(OPTION)
    return conditional();
#else
    return 0;
#endif
}
""", [])

    def test_conditional_definitions_are_not_safely_unique(self):
        self.check("""#if defined(OPTION)
static int helper(void) { return 1; }
#else
static int helper(void) { return 2; }
#endif
int caller(void) { return helper(); }
""", [])

    def test_comments_and_strings_do_not_count_as_calls(self):
        self.check("""static int action(void) { return 1; }
int caller(void) { return action(); }
// action() in a comment
char *message = "action() in a string";
""", [1])

    def test_member_calls_do_not_count_as_static_calls(self):
        self.check("""static int action(void) { return 1; }
int caller(void) { return action(); }
int other(void) { return obj->action(); }
""", [1])

    def test_header_functions_are_excluded(self):
        self.check("""static int helper(void) { return 1; }
int caller(void) { return helper(); }
""", [], name="single_use.h")

    def test_whole_file_include_guard_is_allowed(self):
        self.check("""#if !defined(SINGLE_USE_C)
#define SINGLE_USE_C
static int helper(void) { return 1; }
int caller(void) { return helper(); }
#endif /* SINGLE_USE_C */
""", [3])

    def test_testing_helpers_are_exempt_but_production_helpers_are_not(self):
        self.check("""#if !defined(SINGLE_USE_C)
#define SINGLE_USE_C
static int production(void) { return 1; }
int caller(void) { return production(); }
#if TESTING_single_use
static int test_helper(void) { return 2; }
int main(void) { return test_helper(); }
#endif /* TESTING_single_use */
#endif /* SINGLE_USE_C */
""", [3])

    def test_nested_testing_blocks_are_exempt(self):
        self.check("""#if TESTING_single_use
#if defined(OTHER_FEATURE)
static int nested_helper(void) { return 1; }
int test(void) { return nested_helper(); }
#endif
#endif
""", [])
        self.check("""#ifdef TESTING_single_use
static int helper(void) { return 1; }
int test(void) { return helper(); }
#endif
""", [])
        self.check("""#if defined(TESTING_single_use)
static int helper(void) { return 1; }
int test(void) { return helper(); }
#endif
""", [])

    def test_non_testing_static_function_still_reported(self):
        self.check("""static int helper(void) { return 1; }
int caller(void) { return helper(); }
#if TESTING_single_use
static int test_helper(void) { return 2; }
int test(void) { return test_helper(); }
#endif
""", [1])

    def test_call_indentation_boundary(self):
        self.check("""static int two_levels(void) { return 1; }
int caller_two(void) {
    if (1) {
        return two_levels();
    }
}
static int three_levels(void) { return 2; }
int caller_three(void) {
    if (1) {
        if (1) {
            return three_levels();
        }
    }
}
""", [1, 7])

    def test_wrapped_call_uses_statement_indentation(self):
        self.check("""static int helper(int value) { return value; }
int caller(void) {
    return
                helper(123);
}
""", [1])

    def test_configuration_only_exempts_cbase(self):
        source = """static int helper(void) { return 1; }
int caller(void) { return helper(); }
"""
        config = Path(self.temp.name) / "policy.conf"
        config.write_text("allow-rule STYLE-STATIC-ONCE (^|/)cbase/.*\n",
                          encoding="utf-8")
        for name, expected in (("cbase/sample.c", []),
                               ("src/sample.c", [1]),
                               ("cbase_fake/sample.c", [1])):
            path = Path(self.temp.name) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [str(CHECKER), "--no-project-config", "--config",
                 str(config), "--file", str(path)],
                cwd=ROOT, capture_output=True, text=True, check=False,
            )
            lines = [int(match) for match in FINDING.findall(result.stdout)]
            self.assertEqual(lines, expected, result.stdout + result.stderr)
            self.assertLessEqual(result.returncode, 1, result.stderr)

    def test_testing_only_call_counts_for_production_definition(self):
        self.check("""static int helper(void) { return 1; }
#if TESTING_sample
int test(void) { return helper(); }
#endif
""", [1])

    def test_testing_and_production_calls_count_together(self):
        self.check("""static int helper(void) { return 1; }
int caller(void) { return helper(); }
#if TESTING_sample
int test(void) { return helper(); }
#endif
""", [])

    def test_unused_testing_definition_is_exempt(self):
        self.check("""#if TESTING_sample
static int unused_test_helper(void) { return 1; }
#endif
""", [])

    def test_testing_nested_inside_feature_exempts_definitions(self):
        self.check("""#if defined(FEATURE)
#if TESTING_sample
static int helper(void) { return 1; }
int test(void) { return helper(); }
#endif
#endif
""", [])

    def test_testing_macro_after_feature_exempts_definitions(self):
        self.check("""#if defined(FEATURE) && defined(TESTING_sample)
static int helper(void) { return 1; }
int test(void) { return helper(); }
#endif
""", [])

    def test_non_testing_ifdef_guard_does_not_exempt(self):
        self.check("""#ifdef FEATURE
static int helper(void) { return 1; }
int caller(void) { return helper(); }
#endif
""", [2])

    def test_non_testing_else_branch_does_not_exempt(self):
        self.check("""#if FEATURE
int fallback(void) { return 0; }
#else
static int helper(void) { return 1; }
int caller(void) { return helper(); }
#endif
""", [4])

    def test_indirect_reference_without_direct_call_is_not_unused(self):
        self.check("""static int target(void) { return 1; }
static int (*callback)(void) = target;
""", [])

    def test_cross_file_only_call_suppresses_unused_diagnostic(self):
        self.check_files({
            "src/definition.c": """static int helper(void) { return 1; }
""",
            "src/consumer.c": """#include "definition.c"
int caller(void) { return helper(); }
""",
        }, {})

    def test_cross_file_call_suppresses_local_single_use(self):
        self.check_files({
            "src/definition.c": """static int helper(void) { return 1; }
int local(void) { return helper(); }
""",
            "src/consumer.c": """#include "definition.c"
int other(void) { return helper(); }
""",
        }, {})

    def test_transitive_cross_file_call_suppresses_finding(self):
        self.check_files({
            "src/definition.c": """static int helper(void) { return 1; }
int local(void) { return helper(); }
""",
            "src/middle.c": """#include "definition.c"
""",
            "src/consumer.c": """#include "middle.c"
int other(void) { return helper(); }
""",
        }, {})

    def test_conditional_cross_file_include_is_conservative(self):
        self.check_files({
            "src/definition.c": """static int helper(void) { return 1; }
int local(void) { return helper(); }
""",
            "src/consumer.c": """#if FEATURE
#include "definition.c"
int other(void) { return helper(); }
#endif
""",
        }, {})

    def test_independent_translation_units_keep_distinct_statics(self):
        self.check_files({
            "src/one.c": """static int helper(void) { return 1; }
int one(void) { return helper(); }
""",
            "src/two.c": """static int helper(void) { return 2; }
int two(void) { return helper(); }
""",
        }, {"src/one.c": [1], "src/two.c": [1]})

    def test_non_testing_conditional_guard_does_not_exempt(self):
        self.check("""#if defined(FEATURE)
static int helper(void) { return 1; }
int caller(void) { return helper(); }
#endif
""", [2])


if __name__ == "__main__":
    unittest.main()
