#!/usr/bin/env python3
"""Require > 0 instead of != 0 for simple count-shaped names."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(r":(\d+):(\d+): error\[STYLE-COUNT-NONZERO\]")


class CountNonzeroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "count_nonzero.c"
        self.assertTrue(CHECKER.is_file(), "build csanecheck first")

    def check(self, source, expected_lines, extra=()):
        self.path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [str(CHECKER), "--no-project-config", "--file", str(self.path),
             *extra],
            cwd=ROOT, capture_output=True, text=True, check=False, timeout=15,
        )
        self.assertIn(result.returncode, (0, 1), result.stderr)
        found = [(int(line), int(column)) for line, column in
                 DIAGNOSTIC.findall(result.stdout)]
        lines = source.splitlines()
        offsets = {}
        expected = []
        for line in expected_lines:
            offset = lines[line - 1].index("!=", offsets.get(line, 0))
            expected.append((line, offset + 1))
            offsets[line] = offset + 2
        self.assertEqual(found, expected, result.stdout + result.stderr)

    def test_suffix_and_prefix_patterns(self):
        self.check("""void f(void) {
    if (logged_count != 0) { use(); }
    if (array_len != 0) { use(); }
    if (buffer_cap != 0) { use(); }
    if (buffer_capacity != 0) { use(); }
    if (0 != list_cap) { use(); }
    if (0 != queue_capacity) { use(); }
    if (nitems != 0) { use(); }
    if (nrows != 0) { use(); }
    if (nids != 0) { use(); }
    if (nones != 0) { use(); }
    if (ninput_channels != 0) { use(); }
    while (record_count != 0) { use(); }
    for (; tasks_len != 0;) { use(); }
    int32 present = nresults != 0;
    if (0 != entry_count) { use(); }
    if (0 != nflags) { use(); }
}
""", list(range(2, 18)))

    def test_members_comments_parentheses_and_nested_conditions(self):
        self.check("""void f(void) {
    if (obj.logged_count != 0) { use(); }
    if (ptr->array_len != 0) { use(); }
    if (mstruct->len != 0) { use(); }
    if (mstruct->count != 0) { use(); }
    if (mstruct->cap != 0) { use(); }
    if (mstruct->capacity != 0) { use(); }
    if (mstruct.len != 0) { use(); }
    if (mstruct.count != 0) { use(); }
    if (0 != mstruct->len) { use(); }
    if (0 != mstruct->count) { use(); }
    if (0 != mstruct->cap) { use(); }
    if (0 != mstruct->capacity) { use(); }
    if (((container->array.len)) != ((0))) { use(); }
    if (0 != ((ptr->buffer.capacity))) { use(); }
    if (0 != obj.nitems) { use(); }
    if ((obj.nrows) != (0)) { use(); }
    if (((ptr->logged_count)) != ((0))) { use(); }
    if (0 /* gap */ != /* gap */ obj.array_len) { use(); }
    if (ready && (nrows != 0 || array_len != 0)) { use(); }
    return (0 != (obj.nitems));
}
""", [2, 3, *range(4, 16), 16, 17, 18, 19, 20, 20, 21])

    def test_suffix_n_plural_and_members(self):
        self.check("""void f(void) {
    if (file_nitems != 0) { use(); }
    if (buffer_nrows != 0) { use(); }
    if (input_nchannels != 0) { use(); }
    if (0 != entries_ninput_channels) { use(); }
    if (mstruct->nitems != 0) { use(); }
    if (mstruct->nrows != 0) { use(); }
    if (mstruct->ninput_channels != 0) { use(); }
    if (mstruct.nfiles != 0) { use(); }
    if (((mstruct->items.nrows)) != 0) { use(); }
    if (0 != obj->entry_nitems) { use(); }
}
""", list(range(2, 12)))

    def test_direct_count_function_calls(self):
        self.check("""void f(void) {
    if (get_count() != 0) { use(); }
    if (array_len() != 0) { use(); }
    if (buffer_cap() != 0) { use(); }
    if (buffer_capacity() != 0) { use(); }
    if (get_nitems() != 0) { use(); }
    if (get_ninput_channels() != 0) { use(); }
    if (0 != get_nrows()) { use(); }
    if (0 != get_capacity(data)) { use(); }
    if (0 != array_len(data, sizeof(data))) { use(); }
    if (((get_count())) != ((0))) { use(); }
    if (0 != ((buffer_nitems(1, helper(2))))) { use(); }
    if (obj.get_count() != 0) { use(); }
    if (ptr->get_len() != 0) { use(); }
    if (ptr->get_cap() != 0) { use(); }
    if (obj.buffer_capacity() != 0) { use(); }
    if (ptr->get_nrows() != 0) { use(); }
    if (ptr->nitems() != 0) { use(); }
    if (obj.count() != 0) { use(); }
    if (obj->len() != 0) { use(); }
    if (obj->capacity() != 0) { use(); }
    while (get_count() != 0) { use(); }
    int32 available = get_nitems() != 0;
}
""", list(range(2, 24)))

    def test_exact_suffix_names_and_macro_calls(self):
        self.check("""#define NONEMPTY (0 != get_count())
