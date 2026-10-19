// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONTEXT_C)
#define CSANECHECK_CONTEXT_C

#include "cbase.h"
#include "csck.h"

/* Every expression retains original token indices. Parentheses do not
 * affect the identity of an operand for these syntactic guideline rules. */
static int32
csane_expr_unwrap(CAst *ast, int32 index) {
    while (index >= 0 && ast->expressions[index].kind == C_EXPR_PAREN) {
        index = ast->expressions[index].left;
    }
    return index;
}

static bool
csane_expr_token(CsaneFile *file, int32 index, char *name) {
    Tokenization *tokens = file->ast->tokenization;
    int32 token;
    if (index < 0) {
        return false;
    }
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0 || file->ast->expressions[index].kind != C_EXPR_IDENTIFIER) {
        return false;
    }
    TokenRange range = file->ast->expressions[index].tokens;
    token = token_range_first_significant(tokens, range);
    return token < tokens->token_count
           && TOKEN_IS(&tokens->tokens[token], name);
}

static bool
csane_call_named(CsaneFile *file, int32 index, char *name) {
    CExpression *expr;
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0) {
        return false;
    }
    expr = &file->ast->expressions[index];
    return expr->kind == C_EXPR_CALL
           && csane_expr_token(file, expr->left, name);
}

static int32
csane_arg(CsaneFile *file, int32 call, int32 position) {
    CExpression *expr = &file->ast->expressions[call];
    int32 argument = expr->first_argument;
    for (int32 i = 0; i < position && argument >= 0; i += 1) {
        argument = file->ast->arguments[argument].next;
    }
    return argument >= 0 ? file->ast->arguments[argument].expression : -1;
}

static bool
csane_literal(CsaneFile *file, int32 index, bool string) {
    CExpression *expr;
    Token *token;
    Tokenization *tokens = file->ast->tokenization;
    int32 first;
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0) {
        return false;
    }
    expr = &file->ast->expressions[index];
    if (expr->kind != C_EXPR_LITERAL) {
        return false;
    }
    first = token_range_first_significant(tokens, expr->tokens);
    if (first >= expr->tokens.end) {
        return false;
    }
    token = &tokens->tokens[first];
    if (string) {
        for (int32 i = 0; i < token->len; i += 1) {
            if (token->text[i] == '"') {
                return true;
            }
        }
        return false;
    }
    return token->len > 0 && token->text[0] >= '0'
           && token->text[0] <= '9';
}

/* Legacy ASSERT(x rel n) policy applies to decimal integer constants,
 * not floating-point literals or expressions that merely start with a digit. */
static bool
csane_decimal_integer(CsaneFile *file, int32 index) {
    CExpression *expr;
    Token *token;
    int32 first;
    if (!csane_literal(file, index, false)) {
        return false;
    }
    index = csane_expr_unwrap(file->ast, index);
    expr = &file->ast->expressions[index];
    first = token_range_first_significant(file->tokens, expr->tokens);
    token = &file->tokens->tokens[first];
    for (int32 i = 0; i < token->len; i += 1) {
        if (token->text[i] < '0' || token->text[i] > '9') {
            return false;
        }
    }
    return token->len > 0;
}

static bool
csane_operator(CsaneFile *file, CExpression *expr, char *op) {
    return expr->operator_token >= 0
           && TOKEN_IS(&file->tokens->tokens[expr->operator_token], op);
}

static int32
csane_expression_offset(CsaneFile *file, int32 index) {
    TokenRange range = file->ast->expressions[index].tokens;
    int32 first = token_range_first_significant(file->tokens, range);
    return first < file->tokens->token_count
           ? file->tokens->tokens[first].offset : 0;
}

static bool
csane_format_conversion(Token *token) {
    int32 i = 0;
    if (token->kind != TOKEN_LITERAL) {
        return false;
    }
    if (token->len > 2 && token->text[0] == 'u'
        && token->text[1] == '8') {
        i = 2;
    } else if (token->len > 2 && (token->text[0] == 'u'
                                  || token->text[0] == 'U'
                                  || token->text[0] == 'L')) {
        i = 1;
    }
    if (i + 3 >= token->len || token->text[i] != '"'
        || token->text[++i] != '%') {
        return false;
    }
    i += 1;
    while (i < token->len && (token->text[i] == '-'
           || token->text[i] == '+' || token->text[i] == ' '
           || token->text[i] == '#' || token->text[i] == '0')) {
        i += 1;
    }
    while (i < token->len && token->text[i] >= '0'
           && token->text[i] <= '9') {
        i += 1;
    }
    if (i < token->len && token->text[i] == '.') {
        i += 1;
        if (i < token->len && token->text[i] == '*') {
            i += 1;
        } else {
            while (i < token->len && token->text[i] >= '0'
                   && token->text[i] <= '9') {
                i += 1;
            }
        }
    }
    while (i < token->len && (token->text[i] == 'h'
           || token->text[i] == 'l' || token->text[i] == 'j'
           || token->text[i] == 'z' || token->text[i] == 't'
           || token->text[i] == 'L')) {
        i += 1;
    }
    return (i + 2 == token->len)
           && ((token->text[i] >= 'A' && token->text[i] <= 'Z')
               || (token->text[i] >= 'a' && token->text[i] <= 'z'))
           && token->text[i + 1] == '"';
}

