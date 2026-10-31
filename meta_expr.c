// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_EXPR_C)
#define META_EXPR_C

#if !defined(TESTING_meta_expr)
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_meta_expr 1
#else
#define TESTING_meta_expr 0
#endif
#endif

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"

static void *c_ast_storage_grow(void *, int32 *, int32, int32);

typedef struct ExprCursor {
    CAst *ast;
    TokenRange range;
    int32 pos;
    int32 issue;
    int32 depth;
    enum CExprParseStatus status;
} ExprCursor;

/* Binary precedence numbers from meta_common start at one. Comma,
 * assignment, and conditional operators occupy the three lower levels. */
enum {
    EXPR_PREC_COMMA = 1,
    EXPR_PREC_ASSIGN = 2,
    EXPR_PREC_CONDITIONAL = 3,
    EXPR_PREC_PREFIX = 14,
};

static int32
expr_sig(ExprCursor *c, int32 i) {
    return token_range_next_significant(c->ast->tokenization, c->range, i - 1);
}

static int32
expr_next(ExprCursor *c) {
    return expr_sig(c, c->pos);
}

static bool
expr_at(ExprCursor *c, int32 i, char *text) {
    return (i < c->range.end)
           && TOKEN_IS(&c->ast->tokenization->tokens[i], text,
                       strlen32(text));
}

static void
expr_fail(ExprCursor *c, enum CExprParseStatus status, int32 at) {
    if (c->status == C_EXPR_PARSE_OK) {
        c->status = status;
        c->issue = at;
    }
    return;
}

static int32
expr_make(ExprCursor *c, enum CExpressionKind kind, TokenRange range,
          int32 operator_token, int32 left, int32 right) {
    CAst *ast = c->ast;
    int32 i = ast->expression_count;
    ast->expressions =
        c_ast_storage_grow(ast->expressions, &ast->expression_capacity,
                           i, SIZEOF(CExpression));
    ast->expressions[i] = (CExpression){
        .tokens = range, .type_tokens = {range.first, range.first},
        .kind = kind, .operator_token = operator_token,
        .left = left, .right = right, .third = -1,
        .first_argument = -1,
    };
    ast->expression_count += 1;
    return i;
}

static void
expr_append_arg(ExprCursor *c, int32 list, int32 *tail, int32 value,
                TokenRange designator) {
    CAst *ast = c->ast;
    int32 i = ast->argument_count;
    ast->arguments = c_ast_storage_grow(ast->arguments,
                                        &ast->argument_capacity,
                                        i, SIZEOF(CExprArgument));
    ast->arguments[i] = (CExprArgument){
        .designator = designator, .expression = value, .next = -1,
    };
    ast->argument_count += 1;
    if (*tail < 0) {
        ast->expressions[list].first_argument = i;
    } else {
        ast->arguments[*tail].next = i;
    }
    *tail = i;
    ast->expressions[list].argument_count += 1;
    return;
}

static int32
expr_match(ExprCursor *c, int32 open) {
    Tokenization *t = c->ast->tokenization;
    int32 close = token_range_matching_delimiter_forward(t, c->range, open);
    if (close < 0) {
        expr_fail(c, C_EXPR_PARSE_MALFORMED, open);
    }
    return close;
}

static int32 expr_parse_prec(ExprCursor *, int32);
static int32 expr_parse_initializer(ExprCursor *);

