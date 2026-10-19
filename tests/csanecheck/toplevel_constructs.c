// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck_regions.c"
#include "csanecheck_toplevel.c"

static int32
count_kind(CsaneTopLevels *top, enum CsaneTopKind kind) {
    int32 count = 0;
    for (int32 i = 0; i < top->count; i += 1) {
        if (top->items[i].kind == kind) {
            count += 1;
        }
    }
    return count;
}

static bool
has_item(CsaneTopLevels *top, enum CsaneTopKind kind, char *text) {
    int32 text_len = strlen32(text);
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->kind != kind) {
            continue;
        }
        SourceRange span = item->source;
        Tokenization *tokens = top->regions->tokens;
        for (int32 offset = span.start; offset + text_len <= span.end;
             offset += 1) {
            if (memcmp64(tokens->text + offset, text, text_len) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* Every significant code token must belong to an item owned by its nearest
 * file or conditional-branch region. Preprocessor tokens are never code. */
static bool
check_partition(CsaneRegions *regions, CsaneTopLevels *top) {
    Tokenization *tokens = regions->tokens;
    for (int32 r = 0; r < regions->count; r += 1) {
        CsaneSourceRegion *region = &regions->items[r];
        if (region->kind != CSANE_REGION_CODE) {
            continue;
        }
        int32 owner = region->parent;
        for (int32 i = region->tokens.first; i < region->tokens.end;
             i += 1) {
            Token *token = &tokens->tokens[i];
            if (token_is_trivia(token) || token->kind == TOKEN_PREPROC
                || token->offset < region->source.start
                || token->offset >= region->source.end) {
                continue;
            }
            int32 item = csane_toplevel_at_token(top, owner, i);
            if (item < 0) {
                return false;
            }
            CsaneTopItem *construct = &top->items[item];
            if (construct->source.start > token->offset
                || construct->source.end < token->offset + token->len) {
                return false;
            }
        }
    }
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->kind != CSANE_TOP_FUNCTION_DEFINITION) {
            continue;
        }
        if (item->name_token < 0 || item->body.end <= item->body.first
            || item->header.end != item->body.first
            || !TOKEN_IS(&tokens->tokens[item->body.first], "{")
            || !TOKEN_IS(&tokens->tokens[item->body.end - 1], "}")) {
            return false;
        }
    }
    return true;
}

static int32
check_sample(void) {
    char source[] =
        "typedef struct Thing { int32 x; } Thing;\n"
        "static const Thing array[] = {{1}, {2}};\n"
        "int32 before(int32 a);\n"
        "int32 (*callback)(int32);\n"
        "CALL_DECLARE(Thing)\n"
        "CALL_DECLARE(Other)\n"
        "static int32\n"
        "CAT(build_, KIND)(int32 x) {\n"
        "    return x;\n"
        "}\n"
        "#if A\n"
        "extern int32 alternative(int32 x);\n"
        "#else\n"
        "typedef int32 Alternate;\n"
        "#endif\n"
        "static int32\n"
        "has_branch(void) {\n"
        "#if CONDITION\n"
        "    int32 x = 1;\n"
        "#else\n"
        "    int32 x = 2;\n"
        "#endif\n"
        "    return x;\n"
        "}\n"
        "#if defined(__cplusplus)\n"
        "extern \"C\" {\n"
        "#endif\n"
        "int32 last(int32 a);\n"
        "#if BROKEN\n"
        "int32 incomplete(\n"
        "#else\n"
        "int32 safe(void);\n"
        "#endif\n"
        "int32 final(void);\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CsaneTopLevels top = {0};
    int32 result = 0;
    if (!c_preprocessor_analysis_init(&pp, &tokens)
        || !csane_regions_init(&regions, &tokens, &pp)
        || !csane_toplevel_init(&top, &regions)) {
        result = 1;
        goto done;
    }
    if (!check_partition(&regions, &top)
        || count_kind(&top, CSANE_TOP_FUNCTION_DEFINITION) != 2
        || !has_item(&top, CSANE_TOP_FUNCTION_DEFINITION, "build_")
        || !has_item(&top, CSANE_TOP_FUNCTION_DEFINITION, "has_branch")
        || !has_item(&top, CSANE_TOP_TYPE_DECLARATION, "Thing;")
        || !has_item(&top, CSANE_TOP_DECLARATION, "array[]")
        || !has_item(&top, CSANE_TOP_FUNCTION_DECLARATION, "before(")
        || !has_item(&top, CSANE_TOP_FUNCTION_DECLARATION, "last(")
        || !has_item(&top, CSANE_TOP_FUNCTION_DECLARATION, "safe(")
        || !has_item(&top, CSANE_TOP_FUNCTION_DECLARATION, "final(")
        || !has_item(&top, CSANE_TOP_OPAQUE, "incomplete(")
        || !has_item(&top, CSANE_TOP_FUNCTION_DECLARATION, "alternative(")
        || !has_item(&top, CSANE_TOP_DECLARATION, "callback")
        || count_kind(&top, CSANE_TOP_MACRO_INVOCATION) != 2
        || !has_item(&top, CSANE_TOP_TYPE_DECLARATION, "Alternate")) {
        result = 2;
    }
done:
    csane_toplevel_free(&top);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    return result;
}

static int32
check_file(char *path) {
    char *source = NULL;
    int32 len = read_entire_file(path, &source);
    if (len < 0) {
        return 1;
    }
    Tokenization tokens = tokenize(source, len);
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CsaneTopLevels top = {0};
    bool ok = c_preprocessor_analysis_init(&pp, &tokens)
              && csane_regions_init(&regions, &tokens, &pp)
              && csane_toplevel_init(&top, &regions);
    int32 result = !ok || !check_partition(&regions, &top);
    if (ENDS_WITH(path, strlen32(path), "cbase/hash.c")
        && (count_kind(&top, CSANE_TOP_FUNCTION_DEFINITION) < 20
            || !has_item(&top, CSANE_TOP_FUNCTION_DEFINITION,
                         "hash_probe_"))) {
        result = 1;
    }
    if (ENDS_WITH(path, strlen32(path), "cbase/rw_function.h")
        && count_kind(&top, CSANE_TOP_FUNCTION_DEFINITION) != 2) {
        result = 1;
    }
    if (ENDS_WITH(path, strlen32(path), "cbase/assertions.h")
        && count_kind(&top, CSANE_TOP_MACRO_INVOCATION) < 20) {
        result = 1;
    }
    if (result) {
        fprintf(stderr, "top-level partition failed: %s\n", path);
    }
    csane_toplevel_free(&top);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)len + 1);
    return result;
}

int
main(int argc, char **argv) {
    int32 result = check_sample();
    if (result != 0) {
        fprintf(stderr, "sample top-level check failed: %d\n", result);
        return 1;
    }
    for (int32 i = 1; i < argc; i += 1) {
        if (check_file(argv[i]) != 0) {
            return 2;
        }
    }
    return 0;
}
