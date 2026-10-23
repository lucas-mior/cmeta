// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_COVERAGE_C)
#define CSANECHECK_COVERAGE_C

#include "cbase.h"
#include "csck.h"

bool
csane_token_name_in(Token *token, CsanePaths *names) {
    if (token->kind != TOKEN_IDENT) {
        return false;
    }
    for (int32 i = 0; i < strflex_list_len(&names->strings); i += 1) {
        int32 len = names->strings.items[i]->len;
        if (len == token->len
            && !memcmp64(token->text, names->strings.items[i]->data, len)) {
            return true;
        }
    }
    return false;
}

static void
csane_coverage_result(CsaneRuleCounts *counts, enum CsaneCoverageState state) {
    counts->eligible += 1;
    if (state == CSANE_COVERAGE_CHECKED) {
        counts->checked += 1;
    } else if (state == CSANE_COVERAGE_EXCLUDED) {
        counts->excluded += 1;
    } else {
        counts->unresolved += 1;
    }
}

static void
csane_coverage_target(CsaneRuleCounts *counts, bool resolved) {
    csane_coverage_result(counts, resolved ? CSANE_COVERAGE_CHECKED
                                           : CSANE_COVERAGE_UNRESOLVED);
}

static void
csane_coverage_unresolved(CsaneFile *file, CsaneRuleCounts *counts,
                          char *group, int32 group_len, int32 token,
                          char *reason, bool report) {
    SourceLocation location;

    csane_coverage_target(counts, false);
    if (!report) {
        return;
    }
    location = tokenization_token_location(file->tokens, token);
    fmt_printf("rule_coverage_unresolved file=%.*s group=%.*s "
               "line=%d column=%d reason=%s\n",
               file->path_len, file->path, group_len, group,
               location.line, location.column + 1,
               reason);
}

static bool
csane_coverage_code(CsaneFile *file, int32 index) {
    CPreprocessorAnalysis *pp = file->preprocessor;
    if (pp->token_roles == NULL) {
        return true;
    }
    return pp->token_roles[index] == C_PP_TOKEN_CODE
           || pp->token_roles[index] == C_PP_TOKEN_MACRO_REPLACEMENT;
}

static bool
csane_coverage_opaque(CsaneFile *file, TokenRange range) {
    CsaneRegions *regions = file->regions;
    if (regions == NULL) {
        return true;
    }
    /* An incomplete directive does not poison a disjoint code region.
     * Descendant regions carry their own independent completeness flags. */
    for (int32 i = 0; i < regions->count; i += 1) {
        CsaneSourceRegion *region = &regions->items[i];
        if (region->complete || region->tokens.end <= range.first
            || region->tokens.first >= range.end) {
            continue;
        }
        if (region->kind == CSANE_REGION_CODE
            || region->kind == CSANE_REGION_MACRO_REPLACEMENT) {
            return true;
        }
    }
    return false;
}

static void
csane_coverage_top(CsaneFile *file, CsaneRuleCounts *counts,
                   enum CsaneTargetKind kind) {
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        bool selected = false;
        if (kind == CSANE_TARGET_DECLARATIONS) {
            selected = item->kind == CSANE_TOP_DECLARATION
                       || item->kind == CSANE_TOP_MEMBER_DECLARATION
                       || item->kind == CSANE_TOP_ENUMERATOR
                       || item->kind == CSANE_TOP_PARAMETER_FRAGMENT
                       || item->kind == CSANE_TOP_TYPE_DECLARATION
                       || item->kind == CSANE_TOP_FUNCTION_DEFINITION;
        } else if (kind == CSANE_TARGET_FUNCTIONS) {
            selected = item->kind == CSANE_TOP_FUNCTION_DEFINITION
                       || item->kind == CSANE_TOP_FUNCTION_DECLARATION;
        } else {
            selected = item->kind == CSANE_TOP_FUNCTION_DEFINITION;
        }
        if (item->kind == CSANE_TOP_OPAQUE) {
            csane_coverage_target(counts, false);
        } else if (selected) {
            csane_coverage_target(counts,
                                  !csane_coverage_opaque(file, item->tokens));
        }
    }
}

