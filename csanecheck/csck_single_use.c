// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_SINGLE_USE_C)
#define CSANECHECK_SINGLE_USE_C

#include "cbase.h"
#include "csck.h"

/* Ignore conditional compilation except for a conventional, whole-file
 * include guard. No alternative preprocessor branch is selected. */
static bool
csane_single_use_code(CsaneFile *file, int32 index, int32 guard_branch) {
    Tokenization *tokens = file->tokens;
    CPreprocessorAnalysis *pp = file->preprocessor;
    char *testing_prefix = "TESTING_";
    int32 testing_prefix_len = strlen32(testing_prefix);
    int32 region = csane_regions_at_token(file->regions, index);
    int32 branch;

    /* Test-only functions are permitted to have a single call. Exclude
     * their definitions and call sites, even inside a file include guard. */
    for (int32 current = pp->token_branches[index]; current >= 0;
         current = pp->branches[current].parent_branch) {
        CPreprocessorBranch *entry = &pp->branches[current];
        CPreprocessorDirective *directive =
            &pp->directives[entry->directive_index];
        Token *name = NULL;
        int32 macro;

        if (directive->kind != C_PREPROCESSOR_DIRECTIVE_IF
            && directive->kind != C_PREPROCESSOR_DIRECTIVE_IFDEF
            && directive->kind != C_PREPROCESSOR_DIRECTIVE_ELIF) {
            continue;
        }
        macro = token_range_next_significant(tokens, directive->tokens,
                                             directive->keyword_token);
        if (macro >= directive->tokens.end) {
            continue;
        }
        if (TOKEN_IS(&tokens->tokens[macro], "defined")) {
            macro = token_range_next_significant(tokens, directive->tokens,
                                                 macro);
            if (macro < directive->tokens.end
                && TOKEN_IS(&tokens->tokens[macro], "(")) {
                macro = token_range_next_significant(tokens,
                                                     directive->tokens,
                                                     macro);
            }
        }
        if (macro >= directive->tokens.end) {
            continue;
        }
        name = &tokens->tokens[macro];
        if (name->kind == TOKEN_IDENT
            && ((name->len >= testing_prefix_len
                 && !memcmp64(name->text,
                              testing_prefix, testing_prefix_len))
                || TOKEN_IS(name, "TESTING"))) {
            return false;
        }
    }
    if (region < 0
        || file->regions->items[region].kind != CSANE_REGION_CODE
        || !file->regions->items[region].complete) {
        return false;
    }
    branch = csane_regions_enclosing(file->regions, region,
                                     CSANE_REGION_CONDITIONAL_BRANCH);
    return branch < 0 || branch == guard_branch;
}

/* Count indentation at the containing statement, not at a wrapped call
 * argument, whose alignment can be much farther to the right. */
static bool
csane_single_use_deep_call(CsaneFile *file, int32 function, int32 token) {
    int32 statement = csane_statements_at_token(file->statements, function,
                                                token);
    int32 offset = file->tokens->tokens[token].offset;
    char *text = file->tokens->text;
    int32 start;
    int32 indentation = 0;

    if (statement >= 0) {
        offset = file->statements->items[statement].source.start;
    }
    start = offset;
    while (start > 0 && text[start - 1] != '\n') {
        start -= 1;
    }
    while (start + indentation < offset && text[start + indentation] == ' ') {
        indentation += 1;
    }
    return indentation >= 12;
}

