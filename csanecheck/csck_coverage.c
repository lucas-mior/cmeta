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
    for (int32 i = 0; i < names->count; i += 1) {
        int32 len = strlen32(names->items[i]);
        if (len == token->len
            && !memcmp64(token->text, names->items[i], len)) {
            return true;
        }
    }
    return false;
}

static void
csane_coverage_target(CsaneRuleCounts *counts, bool resolved) {
    counts->eligible += 1;
    if (resolved) {
        counts->checked += 1;
    } else {
        counts->unresolved += 1;
    }
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
csane_coverage_rule(CsaneFile *file, enum CsaneTargetKind kind) {
    CsaneRuleCounts counts = {0};
    Tokenization *tokens = file->tokens;
    if (kind == CSANE_TARGET_LINES) {
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
            if (!csane_coverage_code(file, i)) {
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
                csane_coverage_target(&counts, resolved);
            }
        }
    }
    return counts;
}

#endif /* CSANECHECK_COVERAGE_C */