static int32
expr_parse_list(ExprCursor *c, int32 open, bool initializer, int32 callee) {
    CAst *ast = c->ast;
    int32 node;
    int32 tail = -1;
    int32 i = expr_sig(c, open + 1);
    char *closer = ")";
    enum CExpressionKind kind = C_EXPR_CALL;
    int32 first = open;
    if (initializer) {
        closer = "}";
        kind = C_EXPR_INITIALIZER_LIST;
    } else {
        first = ast->expressions[callee].tokens.first;
    }
    node = expr_make(c, kind, (TokenRange){first, open + 1},
                     open, callee, -1);
    c->pos = i;
    if (expr_at(c, i, closer)) {
        c->pos = expr_sig(c, i + 1);
        ast->expressions[node].tokens.end = i + 1;
        return node;
    }
    while (c->status == C_EXPR_PARSE_OK) {
        TokenRange designator = {c->pos, c->pos};
        int32 value;
        i = expr_next(c);
        if (initializer && (expr_at(c, i, ".")
                            || expr_at(c, i, "["))) {
            int32 start = i;
            while (expr_at(c, i, ".") || expr_at(c, i, "[")) {
                if (expr_at(c, i, ".")) {
                    int32 name = expr_sig(c, i + 1);
                    if ((name >= c->range.end)
                        || (ast->tokenization->tokens[name].kind
                            != TOKEN_IDENT)) {
                        expr_fail(c, C_EXPR_PARSE_MALFORMED, name);
                        return -1;
                    }
                    i = expr_sig(c, name + 1);
                } else {
                    int32 close = expr_match(c, i);
                    if (close < 0) {
                        return -1;
                    }
                    if (expr_sig(c, i + 1) == close) {
                        expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
                        return -1;
                    }
                    i = expr_sig(c, close + 1);
                }
            }
            designator = (TokenRange){start, i};
            if (!expr_at(c, i, "=")) {
                expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
                return -1;
            }
            c->pos = expr_sig(c, i + 1);
        }
        if (initializer) {
            value = expr_parse_initializer(c);
        } else {
            value = expr_parse_prec(c, EXPR_PREC_ASSIGN);
        }
        if (value < 0) {
            return -1;
        }
        expr_append_arg(c, node, &tail, value, designator);
        i = expr_next(c);
        if (expr_at(c, i, closer)) {
            c->pos = expr_sig(c, i + 1);
            ast->expressions[node].tokens.end = i + 1;
            return node;
        }
        if (!expr_at(c, i, ",")) {
            expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
            return -1;
        }
        c->pos = expr_sig(c, i + 1);
        if (initializer && expr_at(c, expr_next(c), closer)) {
            i = expr_next(c);
            c->pos = expr_sig(c, i + 1);
            ast->expressions[node].tokens.end = i + 1;
            return node;
        }
    }
    return -1;
}

static bool
expr_string_literal(Token *token) {
    char *text = token->text;
    int32 n = token->len;
    return (token->kind == TOKEN_LITERAL)
           && ((text[0] == '"')
               || ((n >= 2) && ((text[0] == 'L') || (text[0] == 'u')
                                || (text[0] == 'U')) && (text[1] == '"'))
               || ((n >= 3) && (text[0] == 'u') && (text[1] == '8')
                   && (text[2] == '"')));
}