void
csane_single_use_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    CPreprocessorAnalysis *pp = file->preprocessor;
    int32 guard_branch = -1;

    if (!ENDS_WITH(file->path, file->path_len, ".c") || file->is_ryu
        || !file->preprocessor_ok) {
        return;
    }
    /* A source file may be wrapped in #if !defined(NAME) / #define NAME.
     * Recognize only a matching single-branch guard covering the file. */
    for (int32 i = 0; i < ARRAY_LEN(pp->groups); i += 1) {
        CPreprocessorGroup *group = &pp->groups[i];
        CPreprocessorBranch *branch = NULL;
        CPreprocessorDirective *opening = NULL;
        CPreprocessorDirective *defining = NULL;
        CPreprocessorDefine define;
        int32 pos;
        int32 macro;
        bool whole = true;

        if (group->parent_branch >= 0
            || group->first_branch != group->last_branch
            || group->first_branch < 0 || group->endif_directive < 0) {
            continue;
        }
        for (int32 j = 0; j < ARRAY_LEN(tokens->tokens); j += 1) {
            Token *token = &tokens->tokens[j];
            if ((token->offset < group->source.start
                 || token->offset >= group->source.end)
                && !token_is_trivia(token)) {
                whole = false;
                break;
            }
        }
        if (!whole) {
            continue;
        }
        branch = &pp->branches[group->first_branch];
        opening = &pp->directives[branch->directive_index];
        if (opening->kind != C_PREPROCESSOR_DIRECTIVE_IF) {
            continue;
        }
        pos = token_range_next_significant(tokens, opening->tokens,
                                           opening->keyword_token);
        if (pos >= opening->tokens.end
            || !TOKEN_IS(&tokens->tokens[pos], "!")) {
            continue;
        }
        pos = token_range_next_significant(tokens, opening->tokens, pos);
        if (pos >= opening->tokens.end
            || !TOKEN_IS(&tokens->tokens[pos], "defined")) {
            continue;
        }
        pos = token_range_next_significant(tokens, opening->tokens, pos);
        if (pos >= opening->tokens.end
            || !TOKEN_IS(&tokens->tokens[pos], "(")) {
            continue;
        }
        macro = token_range_next_significant(tokens, opening->tokens, pos);
        if (macro >= opening->tokens.end
            || tokens->tokens[macro].kind != TOKEN_IDENT) {
            continue;
        }
        pos = token_range_next_significant(tokens, opening->tokens, macro);
        if (pos >= opening->tokens.end
            || !TOKEN_IS(&tokens->tokens[pos], ")")) {
            continue;
        }
        pos = token_range_next_significant(tokens, opening->tokens, pos);
        if (pos < opening->tokens.end) {
            continue;
        }
        defining = NULL;
        for (int32 j = branch->directive_index + 1;
             j < ARRAY_LEN(pp->directives); j += 1) {
            CPreprocessorDirective *candidate = &pp->directives[j];
            if (candidate->source.start >= branch->body_source.end) {
                break;
            }
            defining = candidate;
            break;
        }
        if (defining == NULL
            || defining->kind != C_PREPROCESSOR_DIRECTIVE_DEFINE
            || !c_preprocessor_define_info(tokens, defining, &define)
            || define.name_token < 0
            || tokens->tokens[define.name_token].len
               != tokens->tokens[macro].len
            || memcmp64(tokens->tokens[define.name_token].text,
                        tokens->tokens[macro].text,
                        tokens->tokens[macro].len)) {
            continue;
        }
        for (int32 j = 0; j < ARRAY_LEN(file->regions->items); j += 1) {
            CsaneSourceRegion *region = &file->regions->items[j];
            if (region->kind == CSANE_REGION_CONDITIONAL_BRANCH
                && region->branch_index == group->first_branch) {
                guard_branch = j;
                break;
            }
        }
        break;
    }

    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        Token *name = NULL;
        bool is_static = false;
        bool uncertain = false;
        bool deep_call = false;
        bool self_recursive = false;
        int32 count = 0;

        if (item->kind != CSANE_TOP_FUNCTION_DEFINITION
            || item->name_token < 0
            || !csane_single_use_code(file, item->name_token,
                                      guard_branch)) {
            continue;
        }
        for (int32 j = item->header.first; j < item->name_token; j += 1) {
            if (TOKEN_IS(&tokens->tokens[j], "static")) {
                is_static = true;
                break;
            }
        }
        if (!is_static) {
            continue;
        }
        name = &tokens->tokens[item->name_token];
        for (int32 j = 0; j < ARRAY_LEN(tokens->tokens); j += 1) {
            Token *token = &tokens->tokens[j];
            bool prototype = false;
            bool direct = false;
            int32 previous;
            int32 next;

            if (j == item->name_token || token->kind != TOKEN_IDENT
                || token->len != name->len
                || memcmp64(token->text, name->text, name->len)) {
                continue;
            }
            if (!csane_single_use_code(file, j, guard_branch)) {
                uncertain = true;
                break;
            }
            for (int32 k = 0; k < ARRAY_LEN(file->toplevel->items); k += 1) {
                CsaneTopItem *candidate = &file->toplevel->items[k];
                if (candidate->kind == CSANE_TOP_FUNCTION_DECLARATION
                    && candidate->name_token == j) {
                    prototype = true;
                    break;
                }
            }
            if (prototype) {
                continue;
            }
            next = tokenization_next_significant(tokens, j);
            previous = tokenization_previous_significant(tokens, j);
            if (next < ARRAY_LEN(tokens->tokens)
                && TOKEN_IS(&tokens->tokens[next], "(")
                && (previous < 0
                    || (!TOKEN_IS(&tokens->tokens[previous], ".")
                        && !TOKEN_IS(&tokens->tokens[previous], "->")
                        && !TOKEN_IS(&tokens->tokens[previous], "&")))) {
                for (int32 k = 0; k < ARRAY_LEN(file->toplevel->items);
                     k += 1) {
                    CsaneTopItem *caller = &file->toplevel->items[k];
                    if (caller->kind == CSANE_TOP_FUNCTION_DEFINITION
                        && j >= caller->body.first
                        && j < caller->body.end) {
                        direct = true;
                        if (caller == item) {
                            self_recursive = true;
                        }
                        deep_call = csane_single_use_deep_call(file, k, j);
                        break;
                    }
                }
            }
            if (!direct) {
                uncertain = true;
                break;
            }
            count += 1;
            if (count > 1) {
                break;
            }
        }
        if (count <= 1 && !uncertain && !deep_call) {
            char *format;
            int64 capacity;
            char *message;

            if (self_recursive) {
                format = "static function '%.*s' calls only itself: "
                         "remove the function definition and any prototype";
            } else if (count == 0) {
                format = "static function '%.*s' never called: remove the "
                         "function definition and any prototype";
            } else {
                format = "static function '%.*s' called only once: inline "
                         "its body at the call site and remove the function "
                         "definition and any prototype";
            }
            capacity = strlen32(format) + name->len + 1;
            message = malloc2(capacity);
            fmt_sprintf(message, capacity, format, name->len, name->text);
            csane_rule_issue(file, name->offset, STRLIT("STYLE-STATIC-ONCE"),
                             message);
            free2(message, capacity);
        }
    }
    return;
}

#endif /* CSANECHECK_SINGLE_USE_C */
