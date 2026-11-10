// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_MATRIX_INDEX_C)
#define CSANECHECK_MATRIX_INDEX_C

#include "cbase.h"
#include "csck.h"
#include "csck_regions.h"
#include "csck_tokens.h"

/* A syntactic check of additive expressions containing a row*stride term.
 * Bounded token views keep unrelated preprocessor branches and replacement
 * lists separate. A strided one-dimensional buffer can have the same shape;
 * use a narrow policy exemption if that layout is intentional.
 * Only report when a file contains at least two non-exempt matches.
 */
void
csane_matrix_index_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView whole = csane_tokens_full(tokens);
    int32 first_match = -1;
    bool multiple_matches = false;

    if (file->regions == NULL) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        CsaneTokenView local = whole;
        CsaneTokenView expression;
        TokenRange content;
        TokenRange terms[32];
        int32 previous;
        int32 region;
        int32 replacement;
        int32 branch;
        int32 count;
        bool has_product = false;
        bool has_column = false;
        bool symbolic_stride = false;
        bool symbolic_column = false;
        bool valid = true;

        if (!TOKEN_IS(&tokens->tokens[i], "[")
            || !csane_calls_code(file, i)) {
            continue;
        }
        region = csane_regions_at_token(file->regions, i);
        replacement = csane_regions_enclosing(file->regions, region,
                                              CSANE_REGION_MACRO_REPLACEMENT);
        branch = csane_regions_enclosing(file->regions, region,
                                         CSANE_REGION_CONDITIONAL_BRANCH);
        if (replacement >= 0) {
            if (!csane_tokens_view(&local, tokens,
                                   file->regions->items[replacement].tokens)) {
                continue;
            }
        } else if (branch >= 0) {
            if (!csane_tokens_view(&local, tokens,
                                   file->regions->items[branch].tokens)) {
                continue;
            }
        }
        previous = csane_tokens_previous(&local, i);
        if (previous < local.range.first
            || (tokens->tokens[previous].kind != TOKEN_IDENT
                && !TOKEN_IS(&tokens->tokens[previous], "]")
                && !TOKEN_IS(&tokens->tokens[previous], ")"))) {
            continue;
        }
        /* A declared type followed by a name and '[' is an array bound,
         * not a subscript. This includes variable-length arrays.
         */
        {
            int32 name = previous;
            int32 type;

            /* A second (or later) declarator dimension follows a ']'. */
            while (name >= local.range.first
                   && TOKEN_IS(&tokens->tokens[name], "]")) {
                int32 open = csane_tokens_match(&local, name);
                if (open < 0) {
                    break;
                }
                name = csane_tokens_previous(&local, open);
            }
            if (name >= local.range.first
                && TOKEN_IS(&tokens->tokens[name], ")")) {
                int32 open = csane_tokens_match(&local, name);
                if (open >= 0) {
                    name = open;
                }
            }
            type = csane_tokens_previous(&local, name);
            if (name >= local.range.first && type >= local.range.first
                && (csane_type_name(&tokens->tokens[type])
                    || csane_type_hints_typedef(file->type_hints,
                                                &tokens->tokens[type]))) {
                continue;
            }
        }
        if (!csane_tokens_group(&local, i, &content, NULL)) {
            continue;
        }
        content = csane_tokens_unwrap(&local, content);
        if (!csane_tokens_subview(&expression, &local, content)) {
            continue;
        }
        count = csane_tokens_split(&expression, STRLIT("+"), terms, 32);
        if (count < 2 || count > LENGTH(terms)) {
            continue;
        }
        for (int32 t = 0; t < count; t += 1) {
            CsaneTokenView part;
            TokenRange term = csane_tokens_unwrap(&expression, terms[t]);
            TokenRange factors[3];
            int32 first;
            int32 last;
            int32 product_count;
            int32 symbolic_factors = 0;
            bool product_valid = true;

            if (!csane_tokens_subview(&part, &expression, term)) {
                valid = false;
                break;
            }
            first = csane_tokens_next(&part, term.first - 1);
            last = csane_tokens_previous(&part, term.end);
            if (first >= term.end || last < first) {
                valid = false;
                break;
            }
            product_count = csane_tokens_split(&part, STRLIT("*"), factors, 3);
            if (product_count == 2) {
                for (int32 f = 0; f < product_count; f += 1) {
                    CsaneTokenView factor_view;
                    TokenRange factor = csane_tokens_unwrap(&part, factors[f]);
                    int32 start;
                    int32 end;

                    if (!csane_tokens_subview(&factor_view, &part, factor)) {
                        product_valid = false;
                        break;
                    }
                    start = csane_tokens_next(&factor_view, factor.first - 1);
                    end = csane_tokens_previous(&factor_view, factor.end);
                    if (start >= factor.end || end < start
                        || TOKEN_IS(&tokens->tokens[start], "*")
                        || TOKEN_IS(&tokens->tokens[end], "*")) {
                        product_valid = false;
                        break;
                    }
                    if (start != end
                        || tokens->tokens[start].kind != TOKEN_LITERAL) {
                        symbolic_factors += 1;
                    }
                }
                if (!product_valid) {
                    valid = false;
                    break;
                }
                if (symbolic_factors > 0) {
                    has_product = true;
                    symbolic_stride |= symbolic_factors == 2;
                    continue;
                }
            }
            has_column = true;
            symbolic_column |= first != last
                               || tokens->tokens[first].kind != TOKEN_LITERAL;
        }
        /* Treat 2*i + 1 as a linear odd-index expression. A matrix cell
         * needs either a nonliteral column or a symbolic row and stride.
         */
        if (!valid || !has_product || !has_column
            || (!symbolic_column && !symbolic_stride)) {
            continue;
        }
        /* Keep the exempted accessors narrowly scoped to their own #define
         * replacement; an ordinary macro doing manual indexing is checked.
         */
        if (file->preprocessor->token_roles != NULL
            && file->preprocessor->token_roles[i]
               == C_PP_TOKEN_MACRO_REPLACEMENT) {
            CPreprocessorAnalysis *pp = file->preprocessor;
            int32 directive_index = pp->token_directives[i];
            CPreprocessorDefine define = {0};

            if (directive_index >= 0
                && directive_index < ARRAY_LEN(pp->directives)
                && c_preprocessor_define_info(tokens,
                                              &pp->directives[directive_index],
                                              &define)) {
                Token *macro = &tokens->tokens[define.name_token];
                if (csane_policy_macro_allowed(file->policy, file->path,
                                               file->path_len,
                                               STRLIT("STYLE-MANUAL-2D-INDEX"),
                                               macro)) {
                    continue;
                }
            }
        }
        if (first_match < 0) {
            first_match = i;
            continue;
        }
        if (!multiple_matches) {
            csane_rule_issue(file, tokens->tokens[first_match].offset,
                             STRLIT("STYLE-MANUAL-2D-INDEX"),
                             "use a 2D indexing accessor instead of "
                             "array[row*stride + column]");
            multiple_matches = true;
        }
        csane_rule_issue(file, tokens->tokens[i].offset,
                         STRLIT("STYLE-MANUAL-2D-INDEX"),
                         "use a 2D indexing accessor instead of "
                         "array[row*stride + column]");
    }
}

#endif /* CSANECHECK_MATRIX_INDEX_C */
