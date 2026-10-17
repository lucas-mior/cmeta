// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONTEXT_C)
#define CSANECHECK_CONTEXT_C

#include "cbase.h"
#include "csanecheck.h"

/* Every expression refers to the original tokenization. Parentheses do not
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
    int32 token;
    if (index < 0) {
        return false;
    }
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0 || file->ast->expressions[index].kind != C_EXPR_IDENTIFIER) {
        return false;
    }
    TokenRange range = file->ast->expressions[index].tokens;
    token = token_range_first_significant(file->tokens, range);
    return token < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[token], name);
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
    int32 first;
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0) {
        return false;
    }
    expr = &file->ast->expressions[index];
    if (expr->kind != C_EXPR_LITERAL) {
        return false;
    }
    first = token_range_first_significant(file->tokens, expr->tokens);
    if (first >= expr->tokens.end) {
        return false;
    }
    token = &file->tokens->tokens[first];
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
csane_literal_zero(CsaneFile *file, int32 index) {
    CExpression *expr;
    int32 token;
    if (!csane_literal(file, index, false)) {
        return false;
    }
    index = csane_expr_unwrap(file->ast, index);
    expr = &file->ast->expressions[index];
    token = token_range_first_significant(file->tokens, expr->tokens);
    return TOKEN_IS(&file->tokens->tokens[token], "0");
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
csane_expr_same(CsaneFile *file, int32 a, int32 b, int32 depth) {
    CExpression *lhs;
    CExpression *rhs;
    Token *one;
    Token *two;
    int32 i;
    int32 j;
    if (depth > 32) {
        return false;
    }
    a = csane_expr_unwrap(file->ast, a);
    b = csane_expr_unwrap(file->ast, b);
    if (a < 0 || b < 0) {
        return a == b;
    }
    lhs = &file->ast->expressions[a];
    rhs = &file->ast->expressions[b];
    if (lhs->kind != rhs->kind) {
        return false;
    }
    if (lhs->kind == C_EXPR_IDENTIFIER || lhs->kind == C_EXPR_LITERAL) {
        i = token_range_first_significant(file->tokens, lhs->tokens);
        j = token_range_first_significant(file->tokens, rhs->tokens);
        one = &file->tokens->tokens[i];
        two = &file->tokens->tokens[j];
        return one->len == two->len
               && !memcmp64(one->text, two->text, one->len);
    }
    if (lhs->kind == C_EXPR_MEMBER || lhs->kind == C_EXPR_SUBSCRIPT) {
        if (!csane_expr_same(file, lhs->left, rhs->left, depth + 1)) {
            return false;
        }
        if (lhs->kind == C_EXPR_SUBSCRIPT) {
            return csane_expr_same(file, lhs->right, rhs->right, depth + 1);
        }
        one = &file->tokens->tokens[lhs->right];
        two = &file->tokens->tokens[rhs->right];
        return one->len == two->len
               && !memcmp64(one->text, two->text, one->len)
               && file->tokens->tokens[lhs->operator_token].len
                  == file->tokens->tokens[rhs->operator_token].len;
    }
    return false;
}

/* Only side-effect-free variable references are eligible for a merged bounds
 * assertion. Avoid treating calls or arithmetic with side effects as values. */