static int32
expr_parse_primary(ExprCursor *c) {
    CAst *ast = c->ast;
    int32 i = expr_next(c);
    Token *token;
    int32 node;
    if (i >= c->range.end) {
        expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
        return -1;
    }
    token = &ast->tokenization->tokens[i];
    if (token->kind == TOKEN_LITERAL) {
        int32 end = i + 1;
        if (expr_string_literal(token)) {
            int32 next = expr_sig(c, end);
            while ((next < c->range.end)
                   && expr_string_literal(&ast->tokenization->tokens[next])) {
                end = next + 1;
                next = expr_sig(c, end);
            }
        }
        node = expr_make(c, C_EXPR_LITERAL, (TokenRange){i, end},
                         -1, -1, -1);
        c->pos = expr_sig(c, end);
        return node;
    }
    if (token->kind == TOKEN_IDENT) {
        if (TOKEN_IS(token, "_Generic") || TOKEN_IS(token, "__extension__")
            || TOKEN_IS(token, "typeof")
            || TOKEN_IS(token, "__typeof__")) {
            expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, i);
            return -1;
        }
        if (c_token_keyword(token) != 0) {
            expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
            return -1;
        }
        node = expr_make(c, C_EXPR_IDENTIFIER, (TokenRange){i, i + 1},
                         -1, -1, -1);
        c->pos = expr_sig(c, i + 1);
        return node;
    }
    if (expr_at(c, i, "(")) {
        int32 close = expr_match(c, i);
        int32 next;
        int32 value;
        if (close < 0) {
            return -1;
        }
        if (expr_at(c, expr_sig(c, i + 1), "{")) {
            expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, i);
            return -1;
        }
        if (c_ast_is_type_name(ast, (TokenRange){i + 1, close})) {
            TokenRange type = {i + 1, close};
            next = expr_sig(c, close + 1);
            c->pos = next;
            if (expr_at(c, next, "{")) {
                value = expr_parse_initializer(c);
                if (value < 0) {
                    return -1;
                }
                node = expr_make(c, C_EXPR_COMPOUND_LITERAL,
                                 (TokenRange){i,
                                     ast->expressions[value].tokens.end},
                                 i, value, -1);
            } else {
                value = expr_parse_prec(c, EXPR_PREC_PREFIX);
                if (value < 0) {
                    return -1;
                }
                node = expr_make(c, C_EXPR_CAST,
                                 (TokenRange){i,
                                     ast->expressions[value].tokens.end},
                                 i, value, -1);
            }
            ast->expressions[node].type_tokens = type;
            return node;
        }
        c->pos = expr_sig(c, i + 1);
        value = expr_parse_prec(c, EXPR_PREC_COMMA);
        if (value < 0) {
            return -1;
        }
        next = expr_next(c);
        if (!expr_at(c, next, ")")) {
            expr_fail(c, C_EXPR_PARSE_MALFORMED, next);
            return -1;
        }
        c->pos = expr_sig(c, next + 1);
        return expr_make(c, C_EXPR_PAREN, (TokenRange){i, next + 1},
                         i, value, -1);
    }
    if (expr_at(c, i, "#") || expr_at(c, i, "{")) {
        expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, i);
    } else {
        expr_fail(c, C_EXPR_PARSE_MALFORMED, i);
    }
    return -1;
}

static int32
expr_parse_prefix(ExprCursor *c) {
    CAst *ast = c->ast;
    int32 i = expr_next(c);
    int32 node;
    int32 operand;
    enum CUnaryOp unary;
    bool size = expr_at(c, i, "sizeof");
    bool align = expr_at(c, i, "_Alignof");

    if (size || align) {
        int32 open = expr_sig(c, i + 1);
        int32 close = -1;
        int32 after;
        enum CExpressionKind kind;
        if (expr_at(c, open, "(")) {
            close = expr_match(c, open);
            if (close < 0) {
                return -1;
            }
            after = expr_sig(c, close + 1);
            if (!expr_at(c, after, "{")
                && c_ast_is_type_name(ast, (TokenRange){open + 1, close})) {
                c->pos = expr_sig(c, close + 1);
                kind = C_EXPR_SIZEOF_TYPE;
                if (align) {
                    kind = C_EXPR_ALIGNOF_TYPE;
                }
                node = expr_make(c, kind, (TokenRange){i, close + 1},
                                 i, -1, -1);
                ast->expressions[node].type_tokens =
                    (TokenRange){open + 1, close};
                return node;
            }
        }
        if (align) {
            expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, i);
            return -1;
        }
        c->pos = open;
        operand = expr_parse_prec(c, EXPR_PREC_PREFIX);
        if (operand < 0) {
            return -1;
        }
        return expr_make(c, C_EXPR_SIZEOF_EXPR,
                         (TokenRange){i, ast->expressions[operand].tokens.end},
                         i, operand, -1);
    }
    if (i < c->range.end) {
        unary = c_token_unary_op(&ast->tokenization->tokens[i]);
        if (unary != 0) {
            c->pos = expr_sig(c, i + 1);
            operand = expr_parse_prec(c, EXPR_PREC_PREFIX);
            if (operand < 0) {
                return -1;
            }
            return expr_make(c, C_EXPR_UNARY,
                             (TokenRange){i,
                                 ast->expressions[operand].tokens.end},
                             i, operand, -1);
        }
    }
    node = expr_parse_primary(c);
    if (node < 0) {
        return -1;
    }
    while (c->status == C_EXPR_PARSE_OK) {
        int32 next = expr_next(c);
        int32 value;
        if (expr_at(c, next, "(")) {
            int32 close = expr_match(c, next);
            if (close < 0) {
                return -1;
            }
            node = expr_parse_list(c, next, false, node);
            if (node < 0) {
                return -1;
            }
            continue;
        }
        if (expr_at(c, next, "[")) {
            int32 close = expr_match(c, next);
            if (close < 0) {
                return -1;
            }
            c->pos = expr_sig(c, next + 1);
            value = expr_parse_prec(c, EXPR_PREC_COMMA);
            if (value < 0) {
                return -1;
            }
            if (!expr_at(c, expr_next(c), "]")) {
                expr_fail(c, C_EXPR_PARSE_MALFORMED, expr_next(c));
                return -1;
            }
            c->pos = expr_sig(c, close + 1);
            node = expr_make(c, C_EXPR_SUBSCRIPT,
                             (TokenRange){ast->expressions[node].tokens.first,
                                          close + 1},
                             next, node, value);
            continue;
        }
        if (expr_at(c, next, ".") || expr_at(c, next, "->")) {
            int32 member = expr_sig(c, next + 1);
            if ((member >= c->range.end)
                || (ast->tokenization->tokens[member].kind != TOKEN_IDENT)) {
                expr_fail(c, C_EXPR_PARSE_MALFORMED, member);
                return -1;
            }
            node = expr_make(c, C_EXPR_MEMBER,
                             (TokenRange){ast->expressions[node].tokens.first,
                                          member + 1},
                             next, node, -1);
            /* The member name itself is a token, not a lookup expression. */
            ast->expressions[node].right = member;
            c->pos = expr_sig(c, member + 1);
            continue;
        }
        if (expr_at(c, next, "++") || expr_at(c, next, "--")) {
            node = expr_make(c, C_EXPR_POSTFIX,
                             (TokenRange){ast->expressions[node].tokens.first,
                                          next + 1},
                             next, node, -1);
            c->pos = expr_sig(c, next + 1);
            continue;
        }
        break;
    }
    return node;
}