static void
csane_coverage_statements(CsaneFile *file, CsaneRuleCounts *counts,
                          enum CsaneTargetKind kind) {
    for (int32 i = 0; i < file->statements->count; i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        bool selected = kind == CSANE_TARGET_STATEMENTS
                        || kind == CSANE_TARGET_DECLARATIONS;
        if (selected && stmt->kind != CSANE_STMT_BLOCK) {
            csane_coverage_target(counts,
                                  stmt->kind != CSANE_STMT_OPAQUE
                                  && !csane_coverage_opaque(file,
                                                            stmt->tokens));
        }
    }
}


static int32
csane_coverage_first(CsaneFile *file, TokenRange range) {
    int32 first = token_range_first_significant(file->tokens, range);
    return first >= 0 && first < range.end ? first : -1;
}

static bool
csane_coverage_token(CsaneFile *file, int32 index,
                     char *text, int32 text_len) {
    return index >= 0 && index < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[index], text, text_len);
}

static bool
csane_coverage_declaration_start(CsaneFile *file, TokenRange range) {
    int32 first = csane_coverage_first(file, range);
    Token *token;

    if (first < 0) {
        return false;
    }
    token = &file->tokens->tokens[first];
    if (csane_type_name(token) || csane_type_hints_typedef(file->type_hints,
                                                           token)) {
        return true;
    }
    return csane_coverage_token(file, first, STRLIT("struct"))
           || csane_coverage_token(file, first, STRLIT("union"))
           || csane_coverage_token(file, first, STRLIT("enum"))
           || csane_coverage_token(file, first, STRLIT("typedef"))
           || csane_coverage_token(file, first, STRLIT("static"))
           || csane_coverage_token(file, first, STRLIT("extern"))
           || csane_coverage_token(file, first, STRLIT("const"))
           || csane_coverage_token(file, first, STRLIT("volatile"));
}

static bool
csane_coverage_contains(CsaneFile *file, TokenRange range,
                        char *text, int32 text_len) {
    for (int32 i = range.first; i < range.end; i += 1) {
        if (csane_coverage_token(file, i, text, text_len)) {
            return true;
        }
    }
    return false;
}

static void
csane_coverage_decl_one(CsaneFile *file, CsaneRuleCounts *counts,
                        char *group, int32 group_len, bool report) {
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION
            || item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            csane_coverage_target(counts, true);
        } else if (item->kind == CSANE_TOP_OPAQUE
                   && csane_coverage_declaration_start(file, item->tokens)) {
            int32 first = csane_coverage_first(file, item->tokens);
            csane_coverage_unresolved(file, counts, group, group_len, first,
                                      "declaration-structure-unrecognized",
                                      report);
        }
    }
}

static bool
csane_coverage_function_shape(CsaneFile *file, TokenRange range) {
    int32 first = csane_coverage_first(file, range);
    if (first < 0 || !csane_coverage_declaration_start(file, range)) {
        return false;
    }
    for (int32 i = first; i < range.end; i += 1) {
        int32 previous;

        if (csane_coverage_token(file, i, STRLIT("="))) {
            return false;
        }
        if (!csane_coverage_token(file, i, STRLIT("("))) {
            continue;
        }
        previous = tokenization_previous_significant(file->tokens, i);
        return previous >= first
               && file->tokens->tokens[previous].kind == TOKEN_IDENT;
    }
    return false;
}

static void
csane_coverage_func_headers(CsaneFile *file, CsaneRuleCounts *counts,
                            char *group, int32 group_len, bool report) {
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            csane_coverage_target(counts, true);
        } else if (item->kind == CSANE_TOP_OPAQUE
                   && csane_coverage_function_shape(file, item->tokens)) {
            int32 first = csane_coverage_first(file, item->tokens);
            csane_coverage_unresolved(file, counts, group, group_len, first,
                                      "function-header-unrecognized",
                                      report);
        }
    }
}

static bool
csane_coverage_control_kind(enum CsaneStatementKind kind) {
    return kind == CSANE_STMT_IF || kind == CSANE_STMT_FOR
           || kind == CSANE_STMT_WHILE || kind == CSANE_STMT_DO
           || kind == CSANE_STMT_SWITCH || kind == CSANE_STMT_CASE
           || kind == CSANE_STMT_DEFAULT || kind == CSANE_STMT_BLOCK;
}

