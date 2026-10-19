// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STRUCTURE_C)
#define CSANECHECK_STRUCTURE_C

#include "cbase.h"
#include "csanecheck.h"

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

static int32
csane_decl_line(CsaneFile *file, int32 index) {
    CDeclaration *d = &file->ast->declarations[index];
    int32 token = token_range_first_significant(file->tokens, d->tokens);
    if (token >= d->tokens.end) {
        return -1;
    }
    return tokenization_token_location(file->tokens, token).line;
}

static void
csane_structure_declaration(CsaneFile *file, int32 index,
                            int32 *previous_line) {
    CAst *ast = file->ast;
    CDeclaration *d;
    int32 line;
    int32 token;
    bool same_line;
    bool multiple;
    if (index < 0 || index >= ast->declaration_count) {
        return;
    }
    d = &ast->declarations[index];
    if (!csane_decl_variables(ast, index)) {
        return;
    }
    line = csane_decl_line(file, index);
    same_line = (line > 0) && (line == *previous_line);
    multiple = d->declarator_count > 1;
    if (same_line || multiple) {
        token = token_range_first_significant(file->tokens, d->tokens);
        if (token < d->tokens.end) {
            csane_rule_issue(file, file->tokens->tokens[token].offset,
                             "AST-DECL-ONE",
                             "only one variable declaration per line");
        }
    }
    *previous_line = line;
}

/* Declaration checking remains AST-based until Step 9. */
static void
csane_structure_declaration_tree(CsaneFile *file, int32 first) {
    CAst *ast = file->ast;
    int32 last_line = -1;
    for (int32 i = first; i >= 0; i = ast->statements[i].next) {
        CStatement *stmt = &ast->statements[i];
        if (stmt->kind == C_STMT_DECLARATION) {
            csane_structure_declaration(file, stmt->declaration, &last_line);
        } else if (stmt->kind == C_STMT_FOR) {
            int32 isolated = -1;
            csane_structure_declaration(file, stmt->declaration, &isolated);
        }
        if (stmt->first_child >= 0) {
            csane_structure_declaration_tree(file, stmt->first_child);
        }
        if (stmt->else_child >= 0) {
            csane_structure_declaration_tree(file, stmt->else_child);
        }
    }
}

static bool
csane_structure_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static void csane_structure_externals(CsaneFile *, int32);

static void
csane_structure_external(CsaneFile *file, int32 index) {
    CExternal *e = &file->ast->externals[index];
    if (e->function_body >= 0) {
        csane_structure_declaration_tree(file, e->function_body);
    }
    if (e->first_child >= 0) {
        csane_structure_externals(file, e->first_child);
    }
}

static void
csane_structure_externals(CsaneFile *file, int32 first) {
    CAst *ast = file->ast;
    int32 last_line = -1;
    for (int32 i = first; i >= 0; i = ast->externals[i].next) {
        CExternal *e = &ast->externals[i];
        if (e->kind == C_EXTERNAL_DECLARATION) {
            csane_structure_declaration(file, e->declaration, &last_line);
        }
        csane_structure_external(file, i);
    }
}

void
csane_structure_check(CsaneFile *file) {
    CAst *ast = file->ast;
    /* The legacy Perl structural checker excludes generated metadata headers
     * and vendored Ryu, including its declaration and brace checks. */
    if (file->is_ryu || csane_structure_generated(file->path)) {
        return;
    }
    /* Translation-unit and compound scopes have separate declaration lists.
     * Conditional branches also remain independent; never join alternatives.
     */
    /* The translation-unit parse begins with the root external at index 0;
     * child alternatives have separate linked lists. */
    if (ast->external_count > 0) {
        csane_structure_externals(file, 0);
    }
    /* Struct/union members have their own namespace and declaration sequence
     * and are not statement AST nodes. Treat each record independently. */
    for (int32 i = 0; i < ast->record_count; i += 1) {
        CRecord *record = &ast->records[i];
        int32 last_line = -1;
        for (int32 j = record->first_member; j >= 0;
             j = ast->declarations[j].next) {
            csane_structure_declaration(file, j, &last_line);
        }
    }
    return;
}

#endif /* CSANECHECK_STRUCTURE_C */