static int32
expr_parse_prec(ExprCursor *c, int32 min_prec) {
    CAst *ast = c->ast;
    int32 lhs;
    if (c->depth >= 128) {
        expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, expr_next(c));
        return -1;
    }
    c->depth += 1;
    lhs = expr_parse_prefix(c);
    while ((lhs >= 0) && (c->status == C_EXPR_PARSE_OK)) {
        int32 op = expr_next(c);
        int32 prec = 0;
        int32 rhs;
        enum CExpressionKind kind = C_EXPR_BINARY;
        bool right_assoc = false;
        if (op >= c->range.end) {
            break;
        }
        if (expr_at(c, op, ",")) {
            prec = EXPR_PREC_COMMA;
            kind = C_EXPR_COMMA;
        } else if (expr_at(c, op, "?")) {
            prec = EXPR_PREC_CONDITIONAL;
            kind = C_EXPR_CONDITIONAL;
            right_assoc = true;
        } else if (c_token_assign_op(&ast->tokenization->tokens[op]) != 0) {
            prec = EXPR_PREC_ASSIGN;
            kind = C_EXPR_ASSIGN;
            right_assoc = true;
        } else {
            enum CBinaryOp binary_op =
                c_token_binary_op(&ast->tokenization->tokens[op]);
            int32 binary = c_binary_op_precedence(binary_op);
            if (binary != 0) {
                prec = binary + EXPR_PREC_CONDITIONAL;
            }
        }
        if ((prec == 0) || (prec < min_prec)) {
            break;
        }
        c->pos = expr_sig(c, op + 1);
        if (kind == C_EXPR_CONDITIONAL) {
            int32 middle = expr_parse_prec(c, EXPR_PREC_COMMA);
            int32 colon = expr_next(c);
            int32 next;
            if (middle < 0) {
                break;
            }
            if (!expr_at(c, colon, ":")) {
                expr_fail(c, C_EXPR_PARSE_MALFORMED, colon);
                break;
            }
            c->pos = expr_sig(c, colon + 1);
            rhs = expr_parse_prec(c, prec);
            if (rhs < 0) {
                break;
            }
            next = expr_make(c, kind,
                             (TokenRange){
                                 ast->expressions[lhs].tokens.first,
                                 ast->expressions[rhs].tokens.end},
                             op, lhs, middle);
            ast->expressions[next].third = rhs;
            lhs = next;
            continue;
        }
        if (right_assoc) {
            rhs = expr_parse_prec(c, prec);
        } else {
            rhs = expr_parse_prec(c, prec + 1);
        }
        if (rhs < 0) {
            break;
        }
        lhs = expr_make(c, kind,
                        (TokenRange){ast->expressions[lhs].tokens.first,
                                     ast->expressions[rhs].tokens.end},
                        op, lhs, rhs);
    }
    c->depth -= 1;
    if (c->status != C_EXPR_PARSE_OK) {
        return -1;
    }
    return lhs;
}