static void
csane_context_projected_call(CsaneFile *file, int32 index) {
    CAst *ast = file->ast;
    CExpression *call = &ast->expressions[index];
    int32 first;
    if (call->kind != C_EXPR_CALL) {
        return;
    }
    /* The token-local pass already checks direct spelling. This legacy
     * fallback only covers bounded macro aliases projected by the AST. */
    int32 original = csane_expression_offset(file, index);
    TokenRange original_range = call->tokens;
    int32 original_name = token_range_first_significant(file->tokens,
                                                        original_range);
    if (original_name < original_range.end) {
        Token *name = &file->tokens->tokens[original_name];
        int32 open = tokenization_next_significant(file->tokens, original_name);
        int32 arg = tokenization_next_significant(file->tokens, open);
        bool projected_argument = open < original_range.end
                                  && arg < original_range.end
                                  && file->tokens->tokens[arg].kind
                                     == TOKEN_IDENT
                                  && csane_name_kind(&file->tokens->tokens[arg])
                                     == CSANE_NAME_MACRO;
        if (!projected_argument
            && (TOKEN_IS(name, "ASSERT") || TOKEN_IS(name, "STRLIT_LEN")
                || TOKEN_IS(name, "STREQUAL")
                || TOKEN_IS(name, "str_printf"))) {
            return;
        }
    }
    (void)original;
    first = csane_arg(file, index, 0);
    if (csane_call_named(file, index, "ASSERT")
        && call->argument_count == 1 && first >= 0) {
        int32 cmp = csane_expr_unwrap(ast, first);
        CExpression *expr = &ast->expressions[cmp];
        if (expr->kind == C_EXPR_BINARY
            && (csane_operator(file, expr, ">=")
                || csane_operator(file, expr, ">")
                || csane_operator(file, expr, "<=")
                || csane_operator(file, expr, "<"))
            && csane_decimal_integer(file, expr->right)
            && csane_expr_unwrap(ast, expr->left) >= 0
            && ast->expressions[csane_expr_unwrap(ast, expr->left)].kind
               == C_EXPR_IDENTIFIER) {
            csane_rule_issue(file, csane_expression_offset(file, index),
                             "STYLE-ASSERT-REL",
                             "use ASSERT_GE/GT/LE/LT for integer comparison");
        }
    }
    if (csane_call_named(file, index, "STRLIT_LEN")
        && call->argument_count == 1 && csane_literal(file, first, true)) {
        csane_rule_issue(file, csane_expression_offset(file, index),
                         "STYLE-STRLIT-LEN",
                         "STRLIT_LEN called on a literal string");
    }
    if (csane_call_named(file, index, "STREQUAL")) {
        for (int32 i = 0; i < call->argument_count; i += 1) {
            int32 arg = csane_expr_unwrap(ast, csane_arg(file, index, i));
            if (arg >= 0 && csane_call_named(file, arg, "STRLIT")
                && ast->expressions[arg].argument_count == 1
                && csane_literal(file, csane_arg(file, arg, 0), true)) {
                csane_rule_issue(file, csane_expression_offset(file, index),
                                 "STYLE-STREQUAL",
                                 "STREQUAL called with STRLIT literal");
                break;
            }
        }
    }
    if (csane_call_named(file, index, "str_printf")
        && call->argument_count >= 3) {
        int32 format = csane_expr_unwrap(ast, csane_arg(file, index, 1));
        if (format >= 0 && csane_literal(file, format, true)) {
            CExpression *expr = &ast->expressions[format];
            int32 token = token_range_first_significant(file->tokens,
                                                        expr->tokens);
            if (token < expr->tokens.end
                && tokenization_next_significant(file->tokens, token)
                   >= expr->tokens.end
                && csane_format_conversion(&file->tokens->tokens[token])) {
                csane_rule_issue(file, csane_expression_offset(file, format),
                                 "STYLE-PRINTF",
                                 "format contains only one conversion");
            }
        }
    }
}

void
csane_context_check(CsaneFile *file) {
    CAst *ast = file->ast;
    for (int32 i = 0; i < ast->expression_count; i += 1) {
        if (ast->expressions[i].kind == C_EXPR_CALL) {
            csane_context_projected_call(file, i);
        }
    }
}

#endif /* CSANECHECK_CONTEXT_C */