static bool
csane_coverage_control_start(CsaneFile *file, int32 token) {
    return csane_coverage_token(file, token, STRLIT("if"))
           || csane_coverage_token(file, token, STRLIT("for"))
           || csane_coverage_token(file, token, STRLIT("while"))
           || csane_coverage_token(file, token, STRLIT("do"))
           || csane_coverage_token(file, token, STRLIT("switch"))
           || csane_coverage_token(file, token, STRLIT("case"))
           || csane_coverage_token(file, token, STRLIT("default"))
           || csane_coverage_token(file, token, STRLIT("{"));
}

static void
csane_coverage_control(CsaneFile *file, CsaneRuleCounts *counts,
                       char *group, int32 group_len, bool report) {
    for (int32 i = 0; i < file->statements->count; i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        if (csane_coverage_control_kind(stmt->kind)) {
            csane_coverage_target(counts, true);
        } else if (stmt->kind == CSANE_STMT_OPAQUE) {
            int32 first = csane_coverage_first(file, stmt->tokens);
            if (first >= 0 && csane_coverage_control_start(file, first)) {
                csane_coverage_unresolved(file, counts, group, group_len, first,
                                          "control-structure-unrecognized",
                                          report);
            }
        }
    }
}

static void
csane_coverage_status(CsaneFile *file, CsaneRuleCounts *counts,
                      char *group, int32 group_len, bool report) {
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (!csane_coverage_contains(file, item->tokens, STRLIT("status"))) {
            continue;
        }
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            csane_coverage_target(counts, true);
        } else if (item->kind == CSANE_TOP_OPAQUE) {
            int32 first = csane_coverage_first(file, item->tokens);
            csane_coverage_unresolved(file, counts, group, group_len, first,
                                      "status-function-unrecognized",
                                      report);
        }
    }
}

static bool
csane_coverage_call_shape(CsaneFile *file, TokenRange range, bool *closed) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    int32 first;
    int32 last;
    int32 name;
    int32 open;
    int32 close;
    if (!csane_tokens_view(&view, tokens, range)) {
        return false;
    }
    first = csane_coverage_first(file, range);
    last = token_range_last_significant(tokens, range);
    if (first < 0 || last < first
        || !csane_coverage_token(file, last, STRLIT(";"))) {
        return false;
    }
    name = first;
    if (csane_coverage_token(file, name, STRLIT("return"))) {
        name = csane_tokens_next(&view, name);
    } else {
        int32 equals = -1;
        for (int32 i = first; i < last; i += 1) {
            if (csane_coverage_token(file, i, STRLIT("="))) {
                equals = i;
            } else if (csane_coverage_token(file, i, STRLIT("("))
                       || csane_coverage_token(file, i, STRLIT("["))) {
                int32 match = csane_tokens_match(&view, i);
                if (match > i && match < last) {
                    i = match;
                }
            }
        }
        if (equals >= 0) {
            name = csane_tokens_next(&view, equals);
        }
    }
    if (name < 0 || name >= last || tokens->tokens[name].kind != TOKEN_IDENT) {
        return false;
    }
    open = csane_tokens_next(&view, name);
    if (open < 0 || open >= last
        || !csane_coverage_token(file, open, STRLIT("("))) {
        return false;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0) {
        *closed = false;
        return true;
    }
    if (close >= last || csane_tokens_next(&view, close) != last) {
        return false;
    }
    *closed = true;
    return true;
}

static bool
csane_coverage_assignment_shape(CsaneFile *file, TokenRange range,
                                bool *closed) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    int32 first = csane_coverage_first(file, range);
    int32 last = token_range_last_significant(tokens, range);
    bool found = false;
    if (first < 0 || last < first || !csane_tokens_view(&view, tokens, range)) {
        return false;
    }
    *closed = csane_coverage_token(file, last, STRLIT(";"));
    for (int32 i = first; i < last; i += 1) {
        if (csane_coverage_token(file, i, STRLIT("="))) {
            found = true;
        } else if (csane_coverage_token(file, i, STRLIT("("))
                   || csane_coverage_token(file, i, STRLIT("["))
                   || csane_coverage_token(file, i, STRLIT("{"))) {
            int32 close = csane_tokens_match(&view, i);
            if (close < 0) {
                *closed = false;
                break;
            }
            if (close < last) {
                i = close;
            }
        }
    }
    return found;
}

