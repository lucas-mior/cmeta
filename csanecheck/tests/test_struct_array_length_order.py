#!/usr/bin/env python3
"""Consecutive pointer/length pairs must group their pointers first."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "bin/csanecheck"
DIAGNOSTIC = re.compile(
    r":(\d+):(\d+): error\[STYLE-STRUCT-ARRAY-LENGTH-ORDER\]"
)


class StructArrayLengthOrderTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "member_order.c"
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
        self.assertEqual(sorted(found), sorted(expected),
                         result.stdout + result.stderr)

    def test_interleaved_pairs_in_original_example(self):
        self.check("""typedef struct MyStruct {
    double *values;
    int16 nvalues;
    char **values2;
    int64 values2_len;
} MyStruct;
""", [(3, 11)])

    def test_all_lengths_follow_all_pointers(self):
        self.check("""typedef struct MyStruct {
    double *values;
    char **values2;
    char ***values3;
    int16 nvalues;
    int32 values2_len;
    int64 nvalues3;
} MyStruct;
""", [])

    def test_repeated_interleaving_reports_each_early_length(self):
        self.check("""typedef struct MyStruct {
    int32 *one;
    int16 none;
    char **two;
    int32 two_len;
    double *three;
    int64 nthree;
} MyStruct;
""", [(3, 11), (5, 11)])

    def test_unpaired_fields_and_unrelated_lengths_are_not_flagged(self):
        self.check("""typedef struct Example {
    char *first;
    int32 nfirst;
    char *unpaired;
    double x;
    int64 nmissing;
    float *second;
    uint32 second_len;
    int32 *third;
    int32_t third_len;
    int64 unrelated;
} Example;
""", [])

    def test_pointer_qualifiers_and_other_fields(self):
        self.check("""typedef struct Example {
    struct Item *items;
    volatile int32 nitems;
    int64 flag;
    char *restrict names;
    int32 names_len;
} Example;
""", [])

    def test_single_pair_among_unrelated_struct_members(self):
        self.check("""static struct {
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    char ignore_path[MAX_PATH_LENGTH];
    char config_path[MAX_PATH_LENGTH];
    char *search_query;
    int32 search_query_len;
    uint32 search_timeout_id;
    GtkWidget *tree[2];
    GtkWidget *stats_label;
} cecup = {0};
""", [])

    def test_unrelated_member_breaks_consecutive_pairs(self):
        self.check("""typedef struct Example {
    char *first;
    int16 first_len;
    int32 unrelated;
    char **second;
    int32 nsecond;
} Example;
""", [])

    def test_unrecognized_member_breaks_consecutive_pairs(self):
        self.check("""typedef struct Example {
    char *first;
    int32 nfirst;
    int32 (*callback)(void);
    char **second;
    int32 second_len;
} Example;
""", [])

    def test_second_pair_must_also_be_adjacent(self):
        self.check("""typedef struct Example {
    char *first;
    int32 nfirst;
    double **second;
    bool ready;
    int64 second_len;
} Example;
""", [])

    def test_only_consecutive_groups_report(self):
        self.check("""typedef struct Example {
    char *a;
    int16 na;
    char *b;
    int32 nb;
    bool separator;
    char *c;
    int32 nc;
    double *d;
    int64 d_len;
} Example;
""", [(3, 11), (8, 11)])

    def test_cecup_style_declared_fields(self):
        self.check("""static struct {
    char *search_query;
    int32 search_query_len;
    uint32 search_timeout_id;
    int32 *rows[2];
    bool *rows_selected;
    int32 rows_len;
    int32 rows_capacity;
    int32 *rows_visible;
    int32 rows_visible_len;
    IgnorePattern *ignore_patterns;
    int32 ignore_count;
    int32 ignore_capacity;
    char **transfers;
    int32 *transfers_lens;
    int32 ntransfers;
    int32 transfers_capacity;
    char **deletions;
    int32 *deletions_lens;
    int32 ndeletions;
    int32 deletions_capacity;
} cecup = {0};
""", [])

    def test_complete_cecup_layout_is_not_flagged(self):
        self.check("""static struct {
    GtkApplication *application;
    GtkWidget *gtk_window;

    GtkWidget *invert_button;
    GtkWidget *dir_entry[2];
    GtkWidget *browse_button[2];
    ulong entry_id[2];
    char *base[2];
    int32 base_len[2];

    GtkWidget *diff_entry;
    GtkWidget *term_entry;
    GtkWidget *search_entry;

    GtkWidget *preview_button;
    GtkWidget *stop_button;
    GtkWidget *sync_button;

    GtkWidget *ignore_button;

    GtkWidget *check_fs_button;
    GtkWidget *delete_ignored_button;
    GtkWidget *delete_after_button;
    GtkWidget *unselect_button;
    GtkWidget *select_visible_button;
    bool delete_ignored;
    bool delete_after;

    GtkWidget *filter_new;
    GtkWidget *filter_link;
    GtkWidget *filter_update;
    GtkWidget *filter_equal;
    GtkWidget *filter_delete;
    GtkWidget *filter_ignore;

    GListModel *store;
    GtkWidget *log_view;
    GtkTextBuffer *log_buffer;
    char ignore_path[MAX_PATH_LENGTH];
    char config_path[MAX_PATH_LENGTH];
    char *search_query;
    int32 search_query_len;
    uint32 search_timeout_id;
    GtkWidget *tree[2];
    GtkWidget *stats_label;

    GtkWidget *progress_bar;

    int32 *rows[2];
    bool *rows_selected;
    int32 rows_len;
    int32 rows_capacity;

    int32 *rows_visible;
    int32 rows_visible_len;

    enum CecupColumn sort_col;
    GtkSortType sort_order;

    Arena *arena;
    pthread_mutex_t arena_mutex;

    // this is only needed for killing child transfers when window is destroyed
    atomic_int child_pid;
    pthread_t work_thread;
    atomic_bool work_thread_done;
    bool work_thread_started;
    bool work_thread_joined;
    bool window_destroying;

    atomic_bool stop_working;

    IgnorePattern *ignore_patterns;
    int32 ignore_count;
    int32 ignore_capacity;

    Traversal traversal[2];

    struct Hash_actions_set *actions_set;

    char **transfers;
    int32 *transfers_lens;
    int32 ntransfers;
    int32 transfers_capacity;

    char **deletions;
    int32 *deletions_lens;
    int32 ndeletions;
    int32 deletions_capacity;

    bool preview_dirty;
} cecup = {0};
""", [])

    def test_nested_structs_are_independent(self):
        self.check("""typedef struct Outer {
    struct {
        char *inner;
        int32 ninner;
        double **other;
        int64 other_len;
    } nested;
    int32 *left;
    int32 nleft;
    double *right;
    int32 nright;
} Outer;
""", [(4, 15), (9, 11)])

    def test_function_pointer_and_fixed_array_not_counted(self):
        self.check("""typedef struct Example {
    char *words;
    int32 nwords;
    int32 (*handler)(int32);
    char chars[20];
    int32 chars_len;
    char *next;
} Example;
""", [])

    def test_comments_and_directive_tokens(self):
        self.check("""// typedef struct Fake { char *a; int32 na; char *b; int32 nb; };
#define BAD "struct Fake { char *a; int32 na; char *b; int32 nb; }"
typedef struct Example {
    char *a;
    int32 na;
#if EXTRA
    char **b;
    int32 nb;
#else
    double *other;
    int64 other_len;
#endif
} Example;
""", [(5, 11)])

    def test_different_conditional_alternatives_not_combined(self):
        self.check("""typedef struct Example {
#if FIRST
    char *a;
    int32 na;
#else
    char *b;
    int32 nb;
#endif
} Example;
""", [])

    def test_independent_conditional_groups_can_coexist(self):
        self.check("""typedef struct Example {
#if FIRST
    char *first;
    int32 nfirst;
#endif
#if SECOND
    double **second;
    int64 second_len;
#endif
} Example;
""", [(4, 11)])

    def test_rule_can_be_suppressed(self):
        policy = Path(self.temporary.name) / "policy.conf"
        policy.write_text("allow-rule STYLE-STRUCT-ARRAY-LENGTH-ORDER .*\n",
                          encoding="utf-8")
        self.check("""typedef struct Example {
    char *a;
    int32 na;
    char **b;
    int64 b_len;
} Example;
""", [], config=policy)


if __name__ == "__main__":
    unittest.main()
