// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_LAYOUT_C)
#define CSANECHECK_LAYOUT_C

#include "cbase.h"
#include "csck.h"

/* Layout is checked against the original source spans. In particular, a
 * binary '*' is supplied by the expression AST, never guessed from the
 * token's spelling (which is also used by pointer declarators and unary *).
 */
static bool
csane_layout_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static bool
csane_layout_code(CsaneFile *file, int32 token) {
    if ((token < 0) || (token >= file->tokens->token_count)) {
        return false;
    }
    if (!file->preprocessor->token_roles) {
        return true;
    }
    return file->preprocessor->token_roles[token] == C_PP_TOKEN_CODE;
}

/* A gap containing a comment is not a whitespace-style violation. The
 * source may deliberately put a comment between operands and operators. */
static bool
csane_layout_gap(CsaneFile *file, int32 start, int32 end,
                 bool *horizontal, bool *separated) {
    char *text = file->tokens->text;
    bool space = false;
    bool newline = false;
    if (start < 0 || end < start || end > file->tokens->text_len) {
        return false;
    }
    for (int32 i = start; i < end; i += 1) {
        if (text[i] == ' ' || text[i] == '\t') {
            space = true;
        } else if (text[i] == '\n' || text[i] == '\r') {
            newline = true;
        } else {
            return false;
        }
    }
    *horizontal = space && !newline;
    *separated = space || newline;
    return true;
}

static bool
csane_layout_operator_spacing(CsaneFile *file, CExpression *expr,
                              bool tight) {
    Tokenization *tokens = file->tokens;
    CAst *ast = file->ast;
    Token *op;
    Token *left;
    Token *right;
    int32 l;
    int32 r;
    bool before_space = false;
    bool after_space = false;
    bool before_sep = false;
    bool after_sep = false;
    if (expr->left < 0 || expr->right < 0
        || expr->operator_token < 0
        || !csane_layout_code(file, expr->operator_token)) {
        return false;
    }
    l = token_range_last_significant(tokens,
                                     ast->expressions[expr->left].tokens);
    r = token_range_first_significant(tokens,
                                      ast->expressions[expr->right].tokens);
    if ((l < 0) || (r >= tokens->token_count)
        || (l >= expr->operator_token) || (r <= expr->operator_token)) {
        return false;
    }
    op = &tokens->tokens[expr->operator_token];
    left = &tokens->tokens[l];
    right = &tokens->tokens[r];
    if (!csane_layout_gap(file, left->offset + left->len, op->offset,
                          &before_space, &before_sep)
        || !csane_layout_gap(file, op->offset + op->len, right->offset,
                             &after_space, &after_sep)) {
        return false;
    }
    if (tight) {
        return before_space || after_space;
    }
    return !before_sep || !after_sep;
}

static bool
csane_layout_mandatory_binary(Token *op) {
    return TOKEN_IS(op, "+") || TOKEN_IS(op, "-")
           || TOKEN_IS(op, "&") || TOKEN_IS(op, "<<")
           || TOKEN_IS(op, ">>") || TOKEN_IS(op, "&&")
           || TOKEN_IS(op, "||") || TOKEN_IS(op, "==")
           || TOKEN_IS(op, "!=") || TOKEN_IS(op, "<")
           || TOKEN_IS(op, "<=") || TOKEN_IS(op, ">")
           || TOKEN_IS(op, ">=");
}

/* A declaration initializer has no C_EXPR_ASSIGN node: the declaration
 * parser deliberately leaves the value as a token range. Check its '='
 * against the declarator and that range, excluding VLA bounds and bitfields.
 */
