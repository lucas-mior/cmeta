// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_COVERAGE_C)
#define CSANECHECK_COVERAGE_C

#include "cbase.h"
#include "csck.h"

/* Mark source tokens, not AST node counts. Multiple overlapping nodes must
 * never increase the reported amount of source examined. */
static void
csane_coverage_mark(uint8 *flags, int32 count, TokenRange range,
                    uint8 flag) {
    int32 start = MAX(0, range.first);
    int32 end = MIN(count, range.end);
    for (int32 i = start; i < end; i += 1) {
        flags[i] |= flag;
    }
}

CsaneAnalysisCoverage
csane_analysis_coverage(CsaneFile *file, bool pp_ok, bool ast_ok) {
    CsaneAnalysisCoverage result = {0};
    Tokenization *tokens = file->tokens;
    CPreprocessorAnalysis *pp = file->preprocessor;
    CAst *ast = file->ast;
    int32 count = tokens->token_count;
    uint8 *flags = NULL;
    result.preprocessor_ready = pp_ok;
    result.ast_ready = ast_ok;
    result.preprocessor_issues = pp->issue_count + (!pp_ok
                                                   && pp->issue_count == 0);
    result.parse_issues = ast_ok ? ast->issue_count : 0;
    if (count > 0) {
        flags = malloc2((int64)count*SIZEOF(*flags));
        if (flags == NULL) {
            result.ast_ready = false;
        } else {
            for (int32 i = 0; i < count; i += 1) {
                flags[i] = 0;
            }
        }
    }
    if (flags != NULL && ast_ok) {
        for (int32 i = 0; i < ast->external_count; i += 1) {
            CExternal *external = &ast->externals[i];
            if (external->kind != C_EXTERNAL_UNSUPPORTED) {
                csane_coverage_mark(flags, count, external->tokens, 1);
            } else {
                csane_coverage_mark(flags, count, external->tokens, 2);
            }
        }
        for (int32 i = 0; i < ast->statement_count; i += 1) {
            CStatement *statement = &ast->statements[i];
            if (statement->kind == C_STMT_UNSUPPORTED) {
                csane_coverage_mark(flags, count, statement->tokens, 2);
            }
        }
        for (int32 i = 0; i < ast->issue_count; i += 1) {
            CParseIssue *issue = &ast->issues[i];
            csane_coverage_mark(flags, count, issue->tokens, 2);
            if (issue->tokens.first >= issue->tokens.end
                && issue->token >= 0 && issue->token < count) {
                flags[issue->token] |= 2;
            }
        }
    }
    for (int32 i = 0; i < count; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token->kind == TOKEN_SPACE || token->kind == TOKEN_NEWLINE
            || token->kind == TOKEN_COMMENT) {
            continue;
        }
        if (pp->token_roles != NULL) {
            enum CPreprocessorTokenRole role = pp->token_roles[i];
            if (role == C_PP_TOKEN_MACRO_REPLACEMENT) {
                result.macro_tokens += 1;
                continue;
            }
            if (role != C_PP_TOKEN_CODE) {
                continue;
            }
        }
        result.eligible_tokens += 1;
        if (flags != NULL && (flags[i] & 1) && !(flags[i] & 2)) {
            result.parsed_tokens += 1;
        } else {
            result.unresolved_tokens += 1;
        }
    }
    if (flags != NULL) {
        free2(flags, (int64)count*SIZEOF(*flags));
    }
    return result;
}

#endif /* CSANECHECK_COVERAGE_C */