static bool
csane_bounds_reference(CsaneFile *file, int32 index, int32 depth) {
    CExpression *expr;
    if (depth > 32) {
        return false;
    }
    index = csane_expr_unwrap(file->ast, index);
    if (index < 0) {
        return false;
    }
    expr = &file->ast->expressions[index];
    if (expr->kind == C_EXPR_IDENTIFIER) {
        return true;
    }
    if (expr->kind == C_EXPR_MEMBER) {
        return csane_bounds_reference(file, expr->left, depth + 1);
    }
    if (expr->kind == C_EXPR_SUBSCRIPT) {
        int32 subscript = csane_expr_unwrap(file->ast, expr->right);
        return csane_bounds_reference(file, expr->left, depth + 1)
               && subscript >= 0
               && (csane_literal(file, subscript, false)
                   || file->ast->expressions[subscript].kind
                      == C_EXPR_IDENTIFIER);
    }
    return false;
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
csane_context_call(CsaneFile *file, int32 index) {
    CAst *ast = file->ast;
    CExpression *call = &ast->expressions[index];
    int32 first;
    if (call->kind != C_EXPR_CALL) {
        return;
    }
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

static void
csane_context_bounds(CsaneFile *file, int32 first, int32 second) {
    CAst *ast = file->ast;
    CStatement *a = &ast->statements[first];
    CStatement *b = &ast->statements[second];
    int32 left = csane_expr_unwrap(ast, a->expression);
    int32 right = csane_expr_unwrap(ast, b->expression);
    CExpression *upper;
    CExpression *lower;
    int32 x;
    int32 y;
    if (a->kind != C_STMT_EXPRESSION || b->kind != C_STMT_EXPRESSION
        || left < 0 || right < 0 || !csane_call_named(file, left, "ASSERT_GE")
        || !csane_call_named(file, right, "ASSERT_LT")) {
        return;
    }
    lower = &ast->expressions[left];
    upper = &ast->expressions[right];
    if (lower->argument_count != 2 || upper->argument_count != 2) {
        return;
    }
    x = csane_arg(file, left, 0);
    y = csane_arg(file, right, 0);
    if (!csane_bounds_reference(file, x, 0)
        || !csane_expr_same(file, x, y, 0)
        || !csane_literal_zero(file, csane_arg(file, left, 1))) {
        return;
    }
    /* The next statement must begin on the immediately following physical
     * line; blank lines and intervening statements must not be ignored. */
    int32 ending = token_range_last_significant(file->tokens, a->tokens);
    int32 beginning = token_range_first_significant(file->tokens, b->tokens);
    SourceLocation end = tokenization_token_location(file->tokens, ending);
    SourceLocation next = tokenization_token_location(file->tokens, beginning);
    if (next.line == end.line + 1) {
        csane_rule_issue(file, csane_expression_offset(file, left),
                         "STYLE-ASSERT-BOUNDS",
                         "use ASSERT_BETWEEN for adjacent bounds");
    }
}

static int32
csane_status_scan(CsaneFile *file, TokenRange range, int32 *first) {
    int32 count = 0;
    if (first != NULL) {
        *first = -1;
    }
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        int32 previous;
        if (token->kind != TOKEN_IDENT || !TOKEN_IS(token, "status")) {
            continue;
        }
        if (file->preprocessor->token_directives != NULL
            && file->preprocessor->token_directives[i] >= 0) {
            continue;
        }
        previous = tokenization_previous_significant(file->tokens, i);
        if (previous >= range.first
            && (TOKEN_IS(&file->tokens->tokens[previous], ".")
                || TOKEN_IS(&file->tokens->tokens[previous], "->"))) {
            continue;
        }
        if (first != NULL && *first < 0) {
            *first = i;
        }
        count += 1;
    }
    return count;
}

static int32
csane_status_refs(CsaneFile *file, TokenRange range) {
    return csane_status_scan(file, range, NULL);
}

static bool
csane_status_decl(CsaneFile *file, int32 statement) {
    CAst *ast = file->ast;
    CStatement *s = &ast->statements[statement];
    int32 first;
    int32 name;
    int32 end;
    if (s->kind != C_STMT_DECLARATION || s->declaration < 0) {
        return false;
    }
    CDeclaration *decl = &ast->declarations[s->declaration];
    if (decl->declarator_count != 1 || decl->is_typedef) {
        return false;
    }
    first = token_range_first_significant(file->tokens, decl->tokens);
    name = tokenization_next_significant(file->tokens, first);
    end = tokenization_next_significant(file->tokens, name);
    return first < decl->tokens.end && end < decl->tokens.end
           && TOKEN_IS(&file->tokens->tokens[first], "int32")
           && TOKEN_IS(&file->tokens->tokens[name], "status")
           && TOKEN_IS(&file->tokens->tokens[end], ";")
           && tokenization_next_significant(file->tokens, end)
              >= decl->tokens.end;
}

static bool
csane_status_assignment(CsaneFile *file, int32 statement) {
    CStatement *s = &file->ast->statements[statement];
    int32 root;
    if (s->kind != C_STMT_EXPRESSION || s->expression < 0) {
        return false;
    }
    root = csane_expr_unwrap(file->ast, s->expression);
    if (root < 0) {
        return false;
    }
    CExpression *expr = &file->ast->expressions[root];
    return expr->kind == C_EXPR_ASSIGN && csane_operator(file, expr, "=")
           && csane_expr_token(file, expr->left, "status")
           && csane_status_refs(file, s->tokens) == 1;
}

static void
csane_status_function(CsaneFile *file, int32 body) {
    CAst *ast = file->ast;
    CStatement *block = &ast->statements[body];
    int32 declaration = -1;
    int32 pairs = 0;
    int32 refs;
    if (block->kind != C_STMT_COMPOUND) {
        return;
    }
    refs = csane_status_refs(file, block->tokens);
    if (refs < 3 || !(refs & 1)) {
        return;
    }
    for (int32 i = block->first_child; i >= 0;
         i = ast->statements[i].next) {
        if (csane_status_decl(file, i)) {
            if (declaration >= 0) {
                return;
            }
            declaration = i;
        }
    }
    if (declaration < 0) {
        return;
    }
    /* Any nested shadowing, or other use outside the directly adjacent
     * pairs, makes this conservative recommendation inapplicable. */
    for (int32 i = ast->statements[declaration].next; i >= 0;) {
        int32 next = ast->statements[i].next;
        if (next >= 0 && csane_status_assignment(file, i)
            && csane_status_refs(file, ast->statements[next].tokens) == 1) {
            TokenRange arange = ast->statements[i].tokens;
            TokenRange urange = ast->statements[next].tokens;
            int32 assignment = -1;
            int32 use = -1;
            csane_status_scan(file, arange, &assignment);
            csane_status_scan(file, urange, &use);
            SourceLocation a = tokenization_token_location(file->tokens,
                                                           assignment);
            SourceLocation b = tokenization_token_location(file->tokens,
                                                           use);
            if (b.line == a.line + 1) {
                pairs += 1;
                i = ast->statements[next].next;
                continue;
            }
        }
        i = next;
    }
    if (pairs > 0 && refs == 1 + 2*pairs) {
        TokenRange range = ast->statements[declaration].tokens;
        int32 token = token_range_first_significant(file->tokens, range);
        csane_rule_issue(file, file->tokens->tokens[token].offset,
                         "STYLE-STATUS-TEMP",
                         "inline int32 status assignment/use pairs");
    }
}

static void csane_context_statements(CsaneFile *, int32);

static void
csane_context_statements(CsaneFile *file, int32 first) {
    CAst *ast = file->ast;
    for (int32 i = first; i >= 0; i = ast->statements[i].next) {
        CStatement *s = &ast->statements[i];
        if (!file->is_ryu && s->next >= 0) {
            csane_context_bounds(file, i, s->next);
        }
        if (s->first_child >= 0) {
            csane_context_statements(file, s->first_child);
        }
        if (s->else_child >= 0) {
            csane_context_statements(file, s->else_child);
        }
    }
}

static void csane_context_externals(CsaneFile *, int32);

static void
csane_context_externals(CsaneFile *file, int32 first) {
    CAst *ast = file->ast;
    for (int32 i = first; i >= 0; i = ast->externals[i].next) {
        CExternal *ext = &ast->externals[i];
        if (ext->function_body >= 0) {
            CStatement *body = &ast->statements[ext->function_body];
            csane_context_statements(file, body->first_child);
            csane_status_function(file, ext->function_body);
        }
        if (ext->first_child >= 0) {
            csane_context_externals(file, ext->first_child);
        }
    }
}

void
csane_context_check(CsaneFile *file) {
    CAst *ast = file->ast;
    for (int32 i = 0; i < ast->expression_count; i += 1) {
        if (ast->expressions[i].kind == C_EXPR_CALL) {
            csane_context_call(file, i);
        }
    }
    if (ast->external_count > 0) {
        csane_context_externals(file, 0);
    }
}

#endif /* CSANECHECK_CONTEXT_C */
