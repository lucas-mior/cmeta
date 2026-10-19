// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck_regions.c"
#include "csanecheck_toplevel.c"
#include "csanecheck_statements.c"

static bool
check_source(char *source) {
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CsaneTopLevels top = {0};
    CsaneStatements statements = {0};
    bool ok = c_preprocessor_analysis_init(&pp, &tokens)
              && csane_regions_init(&regions, &tokens, &pp)
              && csane_toplevel_init(&top, &regions)
              && csane_statements_init(&statements, &top);
    int32 count_if = 0;
    int32 count_switch = 0;
    int32 count_case = 0;
    int32 count_branch = 0;
    int32 count_blocks = 0;
    int32 count_else = 0;
    int32 count_default = 0;
    int32 count_simple = 0;
    for (int32 i = 0; i < statements.count; i += 1) {
        CsaneStatement *item = &statements.items[i];
        if (item->kind == CSANE_STMT_IF) {
            count_if += 1;
            if (item->body < 0) {
                ok = false;
            }
        }
        if (item->kind == CSANE_STMT_SWITCH) {
            count_switch += 1;
        }
        if (item->kind == CSANE_STMT_CASE) {
            count_case += 1;
        }
        if (item->kind == CSANE_STMT_ELSE) {
            count_else += 1;
        }
        if (item->kind == CSANE_STMT_DEFAULT) {
            count_default += 1;
        }
        if (item->kind == CSANE_STMT_SIMPLE) {
            count_simple += 1;
        }
        if (item->kind == CSANE_STMT_BLOCK) {
            count_blocks += 1;
        }
        if (regions.items[item->owner_region].kind
            == CSANE_REGION_CONDITIONAL_BRANCH) {
            count_branch += 1;
        }
        if (item->source.start >= item->source.end) {
            ok = false;
        }
    }
    if (count_if != 2 || count_switch != 1 || count_case != 1
        || count_branch < 2 || count_blocks < 2
        || count_else != 1 || count_default != 1 || count_simple < 5) {
        ok = false;
    }
    csane_statements_free(&statements);
    csane_toplevel_free(&top);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return ok;
}

static bool
check_file(char *path) {
    char *source = NULL;
    int32 len = read_entire_file(path, &source);
    if (len < 0) {
        return false;
    }
    Tokenization tokens = tokenize(source, len);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CsaneTopLevels top = {0};
    CsaneStatements statements = {0};
    bool ok = c_preprocessor_analysis_init(&pp, &tokens)
              && csane_regions_init(&regions, &tokens, &pp)
              && csane_toplevel_init(&top, &regions)
              && csane_statements_init(&statements, &top);
    for (int32 i = 0; i < statements.count; i += 1) {
        CsaneStatement *item = &statements.items[i];
        if (item->function < 0 || item->function >= top.count
            || item->tokens.first >= item->tokens.end
            || item->source.start >= item->source.end
            || (item->parent >= 0 && item->parent >= i)) {
            ok = false;
        }
    }
    if (!ok) {
        fprintf(stderr, "statement structure failed: %s\n", path);
    }
    csane_statements_free(&statements);
    csane_toplevel_free(&top);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)len + 1);
    return ok;
}

int
main(int argc, char **argv) {
    char source[] =
        "int32 sample(int32 x) {\n"
        "if (x) { x += 1; } else if (x == 0) x = 1;\n"
        "switch (x) { case 1: x += 1; break; default: break; }\n"
        "#if FOO\n"
        "x = 2;\n"
        "#else\n"
        "x = 3;\n"
        "#endif\n"
        "return x;\n"
        "}\n";
    if (!check_source(source)) {
        fprintf(stderr, "sample statement check failed\n");
        return 1;
    }
    for (int32 i = 1; i < argc; i += 1) {
        if (!check_file(argv[i])) {
            return 2;
        }
    }
    return 0;
}