static int32
expr_parse_initializer(ExprCursor *c) {
    int32 i = expr_next(c);
    if (expr_at(c, i, "{")) {
        int32 list;
        if (c->depth >= 128) {
            expr_fail(c, C_EXPR_PARSE_UNSUPPORTED, i);
            return -1;
        }
        c->depth += 1;
        list = expr_parse_list(c, i, true, -1);
        c->depth -= 1;
        return list;
    }
    return expr_parse_prec(c, EXPR_PREC_ASSIGN);
}

static CExprParseResult
expr_parse_root(CAst *ast, TokenRange range, bool initializer) {
    CExprParseResult result = {C_EXPR_PARSE_MALFORMED, -1, -1, -1};
    ExprCursor c;
    int32 previous_nodes;
    int32 previous_args;
    int32 next;
    if ((ast == NULL) || (ast->tokenization == NULL)
        || !token_range_is_valid(ast->tokenization, range)) {
        return result;
    }
    next = token_range_first_significant(ast->tokenization, range);
    if (next >= range.end) {
        result.status = C_EXPR_PARSE_NOT_EXPRESSION;
        return result;
    }
    previous_nodes = ast->expression_count;
    previous_args = ast->argument_count;
    c = (ExprCursor){.ast = ast, .range = range, .pos = next,
                     .issue = -1, .status = C_EXPR_PARSE_OK};
    if (initializer) {
        result.expression = expr_parse_initializer(&c);
    } else {
        result.expression = expr_parse_prec(&c, EXPR_PREC_COMMA);
    }
    next = expr_next(&c);
    if ((result.expression >= 0) && (c.status == C_EXPR_PARSE_OK)
        && (next != range.end)) {
        expr_fail(&c, C_EXPR_PARSE_MALFORMED, next);
    }
    result.status = c.status;
    result.issue_token = c.issue;
    if ((result.expression < 0) || (c.status != C_EXPR_PARSE_OK)) {
        ast->expression_count = previous_nodes;
        ast->argument_count = previous_args;
        result.expression = -1;
    } else {
        result.next_token = next;
    }
    return result;
}

CExprParseResult
c_ast_parse_expression(CAst *ast, TokenRange range) {
    return expr_parse_root(ast, range, false);
}

CExprParseResult
c_ast_parse_initializer(CAst *ast, TokenRange range) {
    return expr_parse_root(ast, range, true);
}

#if TESTING_meta_expr
#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"

static int32
expr_test_find(Tokenization *t, char *name, int32 occurrence) {
    int32 found = -1;
    TokenRange range = {0, t->token_count};
    for (int32 i = 0; i <= occurrence; i += 1) {
        found = token_range_next_text(t, range, found, name, strlen32(name));
    }
    ASSERT_LT_VAR(found, t->token_count);
    return found;
}

