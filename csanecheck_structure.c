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

/* CStatement children are direct syntactic children. The parent kind and
 * nearest enclosing switch are passed down explicitly so a case label in a
 * nested block still belongs to the correct switch.
 */
static void csane_structure_statement(CsaneFile *, int32,
                                     enum CStatementKind, int32);

static bool
csane_structure_pp(enum CStatementKind kind) {
    return kind == C_STMT_PP_DIRECTIVE || kind == C_STMT_PP_GROUP
           || kind == C_STMT_PP_BRANCH || kind == C_STMT_UNSUPPORTED;
}

static int32
csane_structure_token(CsaneFile *file, TokenRange range) {
    int32 token = token_range_first_significant(file->tokens, range);
    return token < range.end ? token : -1;
}

static void
csane_structure_braces(CsaneFile *file, CStatement *stmt) {
    CAst *ast = file->ast;
    int32 token;
    CStatement *body;
    if (stmt->kind != C_STMT_IF && stmt->kind != C_STMT_FOR
        && stmt->kind != C_STMT_WHILE && stmt->kind != C_STMT_DO
        && stmt->kind != C_STMT_SWITCH) {
        return;
    }
    if (stmt->first_child < 0) {
        return;
    }
    body = &ast->statements[stmt->first_child];
    if (body->kind != C_STMT_COMPOUND
        && !csane_structure_pp(body->kind)
        && !(stmt->kind == C_STMT_FOR && body->kind == C_STMT_EMPTY)) {
        token = csane_structure_token(file, stmt->tokens);
        if (token >= 0) {
            csane_rule_issue(file, file->tokens->tokens[token].offset,
                             "AST-BRACES",
                             "control body must use curly braces");
        }
    }
    if (stmt->kind != C_STMT_IF || stmt->else_child < 0) {
        return;
    }
    body = &ast->statements[stmt->else_child];
    if (body->kind == C_STMT_COMPOUND || body->kind == C_STMT_IF
        || csane_structure_pp(body->kind)) {
        return;
    }
    /* The else keyword is between the end of the then-body and the start
     * of its branch. Never report the branch token as an else keyword. */
    int32 begin = ast->statements[stmt->first_child].tokens.end;
    for (int32 i = begin; i < body->tokens.first; i += 1) {
        if (TOKEN_IS(&file->tokens->tokens[i], "else")) {
            csane_rule_issue(file, file->tokens->tokens[i].offset,
                             "AST-BRACES", "else body must use curly braces");
            break;
        }
    }
}

static void
csane_structure_alignment(CsaneFile *file, CStatement *stmt,
                          int32 switch_column) {
    int32 token;
    SourceLocation location;
    if ((stmt->kind != C_STMT_CASE && stmt->kind != C_STMT_DEFAULT)
        || switch_column < 0) {
        return;
    }
    token = csane_structure_token(file, stmt->tokens);
    if (token < 0) {
        return;
    }
    location = tokenization_token_location(file->tokens, token);
    if (location.column != switch_column) {
        csane_rule_issue(file, file->tokens->tokens[token].offset,
                         "AST-SWITCH-ALIGN",
                         "case/default must align with switch");
    }
}

static bool
csane_structure_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static void
csane_structure_artificial(CsaneFile *file, CStatement *stmt,
                           enum CStatementKind parent_kind) {
    int32 token;
    int32 offset;
    int32 start;
    SourceLocation loc;
    char *text;
    if (stmt->kind != C_STMT_COMPOUND
        || (parent_kind != C_STMT_COMPOUND
            && parent_kind != C_STMT_PP_BRANCH)) {
        return;
    }
    token = csane_structure_token(file, stmt->tokens);
    if (token < 0) {
        return;
    }
    loc = tokenization_token_location(file->tokens, token);
    if (loc.column < 12) {
        return;
    }
    offset = file->tokens->tokens[token].offset;
    text = file->tokens->text;
    start = offset;
    while ((start > 0) && (text[start - 1] != '\n')) {
        start -= 1;
    }
    /* The legacy rule limits this prohibition to blocks on lines indented
     * at least three four-space levels, not merely three AST scopes. */
    if (offset - start < 12) {
        return;
    }
    for (int32 i = 0; i < 12; i += 1) {
        if (text[start + i] != ' ') {
            return;
        }
    }
    csane_rule_issue(file, offset, "AST-ARTIFICIAL-BLOCK",
                     "artificial block at nesting level 3 or more");
}

static void
csane_structure_statements(CsaneFile *file, int32 first,
                           enum CStatementKind parent_kind,
                           int32 switch_column) {
    int32 last_line = -1;
    CAst *ast = file->ast;
    for (int32 i = first; i >= 0; i = ast->statements[i].next) {
        CStatement *stmt = &ast->statements[i];
        if (stmt->kind == C_STMT_DECLARATION) {
            csane_structure_declaration(file, stmt->declaration, &last_line);
        } else if (stmt->kind == C_STMT_FOR) {
            /* For initializers are independent from the enclosing scope. */
            int32 isolated = -1;
            csane_structure_declaration(file, stmt->declaration, &isolated);
        }
        csane_structure_statement(file, i, parent_kind, switch_column);
    }
}

static void
csane_structure_statement(CsaneFile *file, int32 index,
                          enum CStatementKind parent_kind,
                          int32 switch_column) {
    CStatement *stmt = &file->ast->statements[index];
    int32 own_switch = switch_column;
    int32 token;
    csane_structure_braces(file, stmt);
    csane_structure_alignment(file, stmt, switch_column);
    csane_structure_artificial(file, stmt, parent_kind);
    if (stmt->kind == C_STMT_SWITCH) {
        token = csane_structure_token(file, stmt->tokens);
        if (token >= 0) {
            own_switch = tokenization_token_location(file->tokens,
                                                     token).column;
        }
    }
    if (stmt->first_child >= 0) {
        csane_structure_statements(file, stmt->first_child,
                                   stmt->kind, own_switch);
    }
    if (stmt->else_child >= 0) {
        csane_structure_statements(file, stmt->else_child,
                                   stmt->kind, own_switch);
    }
}

static void csane_structure_externals(CsaneFile *, int32);

static void
csane_structure_external(CsaneFile *file, int32 index) {
    CExternal *e = &file->ast->externals[index];
    if (e->function_body >= 0) {
        csane_structure_statement(file, e->function_body, 0, -1);
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
