// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_SINGLE_USE_C)
#define CSANECHECK_SINGLE_USE_C

#include "cbase.h"
#include "csck.h"

/* Conditional branches are code, including calls in testing branches.
 * Only testing-only function definitions receive the exemption. */
static bool
csane_single_use_code(CsaneFile *file, int32 index, bool definition) {
    Tokenization *tokens = file->tokens;
    CPreprocessorAnalysis *pp = file->preprocessor;
    char *testing_prefix = "TESTING_";
    int32 prefix_len = strlen32(testing_prefix);
    int32 region = csane_regions_at_token(file->regions, index);

    if (region < 0
        || file->regions->items[region].kind != CSANE_REGION_CODE
        || !file->regions->items[region].complete) {
        return false;
    }
    if (!definition) {
        return true;
    }
    for (int32 current = pp->token_branches[index]; current >= 0;
         current = pp->branches[current].parent_branch) {
        CPreprocessorBranch *branch = &pp->branches[current];
        CPreprocessorDirective *directive =
            &pp->directives[branch->directive_index];
        bool only_testing = true;
        bool has_testing = false;

        if (directive->kind != C_PREPROCESSOR_DIRECTIVE_IF
            && directive->kind != C_PREPROCESSOR_DIRECTIVE_IFDEF
            && directive->kind != C_PREPROCESSOR_DIRECTIVE_ELIF) {
            continue;
        }
        /* A disjunction of positive testing flags is also testing-only. */
        for (int32 j = directive->keyword_token + 1;
             j < directive->tokens.end; j += 1) {
            Token *part = &tokens->tokens[j];

            if (token_is_trivia(part) || TOKEN_IS(part, "defined")
                || TOKEN_IS(part, "(") || TOKEN_IS(part, ")")
                || TOKEN_IS(part, "&&") || TOKEN_IS(part, "||")) {
                continue;
            }
            if (part->kind == TOKEN_IDENT
                && ((part->len >= prefix_len
                     && !memcmp64(part->text, testing_prefix, prefix_len))
                    || TOKEN_IS(part, "TESTING"))) {
                has_testing = true;
                continue;
            }
            only_testing = false;
            break;
        }
        if (only_testing && has_testing) {
            return false;
        }
        for (int32 j = directive->keyword_token + 1;
             j < directive->tokens.end; j += 1) {
            Token *token = &tokens->tokens[j];
            int32 testing_depth = 0;
            int32 depth = 0;
            bool negative = false;
            bool alternative = false;

            if (token->kind != TOKEN_IDENT
                || !((token->len >= prefix_len
                      && !memcmp64(token->text, testing_prefix, prefix_len))
                     || TOKEN_IS(token, "TESTING"))) {
                continue;
            }
            for (int32 k = directive->keyword_token + 1; k < j; k += 1) {
                if (TOKEN_IS(&tokens->tokens[k], "(")) {
                    testing_depth += 1;
                } else if (TOKEN_IS(&tokens->tokens[k], ")")) {
                    testing_depth -= 1;
                }
            }
            /* A disjunction encompassing TESTING_ can select normal code.
             * Disjunctions in unrelated conjuncts do not affect it. */
            for (int32 k = directive->keyword_token + 1;
                 k < directive->tokens.end; k += 1) {
                Token *part = &tokens->tokens[k];
                int32 between;
                bool unrelated = false;

                if (TOKEN_IS(part, "(")) {
                    depth += 1;
                } else if (TOKEN_IS(part, ")")) {
                    depth -= 1;
                }
                if (!TOKEN_IS(part, "||") || depth > testing_depth) {
                    continue;
                }
                between = k < j ? depth : testing_depth;
                for (int32 m = (k < j ? k : j) + 1;
                     m < (k > j ? k : j); m += 1) {
                    if (TOKEN_IS(&tokens->tokens[m], "(")) {
                        between += 1;
                    } else if (TOKEN_IS(&tokens->tokens[m], ")")) {
                        between -= 1;
                    }
                    if (between < depth) {
                        unrelated = true;
                        break;
                    }
                }
                if (!unrelated) {
                    alternative = true;
                    break;
                }
            }
            /* Negation of the name or a containing expression can make
             * the condition true without the testing macro. */
            for (int32 k = directive->keyword_token + 1; k < j; k += 1) {
                int32 next;
                int32 nesting;

                if (!TOKEN_IS(&tokens->tokens[k], "!")) {
                    continue;
                }
                next = tokenization_next_significant(tokens, k);
                if (next == j) {
                    negative = true;
                    break;
                }
                if (TOKEN_IS(&tokens->tokens[next], "defined")) {
                    next = tokenization_next_significant(tokens, next);
                    if (next == j) {
                        negative = true;
                        break;
                    }
                }
                if (!TOKEN_IS(&tokens->tokens[next], "(")) {
                    continue;
                }
                nesting = 1;
                for (int32 m = next + 1; m < j; m += 1) {
                    if (TOKEN_IS(&tokens->tokens[m], "(")) {
                        nesting += 1;
                    } else if (TOKEN_IS(&tokens->tokens[m], ")")) {
                        nesting -= 1;
                    }
                }
                if (nesting > 0) {
                    negative = true;
                    break;
                }
            }
            if (!negative && !alternative) {
                return false;
            }
        }
    }
    return true;
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
    if (!ENDS_WITH(file->path, file->path_len, ".c") || file->is_ryu
        || !file->preprocessor_ok) {
        return;
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
                                      true)) {
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
            bool member = false;
            int32 previous;
            int32 next;

            if (j == item->name_token || token->kind != TOKEN_IDENT
                || token->len != name->len
                || memcmp64(token->text, name->text, name->len)) {
                continue;
            }
            previous = tokenization_previous_significant(tokens, j);
            next = tokenization_next_significant(tokens, j);
            /* Member names, tags and labels have namespaces separate
             * from ordinary file-scope function identifiers. */
            if (previous >= 0
                && (TOKEN_IS(&tokens->tokens[previous], ".")
                    || TOKEN_IS(&tokens->tokens[previous], "->")
                    || TOKEN_IS(&tokens->tokens[previous], "struct")
                    || TOKEN_IS(&tokens->tokens[previous], "union")
                    || TOKEN_IS(&tokens->tokens[previous], "enum")
                    || TOKEN_IS(&tokens->tokens[previous], "goto"))) {
                continue;
            }
            if (next < ARRAY_LEN(tokens->tokens)
                && TOKEN_IS(&tokens->tokens[next], ":")
                && previous >= 0
                && (TOKEN_IS(&tokens->tokens[previous], "{")
                    || TOKEN_IS(&tokens->tokens[previous], ";")
                    || TOKEN_IS(&tokens->tokens[previous], "}"))) {
                continue;
            }
            if (!csane_single_use_code(file, j, false)) {
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
                if (candidate->kind == CSANE_TOP_TYPE_DECLARATION
                    && j >= candidate->tokens.first
                    && j < candidate->tokens.end) {
                    int32 depth = 0;
                    bool opened = false;

                    /* Struct, union and enum members are different names.
                     * Stop after the type body's matching brace; a later
                     * aggregate initializer may contain a real reference. */
                    for (int32 m = candidate->tokens.first; m < j; m += 1) {
                        if (TOKEN_IS(&tokens->tokens[m], "{")) {
                            depth += 1;
                            opened = true;
                        } else if (TOKEN_IS(&tokens->tokens[m], "}")) {
                            depth -= 1;
                            if (opened && depth == 0) {
                                break;
                            }
                        }
                    }
                    if (opened && depth > 0) {
                        member = true;
                        break;
                    }
                }
            }
            if (prototype || member) {
                continue;
            }
            if (next < ARRAY_LEN(tokens->tokens)
                && TOKEN_IS(&tokens->tokens[next], "(")
                && (previous < 0
                    || !TOKEN_IS(&tokens->tokens[previous], "&"))) {
                for (int32 k = 0; k < ARRAY_LEN(file->toplevel->items);
                     k += 1) {
                    CsaneTopItem *caller = &file->toplevel->items[k];
                    int32 statement;
                    bool declaration = false;

                    if (caller->kind != CSANE_TOP_FUNCTION_DEFINITION
                        || j < caller->body.first
                        || j >= caller->body.end) {
                        continue;
                    }
                    /* Local function prototypes have call-like spelling,
                     * but the identifier is a declarator, not an invocation.
                     * A call in a variable initializer is still a call. */
                    statement = csane_statements_at_token(file->statements,
                                                          k, j);
                    if (statement >= 0) {
                        CsaneStatement *stmt =
                            &file->statements->items[statement];
                        if (stmt->kind == CSANE_STMT_SIMPLE
                            && csane_classify_statement(tokens,
                                                        stmt->tokens)
                                   == CSANE_LOCAL_DECLARATION) {
                            declaration = true;
                            for (int32 m = stmt->tokens.first; m < j;
                                 m += 1) {
                                if (TOKEN_IS(&tokens->tokens[m], "=")) {
                                    declaration = false;
                                    break;
                                }
                            }
                        }
                    }
                    if (!declaration) {
                        direct = true;
                        if (caller == item) {
                            self_recursive = true;
                        }
                        deep_call = csane_single_use_deep_call(file, k, j);
                    } else {
                        prototype = true;
                    }
                    break;
                }
            }
            if (prototype) {
                continue;
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
        if (count <= 1 && !uncertain && !deep_call
            && !csane_include_graph_foreign_reference(file->include_graph,
                                                      file->path, name->text,
                                                      name->len)) {
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