static void
expr_test_precedence(void) {
    char *source = "a = b + c*d - e << 2 && f || g, h = i = j";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    CExprParseResult result;
    int32 comma;
    int32 assign;
    int32 logical_or;
    int32 and_node;
    int32 shift;
    int32 sub;
    int32 add;
    int32 op;
    ASSERT(c_ast_init(&ast, &t));
    result = c_ast_parse_expression(&ast, (TokenRange){0, t.token_count});
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    comma = result.expression;
    ASSERT_EQ(ast.expressions[comma].kind, C_EXPR_COMMA);
    assign = ast.expressions[comma].left;
    ASSERT_EQ(ast.expressions[assign].kind, C_EXPR_ASSIGN);
    logical_or = ast.expressions[assign].right;
    ASSERT_EQ(ast.expressions[logical_or].kind, C_EXPR_BINARY);
    op = ast.expressions[logical_or].operator_token;
    ASSERT(TOKEN_IS(&t.tokens[op], "||"));
    and_node = ast.expressions[logical_or].left;
    op = ast.expressions[and_node].operator_token;
    ASSERT(TOKEN_IS(&t.tokens[op], "&&"));
    shift = ast.expressions[and_node].left;
    ASSERT(TOKEN_IS(&t.tokens[ast.expressions[shift].operator_token], "<<"));
    sub = ast.expressions[shift].left;
    ASSERT(TOKEN_IS(&t.tokens[ast.expressions[sub].operator_token], "-"));
    add = ast.expressions[sub].left;
    ASSERT(TOKEN_IS(&t.tokens[ast.expressions[add].operator_token], "+"));
    ASSERT(TOKEN_IS(&t.tokens[ast.expressions[
        ast.expressions[add].right].operator_token], "*"));
    assign = ast.expressions[comma].right;
    ASSERT_EQ(ast.expressions[assign].kind, C_EXPR_ASSIGN);
    ASSERT_EQ(ast.expressions[ast.expressions[assign].right].kind,
              C_EXPR_ASSIGN);
    c_ast_free(&ast);
    free_tokenization(&t);
    return;
}

static void
expr_test_types(void) {
    char *source = "typedef int Type; "
                   "(Type)+value; (value)+other; "
                   "sizeof(Type*); sizeof(value + 1); "
                   "_Alignof(Type); ((Type){ .x = 3 }).x;";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult declaration;
    CExprParseResult result;
    int32 start;
    TokenRange range;
    ASSERT(c_ast_init(&ast, &t));
    declaration = c_ast_parse_declaration(&ast,
                                          (TokenRange){0, t.token_count});
    ASSERT_EQ(declaration.status, C_DECL_PARSE_OK);
    start = declaration.next_token;
    range = (TokenRange){start, expr_test_find(&t, ";", 1)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_CAST);
    range = (TokenRange){expr_test_find(&t, "(", 1),
                         expr_test_find(&t, ";", 2)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_BINARY);
    range = (TokenRange){expr_test_find(&t, "sizeof", 0),
                         expr_test_find(&t, ";", 3)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_SIZEOF_TYPE);
    range = (TokenRange){expr_test_find(&t, "sizeof", 1),
                         expr_test_find(&t, ";", 4)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_SIZEOF_EXPR);
    range = (TokenRange){expr_test_find(&t, "_Alignof", 0),
                         expr_test_find(&t, ";", 5)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_ALIGNOF_TYPE);
    range = (TokenRange){expr_test_find(&t, "(", 5),
                         expr_test_find(&t, ";", 6)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_MEMBER);
    c_ast_free(&ast);
    free_tokenization(&t);
    return;
}

static void
expr_test_calls_and_initializers(void) {
    char *source = "object->fn(a, g(2, 3), array[i++])[0]";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    CExprParseResult result;
    int32 call;
    int32 arg;
    int32 first;
    TokenRange designator;
    ASSERT(c_ast_init(&ast, &t));
    result = c_ast_parse_expression(&ast, (TokenRange){0, t.token_count});
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_SUBSCRIPT);
    call = ast.expressions[result.expression].left;
    ASSERT_EQ(ast.expressions[call].kind, C_EXPR_CALL);
    ASSERT_EQ(ast.expressions[call].argument_count, 3);
    ASSERT_EQ(ast.expressions[ast.expressions[call].left].kind, C_EXPR_MEMBER);
    arg = ast.expressions[call].first_argument;
    ASSERT_EQ(ast.expressions[ast.arguments[arg].expression].kind,
              C_EXPR_IDENTIFIER);
    c_ast_free(&ast);
    free_tokenization(&t);
    source = "{ .x = 1, [2].y = f(3), {4, 5}, }";
    t = tokenize_with_flags(source, strlen32(source), TOKENIZE_SKIP_WHITESPACE);
    ASSERT(c_ast_init(&ast, &t));
    result = c_ast_parse_initializer(&ast, (TokenRange){0, t.token_count});
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[result.expression].kind, C_EXPR_INITIALIZER_LIST);
    ASSERT_EQ(ast.expressions[result.expression].argument_count, 3);
    first = ast.expressions[result.expression].first_argument;
    designator = ast.arguments[first].designator;
    ASSERT_LT_VAR(designator.first, designator.end);
    c_ast_free(&ast);
    free_tokenization(&t);
    return;
}