static void
csane_layout_initializers(CsaneFile *file) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    for (int32 i = 0; i < ast->declarator_count; i += 1) {
        CDeclaratorNode *decl = &ast->declarators[i];
        TokenRange range = decl->extra;
        int32 right;
        int32 op_index;
        int32 left;
        Token *op;
        bool before_space = false;
        bool after_space = false;
        bool before_sep = false;
        bool after_sep = false;
        if (token_range_is_empty(tokens, range)) {
            continue;
        }
        right = token_range_first_significant(tokens, range);
        op_index = token_range_previous_significant(tokens,
                                                    (TokenRange){0, right},
                                                    right);
        if ((op_index < 0) || (op_index >= tokens->token_count)
            || !TOKEN_IS(&tokens->tokens[op_index], "=")
            || !csane_layout_code(file, op_index)) {
            continue;
        }
        left = token_range_previous_significant(tokens,
                                                (TokenRange){0, op_index},
                                                op_index);
        if (left < 0) {
            continue;
        }
        op = &tokens->tokens[op_index];
        if (!csane_layout_gap(file,
                              tokens->tokens[left].offset
                                  + tokens->tokens[left].len,
                              op->offset, &before_space, &before_sep)
            || !csane_layout_gap(file, op->offset + op->len,
                                 tokens->tokens[right].offset,
                                 &after_space, &after_sep)) {
            continue;
        }
        if (!before_sep || !after_sep) {
            csane_rule_issue(file, op->offset, "FMT-ASSIGN-SPACING",
                             "assignment requires surrounding whitespace");
        }
    }
    return;
}

static void
csane_layout_expressions(CsaneFile *file) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    for (int32 i = 0; i < ast->expression_count; i += 1) {
        CExpression *expr = &ast->expressions[i];
        Token *op;
        if (expr->operator_token < 0
            || expr->operator_token >= tokens->token_count) {
            continue;
        }
        op = &tokens->tokens[expr->operator_token];
        if (expr->kind == C_EXPR_BINARY && TOKEN_IS(op, "*")) {
            if (csane_layout_operator_spacing(file, expr, true)) {
                csane_rule_issue(file, op->offset, "FMT-MULTIPLY",
                                 "no spaces around multiplication");
            }
        } else if (expr->kind == C_EXPR_BINARY
                   && csane_layout_mandatory_binary(op)) {
            if (csane_layout_operator_spacing(file, expr, false)) {
                csane_rule_issue(file, op->offset, "FMT-BINARY-SPACING",
                                 "spaces required around binary operator");
            }
        } else if (expr->kind == C_EXPR_ASSIGN) {
            if (csane_layout_operator_spacing(file, expr, false)) {
                csane_rule_issue(file, op->offset, "FMT-ASSIGN-SPACING",
                                 "assignment requires surrounding whitespace");
            }
        }
    }
    return;
}

/* Unlike AST-based spacing, the placement of a continuation-line '=' must
 * also be checked in unsupported expressions, as long as it is C source and
 * not a preprocessor directive or macro replacement. */
static void
csane_layout_leading_assignment(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    char *text = tokens->text;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        int32 start;
        if (!TOKEN_IS(token, "=") || !csane_layout_code(file, i)) {
            continue;
        }
        start = token->offset;
        while (start > 0 && text[start - 1] != '\n') {
            start -= 1;
        }
        for (int32 j = start; j < token->offset; j += 1) {
            if (text[j] != ' ' && text[j] != '\t' && text[j] != '\r') {
                start = -1;
                break;
            }
        }
        if (start >= 0 && start < token->offset + 1) {
            csane_rule_issue(file, token->offset, "FMT-ASSIGN-EQ",
                             "assignment = must end the preceding line");
        }
    }
    return;
}

void
csane_layout_check(CsaneFile *file) {
    if (file->is_ryu || csane_layout_generated(file->path)) {
        return;
    }
    csane_layout_expressions(file);
    csane_layout_initializers(file);
    return;
}

void
csane_layout_assignment_check(CsaneFile *file) {
    if (!file->is_ryu && !csane_layout_generated(file->path)) {
        csane_layout_leading_assignment(file);
    }
}

#endif /* CSANECHECK_LAYOUT_C */
