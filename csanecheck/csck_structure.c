// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STRUCTURE_C)
#define CSANECHECK_STRUCTURE_C

#include "cbase.h"
#include "csck.h"

/* Declarator nodes are nested from outside inward. The operation closest to
 * the identifier determines whether it declares a function, rather than a
 * pointer to a function. Parenthesized declarators do not change this.
 */
static bool
csane_decl_function(CAst *ast, int32 root) {
    enum CDeclaratorKind nearest = 0;
    while (root >= 0) {
        CDeclaratorNode *d = &ast->declarators[root];
        if ((d->kind == C_DECL_FUNCTION) || (d->kind == C_DECL_POINTER)
            || (d->kind == C_DECL_ARRAY)) {
            nearest = d->kind;
        }
        root = d->inner;
    }
    return nearest == C_DECL_FUNCTION;
}

static bool
csane_decl_variables(CAst *ast, int32 index) {
    CDeclaration *d = &ast->declarations[index];
    if (d->kind == C_DECLARATION_PARAMETER
        || d->first_declarator < 0) {
        return false;
    }
    /* A prototype/definition alone is not a variable declaration. */
    if (d->declarator_count == 1
        && csane_decl_function(ast, d->first_declarator)) {
        return false;
    }
    return true;
}

CsaneCoverage
csane_coverage_collect(CAst *ast) {
    CsaneCoverage result = {0};
    for (int32 i = 0; i < ast->declaration_count; i += 1) {
        CDeclaration *d = &ast->declarations[i];
        result.declarations += 1;
        if (d->kind == C_DECLARATION_NORMAL
            && d->declarator_count == 1
            && csane_decl_function(ast, d->first_declarator)) {
            if (d->is_function_definition) {
                result.function_definitions += 1;
            } else {
                result.function_declarations += 1;
            }
        }
    }
    for (int32 i = 0; i < ast->statement_count; i += 1) {
        if (ast->statements[i].kind == C_STMT_UNSUPPORTED) {
            result.unsupported_statements += 1;
        } else if ((ast->statements[i].kind != C_STMT_PP_DIRECTIVE)
                   && (ast->statements[i].kind != C_STMT_PP_GROUP)
                   && (ast->statements[i].kind != C_STMT_PP_BRANCH)) {
            result.statements += 1;
        }
    }
    result.expressions = ast->expression_count;
    for (int32 i = 0; i < ast->external_count; i += 1) {
        if (ast->externals[i].kind == C_EXTERNAL_UNSUPPORTED
            || ast->externals[i].kind == C_EXTERNAL_PARTIAL_FUNCTION) {
            result.unsupported_externals += 1;
        }
    }
    for (int32 i = 0; i < ast->issue_count; i += 1) {
        if (ast->issues[i].kind == C_PARSE_ISSUE_UNSUPPORTED) {
            result.unsupported_issues += 1;
        } else if (ast->issues[i].kind == C_PARSE_ISSUE_MALFORMED) {
            result.malformed_issues += 1;
        }
    }
    return result;
}

#endif /* CSANECHECK_STRUCTURE_C */