static void
expr_test_errors(void) {
    char *source = "f(a,)";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    CExprParseResult result;
    ASSERT(c_ast_init(&ast, &t));
    result = c_ast_parse_expression(&ast, (TokenRange){0, t.token_count});
    ASSERT_EQ(result.status, C_EXPR_PARSE_MALFORMED);
    ASSERT_EQ(ast.expression_count, 0);
    ASSERT_EQ(ast.argument_count, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
    source = "_Generic(x, int: 1)";
    t = tokenize(source, strlen32(source));
    ASSERT(c_ast_init(&ast, &t));
    result = c_ast_parse_expression(&ast, (TokenRange){0, t.token_count});
    ASSERT_EQ(result.status, C_EXPR_PARSE_UNSUPPORTED);
    ASSERT_EQ(ast.expression_count, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
    return;
}

typedef struct ExprTestCase {
    char *text;
    enum CExprParseStatus status;
    enum CExpressionKind kind;
} ExprTestCase;

static void
expr_test_more_syntax(void) {
    ExprTestCase cases[] = {
        {"a ? b : c ? d : e", C_EXPR_PARSE_OK, C_EXPR_CONDITIONAL},
        {"x ? a,b : c", C_EXPR_PARSE_OK, C_EXPR_CONDITIONAL},
        {"x += y *= 2", C_EXPR_PARSE_OK, C_EXPR_ASSIGN},
        {"f((a,b), c)", C_EXPR_PARSE_OK, C_EXPR_CALL},
        {"(*fn)(a)", C_EXPR_PARSE_OK, C_EXPR_CALL},
        {"ptr->x++", C_EXPR_PARSE_OK, C_EXPR_POSTFIX},
        {"sizeof *p", C_EXPR_PARSE_OK, C_EXPR_SIZEOF_EXPR},
        {"sizeof(int (*)(void))", C_EXPR_PARSE_OK, C_EXPR_SIZEOF_TYPE},
        {"sizeof (int){2}", C_EXPR_PARSE_OK, C_EXPR_SIZEOF_EXPR},
        {"(int (*)(void))fn", C_EXPR_PARSE_OK, C_EXPR_CAST},
        {"(unknown)+v", C_EXPR_PARSE_OK, C_EXPR_BINARY},
        {"\"foo\" \"bar\"", C_EXPR_PARSE_OK, C_EXPR_LITERAL},
        {"((int[]){1, 2})[0]", C_EXPR_PARSE_OK, C_EXPR_SUBSCRIPT},
        {"a?b", C_EXPR_PARSE_MALFORMED, 0},
        {"f(a,,b)", C_EXPR_PARSE_MALFORMED, 0},
        {"(int *)", C_EXPR_PARSE_MALFORMED, 0},
        {"++", C_EXPR_PARSE_MALFORMED, 0},
        {"sizeof()", C_EXPR_PARSE_MALFORMED, 0},
        {"1 2", C_EXPR_PARSE_MALFORMED, 0},
        {"({1;})", C_EXPR_PARSE_UNSUPPORTED, 0},
    };
    for (int32 i = 0; i < SIZEOF(cases)/SIZEOF(cases[0]); i += 1) {
        Tokenization tokens = tokenize(cases[i].text, strlen32(cases[i].text));
        CAst ast;
        CExprParseResult result;
        TokenRange range;
        ASSERT(c_ast_init(&ast, &tokens));
        range = (TokenRange){0, tokens.token_count};
        result = c_ast_parse_expression(&ast, range);
        if (result.status != cases[i].status) {
            error("Expression test %d: %s\n", i, cases[i].text);
        }
        ASSERT_EQ(result.status, cases[i].status);
        if (result.status == C_EXPR_PARSE_OK) {
            ASSERT_EQ(ast.expressions[result.expression].kind, cases[i].kind);
        } else {
            ASSERT_EQ(ast.expression_count, 0);
            ASSERT_EQ(ast.argument_count, 0);
        }
        c_ast_free(&ast);
        free_tokenization(&tokens);
    }
    return;
}

static void
expr_test_structure_and_rollback(void) {
    char *source = "a ? b : c ? d : e; f((a,b), c); "
                   "{ [i+1].field = (int[]){3, 4}, .x = 0 }; "
                   "f(a,);";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    TokenRange range;
    CExprParseResult result;
    int32 root;
    int32 first_arg;
    int32 count;
    ASSERT(c_ast_init(&ast, &tokens));
    range = (TokenRange){0, expr_test_find(&tokens, ";", 0)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    root = result.expression;
    ASSERT_EQ(ast.expressions[root].kind, C_EXPR_CONDITIONAL);
    ASSERT_EQ(ast.expressions[ast.expressions[root].third].kind,
              C_EXPR_CONDITIONAL);
    range = (TokenRange){expr_test_find(&tokens, "f", 0),
                         expr_test_find(&tokens, ";", 1)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    root = result.expression;
    ASSERT_EQ(ast.expressions[root].kind, C_EXPR_CALL);
    ASSERT_EQ(ast.expressions[root].argument_count, 2);
    first_arg = ast.expressions[root].first_argument;
    ASSERT_EQ(ast.expressions[ast.arguments[first_arg].expression].kind,
              C_EXPR_PAREN);
    range = (TokenRange){expr_test_find(&tokens, "{", 0),
                         expr_test_find(&tokens, ";", 2)};
    result = c_ast_parse_initializer(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_OK);
    root = result.expression;
    ASSERT_EQ(ast.expressions[root].argument_count, 2);
    first_arg = ast.expressions[root].first_argument;
    ASSERT_LT_VAR(ast.arguments[first_arg].designator.first,
                  ast.arguments[first_arg].designator.end);
    count = ast.expression_count;
    range = (TokenRange){expr_test_find(&tokens, "f", 1),
                         expr_test_find(&tokens, ";", 3)};
    result = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(result.status, C_EXPR_PARSE_MALFORMED);
    ASSERT_EQ(ast.expression_count, count);
    ASSERT_EQ(ast.expressions[root].kind, C_EXPR_INITIALIZER_LIST);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
expr_test_typedef_shadowing(void) {
    char *source = "typedef int T; int T; (T)+v";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult decl;
    CExprParseResult expr;
    int32 start;
    TokenRange range;
    ASSERT(c_ast_init(&ast, &tokens));
    decl = c_ast_parse_declaration(&ast,
                                   (TokenRange){0, tokens.token_count});
    ASSERT_EQ(decl.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, expr_test_find(&tokens, "T", 2)),
              C_NAME_TYPEDEF);
    decl = c_ast_parse_declaration(&ast,
                                   (TokenRange){decl.next_token,
                                                tokens.token_count});
    ASSERT_EQ(decl.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, expr_test_find(&tokens, "T", 2)),
              C_NAME_ORDINARY);
    start = decl.next_token;
    range = (TokenRange){start, tokens.token_count};
    expr = c_ast_parse_expression(&ast, range);
    ASSERT_EQ(expr.status, C_EXPR_PARSE_OK);
    ASSERT_EQ(ast.expressions[expr.expression].kind, C_EXPR_BINARY);
    ASSERT_EQ(ast.binding_count, 2);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

int
main(void) {
    expr_test_more_syntax();
    expr_test_structure_and_rollback();
    expr_test_typedef_shadowing();
    expr_test_precedence();
    expr_test_types();
    expr_test_calls_and_initializers();
    expr_test_errors();
    return 0;
}
#endif

#endif /* META_EXPR_C */
