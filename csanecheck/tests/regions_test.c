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
        error2("regions_test.c:%d: failed\n", line);
        failures += 1;
    }
    return;
}

#define CHECK(CONDITION) test_check((CONDITION), __LINE__)

static int32
test_offset(char *source, int32 source_len,
            char *fragment, int32 fragment_len) {
    char *found = memmem64(source, source_len, fragment, fragment_len);
    return found ? (int32)(found - source) : -1;
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

/* The index must match source-offset traversal at every token start. */
static void
test_token_index(CsaneRegions *regions) {
    Tokenization *tokens = regions->tokens;
    CHECK(csane_regions_at_token(NULL, 0) == -1);
    CHECK(csane_regions_at_token(regions, -1) == -1);
    CHECK(csane_regions_at_token(regions, tokens->token_count) == -1);
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        int32 expected = csane_regions_at_offset(regions,
                                                 tokens->tokens[i].offset);
        CHECK(csane_regions_at_token(regions, i) == expected);
    }
    return;
}

static int32
test_enclosing(CsaneRegions *regions,
               char *text, int32 text_len,
               char *fragment, int32 fragment_len,
               enum CsaneRegionKind kind) {
    int32 offset = test_offset(text, text_len, fragment, fragment_len);
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
    int32 text_len = strlen32(text);
    Tokenization tokens = tokenize_copy(text, text_len, TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    CHECK(regions.complete);
    CHECK(pp.group_count == 2);
    test_partition(&regions, 0);
    test_token_index(&regions);
    int32 first = test_enclosing(&regions, text, text_len,
                                 STRLIT("call_first()"),
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 second = test_enclosing(&regions, text, text_len,
                                  STRLIT("call_second()"),
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    int32 nested = test_enclosing(&regions, text, text_len,
                                  STRLIT("call_nested()"),
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    CHECK(first >= 0 && second >= 0 && nested >= 0);
    CHECK(first != second && first != nested && second != nested);
    CHECK(regions.items[nested].parent
          == test_enclosing(&regions, text, text_len, STRLIT("call_nested()"),
                            CSANE_REGION_CONDITIONAL_GROUP));
    CHECK(test_enclosing(&regions, text, text_len, STRLIT("AFTER()"),
                         CSANE_REGION_CONDITIONAL_BRANCH) == -1);
    int32 parameters = test_enclosing(&regions, text, text_len, STRLIT("x, y"),
                                      CSANE_REGION_MACRO_PARAMETERS);
    int32 replacement = test_enclosing(&regions, text, text_len,
                                       STRLIT("((x) +"),
                                       CSANE_REGION_MACRO_REPLACEMENT);
    CHECK(parameters >= 0 && replacement >= 0);
    CHECK(regions.items[parameters].source.end
          <= regions.items[replacement].source.start);
    CHECK(test_enclosing(&regions, text, text_len, STRLIT("(x)\n"),
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
    int32 text_len = strlen32(text);
    Tokenization tokens = tokenize_copy(text, text_len, TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(!c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(pp.issue_count >= 3);
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    CHECK(!regions.complete);
    test_partition(&regions, 0);
    test_token_index(&regions);
    int32 good = test_enclosing(&regions, text, text_len,
                                STRLIT("good_branch()"),
                                CSANE_REGION_CONDITIONAL_BRANCH);
    int32 alternative = test_enclosing(&regions, text, text_len,
                                       STRLIT("good_alternative()"),
                                       CSANE_REGION_CONDITIONAL_BRANCH);
    int32 other = test_enclosing(&regions, text, text_len,
                                 STRLIT("other_branch()"),
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 nested = test_enclosing(&regions, text, text_len,
                                  STRLIT("nested_branch()"),
                                  CSANE_REGION_CONDITIONAL_BRANCH);
    int32 open = test_enclosing(&regions, text, text_len,
                                STRLIT("unclosed_branch()"),
                                CSANE_REGION_CONDITIONAL_GROUP);
    CHECK(good >= 0 && alternative >= 0 && good != alternative);
    CHECK(other >= 0 && other != good);
    CHECK(nested >= 0 && open >= 0 && regions.items[nested].complete);
    CHECK(!regions.items[open].complete);
    int32 broken = test_enclosing(&regions, text, text_len, STRLIT("BROKEN(x"),
                                  CSANE_REGION_MACRO_DEFINE);
    CHECK(broken >= 0 && !regions.items[broken].complete);
    CHECK(test_enclosing(&regions, text, text_len, STRLIT("BROKEN(x"),
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
    int32 text_len = strlen32(text);
    Tokenization tokens = tokenize_copy(text, text_len, TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(!c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    test_partition(&regions, 0);
    test_token_index(&regions);
    int32 first = test_enclosing(&regions, text, text_len, STRLIT("first()"),
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 next = test_enclosing(&regions, text, text_len, STRLIT("next()"),
                                CSANE_REGION_CONDITIONAL_BRANCH);
    CHECK(first >= 0 && next >= 0 && first != next);
    int32 malformed = test_enclosing(&regions, text, text_len,
                                     STRLIT("#elif BAD"),
                                     CSANE_REGION_DIRECTIVE);
    CHECK(malformed >= 0 && !regions.items[malformed].complete);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return;
}

static void
test_empty_source(void) {
    Tokenization tokens = tokenize_copy(NULL, 0, TOKENIZE_DEFAULT);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CHECK(c_preprocessor_analysis_init(&pp, &tokens));
    CHECK(csane_regions_init(&regions, &tokens, &pp));
    CHECK(regions.token_regions == NULL);
    CHECK(csane_regions_at_token(&regions, 0) == -1);
    test_token_index(&regions);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return;
}

static void
test_bounded_fragments(void) {
    char source[] = {'a', 'b', 'c', 'd', 'a', 'b', 'c',};
    char fragment[] = {'b', 'c', 'X'};
    CHECK(test_offset(source, SIZEOF(source), fragment, 2) == 1);
    CHECK(test_offset(source, SIZEOF(source), fragment, 3) == -1);
    CHECK(test_offset(source, 2, fragment, 2) == -1);
    CHECK(test_offset(source, SIZEOF(source), STRLIT("dabc")) == 3);
    return;
}

int
main(void) {
    test_bounded_fragments();
    test_empty_source();
    test_complete_groups();
    test_recovery();
    test_duplicate_branch_directives();
    return failures > 0;
}