#define CALL_LEN(x) (0 != get_len(x))
void f(void) {
    if (_count() != 0) { use(); }
    if (_len() != 0) { use(); }
    if (_cap() != 0) { use(); }
    if (_capacity() != 0) { use(); }
    if (_nrows() != 0) { use(); }
    if (_count != 0) { use(); }
    if (_capacity != 0) { use(); }
    if (0 != mstruct->nitems) { use(); }
    if (0 != mstruct->get_nfiles()) { use(); }
    if (0 != (get_nitems(one(), two(3)))) { use(); }
}
""", [1, 2, *range(4, 14)])

    def test_calls_do_not_match_computed_expressions(self):
        self.check("""void f(void) {
    if (foo_count() + 1 != 0) { use(); }
    if (0 != foo_count() + 1) { use(); }
    if (obj->get_len() * scale != 0) { use(); }
    if (foo_count()[0] != 0) { use(); }
    if (0 != foo_count()[0]) { use(); }
    if (foo_count()->len != 0) { use(); }
    if (foo_count()() != 0) { use(); }
    if (0 != (foo_count() || ready)) { use(); }
    if (obj->get_counted() != 0) { use(); }
    if (obj->get_nstatus() != 0) { use(); }
    if (obj->get_network_success() != 0) { use(); }
    if (get_nerror_code() != 0) { use(); }
    if (foo_ncapacity() != 0) { use(); }
    if (foo_count(2) != 1) { use(); }
    if (foo_len() == 0) { use(); }
    if (foo_capacity() > 0) { use(); }
}
""", [])

    def test_no_false_positives_for_noncounts_and_expressions(self):
        self.check("""void f(void) {
    if (status != 0) { use(); }
    if (nondecimal_status != 0) { use(); }
    if (network_success != 0) { use(); }
    if (name != 0) { use(); }
    if (nerror_code != 0) { use(); }
    if (array_length != 0) { use(); }
    if (foo_capacity2 != 0) { use(); }
    if (foo_capped != 0) { use(); }
    if (len != 0) { use(); }
    if (count != 0) { use(); }
    if (cap != 0) { use(); }
    if (capacity != 0) { use(); }
    if (obj.status != 0) { use(); }
    if (obj.counter != 0) { use(); }
    if (foo_counted != 0) { use(); }
    if (foo_count != 1) { use(); }
    if (foo_count == 0) { use(); }
    if (foo_count > 0) { use(); }
    if (foo_counted() != 0) { use(); }
    if (0 != foo_counter()) { use(); }
    if (obj.foo_counter() != 0) { use(); }
    if (obj.status() != 0) { use(); }
    if (obj->length() != 0) { use(); }
    if (value + logged_count != 0) { use(); }
    if (logged_count + value != 0) { use(); }
    if (logged_count != 0 + value) { use(); }
    if (0 + value != logged_count) { use(); }
    if (0 != logged_count + value) { use(); }
    if ((logged_count + 1) != 0) { use(); }
    if ((0 + 1) != logged_count) { use(); }
    if (logged_count[0] != 0) { use(); }
    if (0 != logged_count[0]) { use(); }
    if (0 != -array_len) { use(); }
    if (0 != (int32)array_len) { use(); }
    if (logged_count != 0U) { use(); }
    if (logged_count != 00) { use(); }
    if (logged_count != NULL) { use(); }
    // if (logged_count != 0) { use(); }
    char *example = "logged_count != 0";
}
""", [])

    def test_macros_conditional_branches_and_multiline(self):
        self.check("""#define HAS_ITEMS(nitems) ((nitems) != 0)
#define EMPTY_ITEMS(nitems) ((nitems) == 0)
#if defined(USE_FIRST)
void f(void) {
    if (users_count
        != 0) { use(); }
}
#else
void f(void) {
    if (0 != data_len) { use(); }
}
#endif
""", [1, 6, 10])

    def test_rule_exemption(self):
        config = Path(self.temp.name) / "csane.conf"
        config.write_text("allow-rule STYLE-COUNT-NONZERO .*\\.c$\n",
                          encoding="utf-8")
        source = "void f(void) { if (array_len != 0) { use(); } }\n"
        self.check(source, [1])
        self.check(source, [], ("--config", str(config)))


if __name__ == "__main__":
    unittest.main()