static void
csane_coverage_wrap_statements(CsaneFile *file, CsaneRuleCounts *counts,
                               char *group, int32 group_len,
                               bool report, bool calls) {
    for (int32 i = 0; i < file->statements->count; i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        bool closed = false;
        bool candidate;
        if (stmt->kind != CSANE_STMT_SIMPLE
            && stmt->kind != CSANE_STMT_OPAQUE) {
            continue;
        }
        candidate = calls
                    ? csane_coverage_call_shape(file, stmt->tokens, &closed)
                    : csane_coverage_assignment_shape(file, stmt->tokens,
                                                      &closed);
        if (!candidate) {
            continue;
        }
        if (closed && stmt->kind == CSANE_STMT_SIMPLE) {
            csane_coverage_target(counts, true);
        } else {
            int32 first = csane_coverage_first(file, stmt->tokens);
            char *reason = calls ? "call-statement-boundary-unrecognized"
                                 : "assignment-statement-boundary-unrecognized";
            csane_coverage_unresolved(file, counts, group, group_len,
                                      first, reason, report);
        }
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        bool closed = false;
        bool candidate;
        if (item->kind != CSANE_TOP_DECLARATION
            && item->kind != CSANE_TOP_OPAQUE) {
            continue;
        }
        candidate = calls
                    ? csane_coverage_call_shape(file, item->tokens, &closed)
                    : csane_coverage_assignment_shape(file, item->tokens,
                                                      &closed);
        if (!candidate) {
            continue;
        }
        if (closed) {
            csane_coverage_target(counts, true);
        } else {
            int32 first = csane_coverage_first(file, item->tokens);
            char *reason = calls ? "call-statement-boundary-unrecognized"
                                 : "assignment-statement-boundary-unrecognized";
            csane_coverage_unresolved(file, counts, group, group_len,
                                      first, reason, report);
        }
    }
}

static bool
csane_coverage_operator(Token *token) {
    return TOKEN_IS(token, "*") || TOKEN_IS(token, "+")
           || TOKEN_IS(token, "-") || TOKEN_IS(token, "&")
           || TOKEN_IS(token, "<<") || TOKEN_IS(token, ">>")
           || TOKEN_IS(token, "&&") || TOKEN_IS(token, "||")
           || TOKEN_IS(token, "==") || TOKEN_IS(token, "!=")
           || TOKEN_IS(token, "<") || TOKEN_IS(token, "<=")
           || TOKEN_IS(token, ">") || TOKEN_IS(token, ">=")
           || TOKEN_IS(token, "=") || TOKEN_IS(token, "+=")
           || TOKEN_IS(token, "-=") || TOKEN_IS(token, "*=")
           || TOKEN_IS(token, "/=") || TOKEN_IS(token, "%=")
           || TOKEN_IS(token, "&=") || TOKEN_IS(token, "|=")
           || TOKEN_IS(token, "^=") || TOKEN_IS(token, "<<=")
           || TOKEN_IS(token, ">>=");
}

static bool
csane_coverage_call_closed(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    int32 region = csane_regions_at_token(file->regions, index);
    enum CsaneRegionKind kind = CSANE_REGION_MACRO_REPLACEMENT;
    int32 macro = csane_regions_enclosing(file->regions, region, kind);
    if (macro >= 0) {
        if (!csane_tokens_view(&view, tokens,
                               file->regions->items[macro].tokens)) {
            return false;
        }
    } else {
        int32 branch = csane_regions_enclosing(file->regions, region,
                                               CSANE_REGION_CONDITIONAL_BRANCH);
        if (branch >= 0) {
            CsaneTokenView candidate;
            if (csane_tokens_view(&candidate, tokens,
                                  file->regions->items[branch].tokens)
                && csane_tokens_match(&candidate, index) >= 0) {
                view = candidate;
            }
        }
    }
    return csane_tokens_match(&view, index) >= 0;
}

