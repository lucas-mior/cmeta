// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck/csck_regions.c"

static int32 failures = 0;

static void
test_check(bool condition, int32 line) {
    if (!condition) {
        fprintf(stderr, "regions_test.c:%d: failed\n", line);
        failures += 1;
    }
    return;
}

#define CHECK(CONDITION) test_check((CONDITION), __LINE__)

static int32
test_offset(char *source, char *fragment) {
    int32 source_len = strlen32(source);
    int32 len = strlen32(fragment);
    for (int32 i = 0; i + len <= source_len; i += 1) {
        if (!memcmp64(source + i, fragment, len)) {
            return i;
        }
    }
    return -1;
}

/* Every source byte belongs to exactly one child at each tree level. */
static void
test_partition(CsaneRegions *regions, int32 parent) {
    CsaneSourceRegion *item = &regions->items[parent];
    int32 child = item->first_child;
    int32 cursor = item->source.start;
    while (child >= 0) {
        CsaneSourceRegion *part = &regions->items[child];
        CHECK(part->parent == parent);
        CHECK(part->source.start == cursor);
        CHECK(part->source.end > part->source.start);
        CHECK(part->source.end <= item->source.end);
        CHECK(part->tokens.first <= part->tokens.end);
        test_partition(regions, child);
        cursor = part->source.end;
        child = part->next_sibling;
    }
    if (item->first_child >= 0) {
        CHECK(cursor == item->source.end);
    }
    return;
}

static int32
test_enclosing(CsaneRegions *regions, char *text, char *fragment,
               enum CsaneRegionKind kind) {
    int32 offset = test_offset(text, fragment);
    CHECK(offset >= 0);
    if (offset < 0) {
        return -1;
    }
    int32 leaf = csane_regions_at_offset(regions, offset);
    return csane_regions_enclosing(regions, leaf, kind);
}

static void
test_complete_groups(void) {
    char *text =
        "void f(void) {\n"
        "#if FIRST\n"
        "    call_first();\n"
        "#if NESTED\n"
        "    call_nested();\n"
        "#else\n"
        "    call_nested_alternative();\n"
        "#endif\n"
        "#elif SECOND\n"
        "    call_second();\n"
        "#else\n"
        "    call_fallback();\n"
        "#endif\n"
        "    AFTER();\n"
        "}\n"
        "#define WRAP(x, y) ((x) + \\\n"
        "                    (y))\n"
        "#define ID(x) (x)\n";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    CHECK(regions.complete);
    CHECK(pp.group_count == 2);
    test_partition(&regions, 0);
    int32 first = test_enclosing(&regions, text, "call_first()",
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 second = test_enclosing(&regions, text, "call_second()",
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    int32 nested = test_enclosing(&regions, text, "call_nested()",
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    CHECK(first >= 0 && second >= 0 && nested >= 0);
    CHECK(first != second && first != nested && second != nested);
    CHECK(regions.items[nested].parent
          == test_enclosing(&regions, text, "call_nested()",
                            CSANE_REGION_CONDITIONAL_GROUP));
    CHECK(test_enclosing(&regions, text, "AFTER()",
                         CSANE_REGION_CONDITIONAL_BRANCH) == -1);
    int32 parameters = test_enclosing(&regions, text, "x, y",
                                      CSANE_REGION_MACRO_PARAMETERS);
    int32 replacement = test_enclosing(&regions, text, "((x) +",
                                       CSANE_REGION_MACRO_REPLACEMENT);
    CHECK(parameters >= 0 && replacement >= 0);
    CHECK(regions.items[parameters].source.end
          <= regions.items[replacement].source.start);
    CHECK(test_enclosing(&regions, text, "(x)\n",
                         CSANE_REGION_MACRO_REPLACEMENT) >= 0);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return;
}

static void
test_recovery(void) {
    char *text =
        "#endif\n"
        "#if GOOD\n"
        "good_branch();\n"
        "#else\n"
        "good_alternative();\n"
        "#endif\n"
        "#define BROKEN(x, y\n"
        "#if OTHER\n"
        "other_branch();\n"
        "#else\n"
        "other_alternative();\n"
        "#endif\n"
        "#if UNCLOSED\n"
        "unclosed_branch();\n"
        "#if NESTED\n"
        "nested_branch();\n"
        "#endif\n";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(!c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(pp.issue_count >= 3);
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    CHECK(!regions.complete);
    test_partition(&regions, 0);
    int32 good = test_enclosing(&regions, text, "good_branch()",
                                CSANE_REGION_CONDITIONAL_BRANCH);
    int32 alternative = test_enclosing(&regions, text,
                                       "good_alternative()",
                                       CSANE_REGION_CONDITIONAL_BRANCH);
    int32 other = test_enclosing(&regions, text, "other_branch()",
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 nested = test_enclosing(&regions, text, "nested_branch()",
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    int32 open = test_enclosing(&regions, text, "unclosed_branch()",
                                CSANE_REGION_CONDITIONAL_GROUP);
    CHECK(good >= 0 && alternative >= 0 && good != alternative);
    CHECK(other >= 0 && other != good);
    CHECK(nested >= 0 && open >= 0 && regions.items[nested].complete);
    CHECK(!regions.items[open].complete);
    int32 broken = test_enclosing(&regions, text, "BROKEN(x",
                                  CSANE_REGION_MACRO_DEFINE);
    CHECK(broken >= 0 && !regions.items[broken].complete);
    CHECK(test_enclosing(&regions, text, "BROKEN(x",
                         CSANE_REGION_MACRO_REPLACEMENT) == -1);
    CHECK(!regions.items[csane_regions_at_offset(&regions, 0)].complete);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return;
}

static void
test_duplicate_branch_directives(void) {
    char *text =
        "#if FIRST\n"
        "first();\n"
        "#else\n"
        "second();\n"
        "#elif BAD\n"
        "third();\n"
        "#else\n"
        "fourth();\n"
        "#endif\n"
        "#if NEXT\n"
        "next();\n"
        "#endif\n";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(!c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    test_partition(&regions, 0);
    int32 first = test_enclosing(&regions, text, "first()",
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 next = test_enclosing(&regions, text, "next()",
                                CSANE_REGION_CONDITIONAL_BRANCH);
    CHECK(first >= 0 && next >= 0 && first != next);
    int32 malformed = test_enclosing(&regions, text, "#elif BAD",
                                     CSANE_REGION_DIRECTIVE);
    CHECK(malformed >= 0 && !regions.items[malformed].complete);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return;
}

int
main(void) {
    test_complete_groups();
    test_recovery();
    test_duplicate_branch_directives();
    return failures > 0;
}
