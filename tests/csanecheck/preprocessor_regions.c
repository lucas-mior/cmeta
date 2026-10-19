// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck_regions.c"

static bool
check_partition(CsaneRegions *regions, int32 parent) {
    CsaneSourceRegion *region = &regions->items[parent];
    int32 cursor = region->source.start;
    for (int32 child = region->first_child; child >= 0;
         child = regions->items[child].next_sibling) {
        CsaneSourceRegion *item = &regions->items[child];
        if ((item->parent != parent) || (item->source.start != cursor)
            || (item->source.end <= cursor)
            || (item->source.end > region->source.end)
            || !check_partition(regions, child)) {
            return false;
        }
        cursor = item->source.end;
    }
    return region->first_child < 0 || cursor == region->source.end;
}

static int32
source_region(CsaneRegions *regions, char *needle,
              enum CsaneRegionKind kind) {
    int32 len = strlen32(needle);
    Tokenization *tokens = regions->tokens;
    for (int32 at = 0; at + len <= tokens->text_len; at += 1) {
        if (memcmp64(tokens->text + at, needle, len) != 0) {
            continue;
        }
        int32 index = csane_regions_at_offset(regions, at);
        return csane_regions_enclosing(regions, index, kind);
    }
    return -1;
}

static bool
check_tokens(CsaneRegions *regions) {
    Tokenization *tokens = regions->tokens;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        int32 region = csane_regions_at_token(regions, i);
        if (region < 0) {
            return false;
        }
        SourceRange source = regions->items[region].source;
        enum CPreprocessorTokenRole role =
            regions->preprocessor->token_roles[i];
        if ((role == C_PP_TOKEN_MACRO_PARAMETERS
             && csane_regions_enclosing(regions, region,
                                        CSANE_REGION_MACRO_PARAMETERS) < 0)
            || (role == C_PP_TOKEN_MACRO_REPLACEMENT
                && csane_regions_enclosing(regions, region,
                                           CSANE_REGION_MACRO_REPLACEMENT)
                   < 0)
            || (role == C_PP_TOKEN_CODE
                && regions->items[region].kind != CSANE_REGION_CODE)) {
            return false;
        }
        if (tokens->tokens[i].offset < source.start
            || tokens->tokens[i].offset >= source.end) {
            return false;
        }
    }
    return true;
}

static int
check_sample(void) {
    char source[] =
        "int32 before;\n"
        "#define OBJ (alpha + beta) \\\n"
        "    + gamma\n"
        "#define FUNC(x, y) \\\n"
        "    ((x) + (y))\n"
        "#if ENABLE\n"
        "int32 active;\n"
        "#if SUB\n"
        "typedef int32 Inner;\n"
        "#else\n"
        "int32 other;\n"
        "#endif\n"
        "#elif SECOND\n"
        "int32 second;\n"
        "#else\n"
        "int32 third;\n"
        "#endif\n"
        "#if 0\n"
        "extern \"C\" {\n"
        "#else\n"
        "float f;\n"
        "#endif\n"
        "int32 after;\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    int result = 0;
    if (!c_preprocessor_analysis_init(&pp, &tokens)
        || !csane_regions_init(&regions, &tokens, &pp)) {
        result = 1;
        goto done;
    }
    if (!regions.complete || pp.group_count != 3 || pp.branch_count != 7
        || !check_partition(&regions, 0) || !check_tokens(&regions)) {
        result = 2;
        goto done;
    }
    for (int32 offset = 0; offset < tokens.text_len; offset += 1) {
        if (csane_regions_at_offset(&regions, offset) < 0) {
            result = 3;
            goto done;
        }
    }
    if (source_region(&regions, "OBJ", CSANE_REGION_MACRO_DEFINE) < 0
        || source_region(&regions, "gamma", CSANE_REGION_MACRO_REPLACEMENT)
           < 0
        || source_region(&regions, "x, y", CSANE_REGION_MACRO_PARAMETERS) < 0
        || source_region(&regions, "((x)", CSANE_REGION_MACRO_REPLACEMENT)
           < 0
        || source_region(&regions, "int32 before", CSANE_REGION_CODE) < 0
        || source_region(&regions, "int32 after", CSANE_REGION_CODE) < 0) {
        result = 4;
        goto done;
    }
    int32 active = source_region(&regions, "int32 active",
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 other = source_region(&regions, "int32 other",
                                CSANE_REGION_CONDITIONAL_BRANCH);
    int32 second = source_region(&regions, "int32 second",
                                 CSANE_REGION_CONDITIONAL_BRANCH);
    int32 third = source_region(&regions, "int32 third",
                                CSANE_REGION_CONDITIONAL_BRANCH);
    int32 inner = source_region(&regions, "Inner",
                                CSANE_REGION_CONDITIONAL_GROUP);
    if (active < 0 || other < 0 || second < 0 || third < 0 || inner < 0
        || active == other || active == second || second == third
        || regions.items[inner].parent != active
        || regions.items[other].parent != inner
        || source_region(&regions, "extern \"C\"",
                         CSANE_REGION_CONDITIONAL_BRANCH) < 0) {
        result = 5;
        goto done;
    }
    if (csane_regions_at_offset(&regions, -1) != -1
        || csane_regions_at_offset(&regions, tokens.text_len) != -1
        || csane_regions_at_token(&regions, tokens.token_count) != -1
        || csane_regions_enclosing(&regions, -1, CSANE_REGION_FILE) != -1) {
        result = 6;
    }
done:
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return result;
}

static int
check_bad_preprocessor(void) {
    char source[] = "int32 a;\n#if CONDITION\nint32 b;\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    bool ok = c_preprocessor_analysis_init(&pp, &tokens);
    bool initialized = csane_regions_init(&regions, &tokens, &pp);
    int result = 0;
    if (ok || !initialized || regions.complete || !check_partition(&regions, 0)
        || !check_tokens(&regions)
        || source_region(&regions, "int32 b", CSANE_REGION_CODE) < 0) {
        result = 1;
    }
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return result;
}

static int
check_project_file(char *filename) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    if (len < 0) {
        return 1;
    }
    Tokenization tokens = tokenize(text, len);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    bool ok = c_preprocessor_analysis_init(&pp, &tokens);
    bool initialized = csane_regions_init(&regions, &tokens, &pp);
    int result = (!ok || !initialized || !regions.complete
                  || !check_partition(&regions, 0)
                  || !check_tokens(&regions));
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(text, (int64)len + 1);
    return result;
}

int
main(int argc, char **argv) {
    if (check_sample() != 0 || check_bad_preprocessor() != 0) {
        return 1;
    }
    for (int32 i = 1; i < argc; i += 1) {
        if (check_project_file(argv[i]) != 0) {
            fprintf(stderr, "regions check failed: %s\n", argv[i]);
            return 2;
        }
    }
    return 0;
}