CsaneRuleCounts
csane_coverage_rule(CsaneFile *file, enum CsaneTargetKind kind,
                    char *group, int32 group_len, bool report) {
    CsaneRuleCounts counts = {0};
    Tokenization *tokens = file->tokens;
    if (kind == CSANE_TARGET_DECL_ONE) {
        csane_coverage_decl_one(file, &counts, group, group_len, report);
    } else if (kind == CSANE_TARGET_FUNC_HEADERS) {
        csane_coverage_func_headers(file, &counts, group, group_len, report);
    } else if (kind == CSANE_TARGET_CONTROL) {
        csane_coverage_control(file, &counts, group, group_len, report);
    } else if (kind == CSANE_TARGET_STYLE_STATUS) {
        csane_coverage_status(file, &counts, group, group_len, report);
    } else if (kind == CSANE_TARGET_CALL_WRAP) {
        csane_coverage_wrap_statements(file, &counts, group, group_len,
                                       report, true);
    } else if (kind == CSANE_TARGET_ASSIGN_WRAP) {
        csane_coverage_wrap_statements(file, &counts, group, group_len,
                                       report, false);
    } else if (kind == CSANE_TARGET_LINES) {
        for (int32 i = 0; i < tokens->text_len; i += 1) {
            if (i == 0 || tokens->text[i - 1] == '\n') {
                csane_coverage_target(&counts, true);
            }
        }
    } else if (kind == CSANE_TARGET_DIRECTIVES) {
        for (int32 i = 0; i < file->preprocessor->directive_count; i += 1) {
            csane_coverage_target(&counts, true);
        }
        if (!file->preprocessor_ok) {
            csane_coverage_target(&counts, false);
        }
    } else if (kind == CSANE_TARGET_DECLARATIONS
               || kind == CSANE_TARGET_STATEMENTS
               || kind == CSANE_TARGET_FUNCTIONS) {
        csane_coverage_top(file, &counts, kind);
        if (kind != CSANE_TARGET_FUNCTIONS) {
            csane_coverage_statements(file, &counts, kind);
        }
    } else {
        for (int32 i = 0; i < tokens->token_count; i += 1) {
            Token *token = &tokens->tokens[i];
            bool relevant = false;
            bool resolved = true;
            enum CsaneApiUse api_use = CSANE_API_NOT_CALL;
            if (kind != CSANE_TARGET_APIS
                && !csane_coverage_code(file, i)) {
                continue;
            }
            if ((kind == CSANE_TARGET_OPERATORS
                 || kind == CSANE_TARGET_ASSIGNMENTS)
                && file->preprocessor->token_roles != NULL
                && file->preprocessor->token_roles[i]
                   != C_PP_TOKEN_CODE) {
                continue;
            }
            if (kind == CSANE_TARGET_SIZEOF) {
                relevant = token->kind == TOKEN_IDENT
                           && TOKEN_IS(token, "sizeof");
            } else if (kind == CSANE_TARGET_APIS) {
                CsanePaths *never = &file->policy->never_names;
                CsanePaths *restricted = &file->policy->cbase_names;
                relevant = csane_token_name_in(token, never)
                           || (!file->in_cbase
                               && csane_token_name_in(token, restricted));
                if (relevant) {
                    api_use = csane_api_classify(file, i);
                    resolved = api_use != CSANE_API_UNRESOLVED;
                }
            } else if (kind == CSANE_TARGET_ASSIGNMENTS) {
                relevant = TOKEN_IS(token, "=");
            } else if (kind == CSANE_TARGET_OPERATORS) {
                relevant = csane_coverage_operator(token);
                resolved = csane_layout_classify(file, i)
                           != CSANE_LAYOUT_UNRESOLVED;
            } else if (kind == CSANE_TARGET_CALLS
                       || kind == CSANE_TARGET_STYLE_CALLS) {
                int32 name = tokenization_previous_significant(tokens, i);
                relevant = TOKEN_IS(token, "(") && name >= 0
                           && tokens->tokens[name].kind == TOKEN_IDENT;
                if (kind == CSANE_TARGET_STYLE_CALLS && relevant) {
                    relevant = TOKEN_IS(&tokens->tokens[name], "ASSERT")
                               || TOKEN_IS(&tokens->tokens[name],
                                           "STRLIT_LEN")
                               || TOKEN_IS(&tokens->tokens[name],
                                           "STREQUAL")
                               || TOKEN_IS(&tokens->tokens[name],
                                           "str_printf");
                }
                resolved = csane_coverage_call_closed(file, i);
            }
            if (relevant) {
                if (!resolved && kind == CSANE_TARGET_OPERATORS) {
                    csane_coverage_unresolved(file, &counts, group, group_len,
                                              i, "ambiguous-operator-context",
                                              report);
                } else if (!resolved && kind == CSANE_TARGET_APIS) {
                    csane_coverage_unresolved(file, &counts, group, group_len,
                                              i,
                                              "api-call-context-unrecognized",
                                              report);
                } else {
                    csane_coverage_target(&counts, resolved);
                }
            }
        }
    }
    return counts;
}

#endif /* CSANECHECK_COVERAGE_C */
