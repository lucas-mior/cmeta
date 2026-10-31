// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_STMT_C)
#define META_STMT_C

#if !defined(TESTING_meta_stmt)
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_meta_stmt 1
#else
#define TESTING_meta_stmt 0
#endif
#endif

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"

static void *c_ast_storage_grow(void *, int32 *, int32, int32);

typedef struct StmtCursor {
    CAst *ast;
    CPreprocessorAnalysis *pp;
    TokenRange range;
    int32 pos;
    int32 depth;
} StmtCursor;

static int32
stmt_sig(StmtCursor *c, int32 start) {
    return token_range_next_significant(c->ast->tokenization, c->range,
                                        start - 1);
}

static bool
stmt_at(StmtCursor *c, int32 i, char *text) {
    return (i < c->range.end)
           && TOKEN_IS(&c->ast->tokenization->tokens[i], text,
                       strlen32(text));
}

static int32
stmt_directive(StmtCursor *c, int32 token) {
    int32 d;
    if ((c->pp == NULL) || (token >= c->range.end)
        || (token >= c->ast->tokenization->token_count)) {
        return -1;
    }
    d = c->pp->token_directives[token];
    if ((d >= 0) && (c->pp->directives[d].hash_token == token)) {
        return d;
    }
    return -1;
}

/* No C construct may consume tokens from both sides of a directive. */
static int32
stmt_boundary(StmtCursor *c, int32 start) {
    if (c->pp == NULL) {
        return c->range.end;
    }
    for (int32 i = start; i < c->range.end; i += 1) {
        if (stmt_directive(c, i) >= 0) {
            return i;
        }
    }
    return c->range.end;
}

static void
stmt_issue(StmtCursor *c, enum CParseIssueKind kind, int32 token,
           TokenRange range) {
    CAst *ast = c->ast;
    int32 i = ast->issue_count;
    ast->issues = c_ast_storage_grow(ast->issues, &ast->issue_capacity,
                                     i, SIZEOF(CParseIssue));
    ast->issues[i] = (CParseIssue){kind, token, range};
    ast->issue_count += 1;
}

static int32
stmt_new(StmtCursor *c, enum CStatementKind kind, int32 first) {
    CAst *ast = c->ast;
    int32 i = ast->statement_count;
    ast->statements = c_ast_storage_grow(ast->statements,
                                         &ast->statement_capacity,
                                         i, SIZEOF(CStatement));
    ast->statements[i] = (CStatement){
        .kind = kind, .tokens = {first, first},
        .condition = {first, first}, .for_init = {first, first},
        .for_step = {first, first}, .expression = -1,
        .for_init_expression = -1, .for_step_expression = -1,
        .declaration = -1,
        .first_child = -1, .next = -1, .else_child = -1,
        .token = -1, .directive = -1,
    };
    ast->statement_count += 1;
    return i;
}

static void
stmt_append(StmtCursor *c, int32 parent, int32 *tail, int32 child) {
    if (*tail < 0) {
        c->ast->statements[parent].first_child = child;
    } else {
        c->ast->statements[*tail].next = child;
    }
    *tail = child;
}

/* Match a brace-delimited construct without treating conditional branches
 * as concatenated C. Each preprocessor group is opaque while matching an
 * enclosing block; its alternatives are parsed independently afterwards. */
static int32
stmt_match(StmtCursor *c, int32 open) {
    Token *t = &c->ast->tokenization->tokens[open];
    char *left = "(";
    char *right = ")";
    int32 depth = 0;
    if (TOKEN_IS(t, "{")) {
        left = "{";
        right = "}";
    } else if (TOKEN_IS(t, "[")) {
        left = "[";
        right = "]";
    }
    for (int32 i = open; i < c->range.end; i += 1) {
        int32 directive = stmt_directive(c, i);
        if (directive >= 0) {
            bool skipped_group = false;
            for (int32 j = 0; j < c->pp->group_count; j += 1) {
                CPreprocessorGroup *g = &c->pp->groups[j];
                int32 b = g->first_branch;
                if ((b >= 0)
                    && (c->pp->branches[b].directive_index == directive)) {
                    if (g->endif_directive < 0) {
                        return -1;
                    }
                    i = c->pp->directives[g->endif_directive].tokens.end - 1;
                    skipped_group = true;
                    break;
                }
            }
            if (!skipped_group) {
                i = c->pp->directives[directive].tokens.end - 1;
            }
            continue;
        }
        if (TOKEN_IS(&c->ast->tokenization->tokens[i], left, 1)) {
            depth += 1;
        } else if (TOKEN_IS(&c->ast->tokenization->tokens[i], right, 1)) {
            depth -= 1;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

static int32
stmt_semicolon(StmtCursor *c, int32 first) {
    TokenRange range = {first, stmt_boundary(c, first)};
    TokenDelimiterDepth depth = {0};
    return token_range_next_text_at_depth(c->ast->tokenization, range,
                                          first - 1, depth, ";", 1);
}

static int32
stmt_recover(StmtCursor *c, int32 first) {
    int32 semi = stmt_semicolon(c, first);
    int32 end = stmt_boundary(c, first);
    if (semi < end) {
        return stmt_sig(c, semi + 1);
    }
    for (int32 i = stmt_sig(c, first); i < end; i = stmt_sig(c, i + 1)) {
        if (stmt_at(c, i, ";")) {
            return stmt_sig(c, i + 1);
        }
    }
    /* A bad statement before a closing brace cannot swallow its parent. */
    for (int32 i = stmt_sig(c, first); i < end; i = stmt_sig(c, i + 1)) {
        if (stmt_at(c, i, "}")) {
            return i;
        }
    }
    return end;
}

static int32
stmt_unsupported(StmtCursor *c, int32 first,
                 enum CParseIssueKind issue, int32 at) {
    int32 next = stmt_recover(c, first);
    int32 node = stmt_new(c, C_STMT_UNSUPPORTED, first);
    if (c->pos > next) {
        next = c->pos;
    }
    if (next <= first) {
        next = stmt_sig(c, first + 1);
    }
    c->ast->statements[node].tokens.end = next;
    stmt_issue(c, issue, at, (TokenRange){first, next});
    c->pos = next;
    return node;
}

static void
stmt_expression(StmtCursor *c, int32 node, TokenRange range) {
    CExprParseResult result;
    int32 first = stmt_sig(c, range.first);
    if (first >= range.end) {
        return;
    }
    result = c_ast_parse_expression(c->ast, range);
    if (result.status == C_EXPR_PARSE_OK) {
        c->ast->statements[node].expression = result.expression;
    } else {
        enum CParseIssueKind issue = C_PARSE_ISSUE_MALFORMED;
        if (result.status == C_EXPR_PARSE_UNSUPPORTED) {
            issue = C_PARSE_ISSUE_UNSUPPORTED;
        }
        stmt_issue(c, issue, result.issue_token, range);
    }
}

/* When a typedef comes from an unparsed header, "Name *variable;" can
 * otherwise be mistaken for the expression "Name * variable;". The C grammar
 * cannot resolve this without knowing what Name denotes. Probe a declaration
 * with Name provisionally bound as a typedef. If it parses, leave the block
 * item unsupported instead of committing a possibly incorrect expression.
 * Known ordinary names retain their unambiguous expression interpretation.
 */
static bool
stmt_ambiguous_pointer_decl(StmtCursor *c, int32 first, int32 semi) {
    CAst *ast = c->ast;
    Tokenization *tokens = ast->tokenization;
    int32 next = stmt_sig(c, first + 1);
    int32 declarations;
    int32 declarators;
    int32 records;
    int32 enumerators;
    int32 bindings;
    int32 scopes;
    int32 current_scope;
    CDeclParseResult result;

    if (first >= semi || tokens->tokens[first].kind != TOKEN_IDENT
        || !stmt_at(c, next, "*")
        || c_ast_lookup(ast, first) != C_NAME_UNKNOWN) {
        return false;
    }
    declarations = ast->declaration_count;
    declarators = ast->declarator_count;
    records = ast->record_count;
    enumerators = ast->enumerator_count;
    bindings = ast->binding_count;
    scopes = ast->scope_count;
    current_scope = ast->current_scope;
    c_ast_scope_push(ast);
    c_ast_bind(ast, first, C_NAME_TYPEDEF);
    result = c_ast_parse_declaration(ast, (TokenRange){first, semi + 1});
    ast->declaration_count = declarations;
    ast->declarator_count = declarators;
    ast->record_count = records;
    ast->enumerator_count = enumerators;
    ast->binding_count = bindings;
    ast->scope_count = scopes;
    ast->current_scope = current_scope;
    return result.status == C_DECL_PARSE_OK;
}

static int32 stmt_parse_one(StmtCursor *);

/* A declaration is a block item, not a directly controlled statement in
 * C11. Roll back its names if it appears after if/while/for/do or a label. */
static int32
stmt_parse_body(StmtCursor *c) {
    int32 bindings = c->ast->binding_count;
    StmtCursor child = *c;
    int32 body;
    child.depth += 1;
    body = stmt_parse_one(&child);
    c->pos = child.pos;
    if ((body >= 0)
        && (c->ast->statements[body].kind == C_STMT_DECLARATION)) {
        CStatement *stmt = &c->ast->statements[body];
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, stmt->tokens.first,
                   stmt->tokens);
        stmt->kind = C_STMT_UNSUPPORTED;
        c->ast->binding_count = bindings;
    }
    return body;
}

static void
stmt_parse_sequence(StmtCursor *c, int32 parent) {
    int32 tail = -1;
    while (stmt_sig(c, c->pos) < c->range.end) {
        int32 first = stmt_sig(c, c->pos);
        int32 child;
        c->pos = first;
        if (stmt_at(c, first, "}")) {
            stmt_issue(c, C_PARSE_ISSUE_MALFORMED, first,
                       (TokenRange){first, first + 1});
            c->pos = stmt_sig(c, first + 1);
            continue;
        }
        child = stmt_parse_one(c);
        if (child >= 0) {
            stmt_append(c, parent, &tail, child);
        }
        if (c->pos <= first) {
            stmt_issue(c, C_PARSE_ISSUE_MALFORMED, first,
                       (TokenRange){first, first + 1});
            c->pos = stmt_sig(c, first + 1);
        }
    }
}

static int32 stmt_parse_compound(StmtCursor *, int32);

/* A conditional group can select an `if (condition)` header, with one
 * shared braced body after #endif. The body is not an artificial block.
 * Keep it attached to the preprocessor group rather than treating it as an
 * independent statement or joining the conditions across alternatives.
 */
static int32
stmt_pp_shared_if_start(StmtCursor *c, TokenRange range) {
    Tokenization *tokens = c->ast->tokenization;
    int32 last = token_range_last_significant(tokens, range);
    if (last < range.first || !TOKEN_IS(&tokens->tokens[last], ")")) {
        return -1;
    }
    for (int32 i = token_range_first_significant(tokens, range);
         i < range.end;
         i = token_range_next_significant(tokens, range, i)) {
        if (TOKEN_IS(&tokens->tokens[i], "if")) {
            int32 open = token_range_next_significant(tokens, range, i);
            if (open < range.end
                && TOKEN_IS(&tokens->tokens[open], "(")) {
                StmtCursor inner = *c;
                inner.range = range;
                if (stmt_match(&inner, open) == last) {
                    return i;
                }
            }
        }
    }
    return -1;
}

static bool
stmt_pp_shared_if_body(StmtCursor *c, CPreprocessorGroup *group) {
    for (int32 b = group->first_branch; b >= 0;
         b = c->pp->branches[b].next_branch) {
        if (stmt_pp_shared_if_start(c, c->pp->branches[b].body_tokens)
            < 0) {
            return false;
        }
    }
    return group->first_branch >= 0;
}

static int32
stmt_pp(StmtCursor *c, int32 first, int32 directive) {
    CPreprocessorDirective *d = &c->pp->directives[directive];
    int32 group = -1;
    int32 node;
    CPreprocessorGroup *g;
    int32 tail;
    int32 following;
    bool shared;
    for (int32 i = 0; i < c->pp->group_count; i += 1) {
        int32 branch = c->pp->groups[i].first_branch;
        if ((branch >= 0)
            && (c->pp->branches[branch].directive_index == directive)) {
            group = i;
            break;
        }
    }
    if (group < 0) {
        node = stmt_new(c, C_STMT_PP_DIRECTIVE, first);
        c->ast->statements[node].directive = directive;
        c->ast->statements[node].tokens.end = d->tokens.end;
        c->pos = stmt_sig(c, d->tokens.end);
        return node;
    }
    g = &c->pp->groups[group];
    if ((g->endif_directive < 0)
        || (c->pp->directives[g->endif_directive].tokens.end > c->range.end)) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_PREPROCESSOR, first);
    }
    node = stmt_new(c, C_STMT_PP_GROUP, first);
    c->ast->statements[node].directive = group;
    c->ast->statements[node].tokens.end =
        c->pp->directives[g->endif_directive].tokens.end;
    tail = -1;
    following = stmt_sig(c, c->ast->statements[node].tokens.end);
    shared = stmt_at(c, following, "{")
             && stmt_pp_shared_if_body(c, g);
    for (int32 b = g->first_branch; b >= 0;
         b = c->pp->branches[b].next_branch) {
        CPreprocessorBranch *branch = &c->pp->branches[b];
        StmtCursor inner = *c;
        int32 part = stmt_new(c, C_STMT_PP_BRANCH, branch->body_tokens.first);
        int32 incomplete_if;
        inner.range = branch->body_tokens;
        inner.pos = inner.range.first;
        inner.depth += 1;
        c->ast->statements[part].directive = branch->directive_index;
        c->ast->statements[part].tokens = branch->body_tokens;
        stmt_append(c, node, &tail, part);
        /* No conditional alternative can export a typedef to its siblings. */
        c_ast_scope_push(c->ast);
        incomplete_if = shared
            ? stmt_pp_shared_if_start(c, branch->body_tokens) : -1;
        if (incomplete_if >= 0) {
            inner.range.end = incomplete_if;
        }
        stmt_parse_sequence(&inner, part);
        if (incomplete_if >= 0) {
            StmtCursor pending = inner;
            int32 last_child = c->ast->statements[part].first_child;
            int32 opaque = stmt_new(&pending, C_STMT_UNSUPPORTED,
                                    incomplete_if);
            c->ast->statements[opaque].tokens =
                (TokenRange){incomplete_if, branch->body_tokens.end};
            if (last_child < 0) {
                c->ast->statements[part].first_child = opaque;
            } else {
                while (c->ast->statements[last_child].next >= 0) {
                    last_child = c->ast->statements[last_child].next;
                }
                c->ast->statements[last_child].next = opaque;
            }
            stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, incomplete_if,
                       c->ast->statements[opaque].tokens);
        }
        c_ast_scope_pop(c->ast);
    }
    c->pos = stmt_sig(c, c->ast->statements[node].tokens.end);
    if (shared) {
        int32 body = stmt_parse_compound(c, c->pos);
        if (body >= 0) {
            c->ast->statements[node].else_child = body;
            c->ast->statements[node].tokens.end =
                c->ast->statements[body].tokens.end;
        }
    }
    return node;
}

static int32
stmt_parse_compound(StmtCursor *c, int32 first) {
    int32 end = stmt_match(c, first);
    int32 node;
    StmtCursor inner;
    if (end < 0) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, first);
    }
    node = stmt_new(c, C_STMT_COMPOUND, first);
    c->ast->statements[node].tokens.end = end + 1;
    inner = *c;
    inner.range = (TokenRange){first + 1, end};
    inner.pos = first + 1;
    inner.depth += 1;
    c_ast_scope_push(c->ast);
    stmt_parse_sequence(&inner, node);
    c_ast_scope_pop(c->ast);
    c->pos = stmt_sig(c, end + 1);
    return node;
}

static int32
stmt_parse_control(StmtCursor *c, int32 first, enum CStatementKind kind) {
    int32 open = stmt_sig(c, first + 1);
    int32 close;
    int32 node;
    int32 body;
    bool opaque_condition;
    if (!stmt_at(c, open, "(")) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    close = stmt_match(c, open);
    if (close < 0) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    opaque_condition = stmt_boundary(c, open + 1) < close;
    node = stmt_new(c, kind, first);
    c->ast->statements[node].condition = (TokenRange){open + 1, close};
    if (opaque_condition) {
        /* Keep the controlled statement even when the condition contains
         * conditional macro alternatives. The condition itself is not
         * classified as a C expression without preprocessing it. */
        stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, open + 1,
                   (TokenRange){open + 1, close});
    } else if (stmt_sig(c, open + 1) == close) {
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, open,
                   (TokenRange){open, close + 1});
    } else {
        stmt_expression(c, node, (TokenRange){open + 1, close});
    }
    c->pos = stmt_sig(c, close + 1);
    body = stmt_parse_body(c);
    if (body < 0) {
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, close,
                   (TokenRange){first, close + 1});
        c->ast->statements[node].tokens.end = close + 1;
        return node;
    }
    c->ast->statements[node].first_child = body;
    c->ast->statements[node].tokens.end = c->ast->statements[body].tokens.end;
    if ((kind == C_STMT_IF) && stmt_at(c, c->pos, "else")) {
        int32 branch;
        c->pos = stmt_sig(c, c->pos + 1);
        branch = stmt_parse_body(c);
        if (branch >= 0) {
            c->ast->statements[node].else_child = branch;
            c->ast->statements[node].tokens.end =
                c->ast->statements[branch].tokens.end;
        } else {
            stmt_issue(c, C_PARSE_ISSUE_MALFORMED, c->pos,
                       (TokenRange){first, c->pos});
        }
    }
    return node;
}

static int32
stmt_parse_for(StmtCursor *c, int32 first) {
    int32 open = stmt_sig(c, first + 1);
    int32 close;
    int32 a;
    int32 b;
    int32 node;
    int32 body;
    TokenDelimiterDepth zero = {0};
    TokenRange header;
    if (!stmt_at(c, open, "(")) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    close = stmt_match(c, open);
    if (close < 0) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    if (stmt_boundary(c, open + 1) < close) {
        /* A conditional header may contain different init/step clauses.
         * Do not join branches or guess their expressions. Still parse its
         * controlled statement as a genuine for-loop body. */
        node = stmt_new(c, C_STMT_FOR, first);
        c->ast->statements[node].condition = (TokenRange){open + 1, close};
        stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, open + 1,
                   (TokenRange){open + 1, close});
        c_ast_scope_push(c->ast);
        c->pos = stmt_sig(c, close + 1);
        body = stmt_parse_body(c);
        if (body >= 0) {
            c->ast->statements[node].first_child = body;
            c->ast->statements[node].tokens.end =
                c->ast->statements[body].tokens.end;
        } else {
            stmt_issue(c, C_PARSE_ISSUE_MALFORMED, close,
                       (TokenRange){first, close + 1});
            c->ast->statements[node].tokens.end = close + 1;
        }
        c_ast_scope_pop(c->ast);
        return node;
    }
    header = (TokenRange){open + 1, close};
    a = token_range_next_text_at_depth(c->ast->tokenization, header,
                                       open, zero, ";", 1);
    if (a >= close) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    b = token_range_next_text_at_depth(c->ast->tokenization, header,
                                       a, zero, ";", 1);
    if (b >= close) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, a);
    }
    node = stmt_new(c, C_STMT_FOR, first);
    c->ast->statements[node].for_init = (TokenRange){open + 1, a};
    c->ast->statements[node].condition = (TokenRange){a + 1, b};
    c->ast->statements[node].for_step = (TokenRange){b + 1, close};
    c_ast_scope_push(c->ast);
    if (stmt_sig(c, open + 1) < a) {
        CDeclParseResult decl =
            c_ast_parse_declaration(c->ast, (TokenRange){open + 1, a + 1});
        if (decl.status == C_DECL_PARSE_OK) {
            c->ast->statements[node].declaration = decl.declaration;
        } else if (decl.status == C_DECL_PARSE_NOT_DECLARATION
                   && stmt_ambiguous_pointer_decl(c,
                                                  stmt_sig(c, open + 1), a)) {
            stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, open + 1,
                       (TokenRange){open + 1, a + 1});
        } else if (decl.status == C_DECL_PARSE_NOT_DECLARATION) {
            CExprParseResult expr =
                c_ast_parse_expression(c->ast, (TokenRange){open + 1, a});
            if (expr.status == C_EXPR_PARSE_OK) {
                c->ast->statements[node].for_init_expression = expr.expression;
            } else {
                stmt_issue(c, expr.status == C_EXPR_PARSE_UNSUPPORTED
                           ? C_PARSE_ISSUE_UNSUPPORTED
                           : C_PARSE_ISSUE_MALFORMED,
                           expr.issue_token, (TokenRange){open + 1, a});
            }
        } else {
            stmt_issue(c, decl.status == C_DECL_PARSE_UNSUPPORTED
                       ? C_PARSE_ISSUE_UNSUPPORTED : C_PARSE_ISSUE_MALFORMED,
                       decl.issue_token, (TokenRange){open + 1, a + 1});
        }
    }
    /* The header's source spans are retained even when not analyzed here. */
    if (stmt_sig(c, a + 1) < b) {
        CExprParseResult expr =
            c_ast_parse_expression(c->ast, (TokenRange){a + 1, b});
        if (expr.status == C_EXPR_PARSE_OK) {
            c->ast->statements[node].expression = expr.expression;
        } else {
            stmt_issue(c, expr.status == C_EXPR_PARSE_UNSUPPORTED
                       ? C_PARSE_ISSUE_UNSUPPORTED : C_PARSE_ISSUE_MALFORMED,
                       expr.issue_token, (TokenRange){a + 1, b});
        }
    }
    if (stmt_sig(c, b + 1) < close) {
        CExprParseResult expr =
            c_ast_parse_expression(c->ast, (TokenRange){b + 1, close});
        if (expr.status == C_EXPR_PARSE_OK) {
            c->ast->statements[node].for_step_expression = expr.expression;
        } else {
            stmt_issue(c, expr.status == C_EXPR_PARSE_UNSUPPORTED
                       ? C_PARSE_ISSUE_UNSUPPORTED : C_PARSE_ISSUE_MALFORMED,
                       expr.issue_token, (TokenRange){b + 1, close});
        }
    }
    c->pos = stmt_sig(c, close + 1);
    body = stmt_parse_body(c);
    if (body >= 0) {
        c->ast->statements[node].first_child = body;
        c->ast->statements[node].tokens.end =
            c->ast->statements[body].tokens.end;
    } else {
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, close,
                   (TokenRange){first, close + 1});
        c->ast->statements[node].tokens.end = close + 1;
    }
    c_ast_scope_pop(c->ast);
    return node;
}

static int32
stmt_parse_do(StmtCursor *c, int32 first) {
    int32 node;
    int32 body;
    int32 open;
    int32 close;
    int32 semi;
    c->pos = stmt_sig(c, first + 1);
    body = stmt_parse_body(c);
    if (body < 0) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, c->pos);
    }
    if (!stmt_at(c, c->pos, "while")) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, c->pos);
    }
    open = stmt_sig(c, c->pos + 1);
    close = stmt_at(c, open, "(") ? stmt_match(c, open) : -1;
    if (close < 0) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, open);
    }
    semi = stmt_sig(c, close + 1);
    if (!stmt_at(c, semi, ";")) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, semi);
    }
    node = stmt_new(c, C_STMT_DO, first);
    c->ast->statements[node].first_child = body;
    c->ast->statements[node].condition = (TokenRange){open + 1, close};
    if (stmt_boundary(c, open + 1) < close) {
        stmt_issue(c, C_PARSE_ISSUE_PREPROCESSOR, open,
                   (TokenRange){open, close + 1});
    } else if (stmt_sig(c, open + 1) == close) {
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, open,
                   (TokenRange){open, close + 1});
    } else {
        stmt_expression(c, node, (TokenRange){open + 1, close});
    }
    c->ast->statements[node].tokens.end = semi + 1;
    c->pos = stmt_sig(c, semi + 1);
    return node;
}

static int32
stmt_parse_label(StmtCursor *c, int32 first,
                 enum CStatementKind kind, int32 colon) {
    int32 node = stmt_new(c, kind, first);
    int32 body;
    if (kind == C_STMT_CASE) {
        c->ast->statements[node].condition = (TokenRange){first + 1, colon};
        stmt_expression(c, node, (TokenRange){first + 1, colon});
    } else if (kind == C_STMT_LABEL) {
        c->ast->statements[node].token = first;
    }
    c->pos = stmt_sig(c, colon + 1);
    body = stmt_parse_body(c);
    if (body >= 0) {
        c->ast->statements[node].first_child = body;
        c->ast->statements[node].tokens.end =
            c->ast->statements[body].tokens.end;
    } else {
        stmt_issue(c, C_PARSE_ISSUE_MALFORMED, colon,
                   (TokenRange){first, colon + 1});
        c->ast->statements[node].tokens.end = colon + 1;
    }
    return node;
}

static int32
stmt_parse_one(StmtCursor *c) {
    int32 first = stmt_sig(c, c->pos);
    int32 next = stmt_sig(c, first + 1);
    int32 semi;
    int32 node;
    Token *token;
    if ((first >= c->range.end) || stmt_at(c, first, "}")) {
        return -1;
    }
    if (c->depth >= 128) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_UNSUPPORTED, first);
    }
    if (stmt_directive(c, first) >= 0) {
        return stmt_pp(c, first, stmt_directive(c, first));
    }
    if (stmt_at(c, first, "{")) {
        return stmt_parse_compound(c, first);
    }
    if (stmt_at(c, first, "_Static_assert")) {
        int32 open = next;
        int32 close = -1;
        int32 end = stmt_boundary(c, first);
        int32 after = end;
        TokenRange args;
        TokenDelimiterDepth depth = {0};
        int32 comma = -1;
        if (stmt_at(c, open, "(")) {
            close = stmt_match(c, open);
        }
        if (close >= 0) {
            after = stmt_sig(c, close + 1);
        }
        args = (TokenRange){open + 1, close};
        if (close > open + 1) {
            comma = token_range_next_text_at_depth(c->ast->tokenization,
                                                   args, open, depth, ",", 1);
        }
        if (comma <= open + 1 || comma >= close - 1 || after >= end
            || !stmt_at(c, after, ";")) {
            return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, first);
        }
        node = stmt_new(c, C_STMT_STATIC_ASSERT, first);
        c->ast->statements[node].tokens.end = after + 1;
        c->pos = stmt_sig(c, after + 1);
        return node;
    }
    if (stmt_at(c, first, ";")) {
        node = stmt_new(c, C_STMT_EMPTY, first);
        c->ast->statements[node].tokens.end = first + 1;
        c->pos = next;
        return node;
    }
    if (stmt_at(c, first, "if")) {
        return stmt_parse_control(c, first, C_STMT_IF);
    }
    if (stmt_at(c, first, "while")) {
        return stmt_parse_control(c, first, C_STMT_WHILE);
    }
    if (stmt_at(c, first, "switch")) {
        return stmt_parse_control(c, first, C_STMT_SWITCH);
    }
    if (stmt_at(c, first, "for")) {
        return stmt_parse_for(c, first);
    }
    if (stmt_at(c, first, "do")) {
        return stmt_parse_do(c, first);
    }
    if (stmt_at(c, first, "case") || stmt_at(c, first, "default")) {
        TokenRange limited = {first, stmt_boundary(c, first)};
        TokenDelimiterDepth depth = {0};
        int32 colon = token_range_next_text_at_depth(c->ast->tokenization,
                                                     limited, first, depth,
                                                     ":", 1);
        if ((colon >= limited.end) || ((colon == next)
             && stmt_at(c, first, "case"))) {
            return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, first);
        }
        return stmt_parse_label(c, first,
                                stmt_at(c, first, "case")
                                ? C_STMT_CASE : C_STMT_DEFAULT, colon);
    }
    token = &c->ast->tokenization->tokens[first];
    if ((token->kind == TOKEN_IDENT) && stmt_at(c, next, ":")) {
        return stmt_parse_label(c, first, C_STMT_LABEL, next);
    }
    semi = stmt_semicolon(c, first);
    if (semi >= stmt_boundary(c, first)) {
        return stmt_unsupported(c, first, C_PARSE_ISSUE_MALFORMED, first);
    }
    if (stmt_at(c, first, "return") || stmt_at(c, first, "break")
        || stmt_at(c, first, "continue") || stmt_at(c, first, "goto")) {
        enum CStatementKind kind = C_STMT_RETURN;
        if (stmt_at(c, first, "break")) {
            kind = C_STMT_BREAK;
        } else if (stmt_at(c, first, "continue")) {
            kind = C_STMT_CONTINUE;
        } else if (stmt_at(c, first, "goto")) {
            kind = C_STMT_GOTO;
        }
        node = stmt_new(c, kind, first);
        if (kind == C_STMT_RETURN) {
            stmt_expression(c, node, (TokenRange){next, semi});
        } else if (kind == C_STMT_GOTO) {
            if ((next >= semi) || (next + 1 != semi)
                || (c->ast->tokenization->tokens[next].kind != TOKEN_IDENT)) {
                stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, first,
                           (TokenRange){first, semi + 1});
            } else {
                c->ast->statements[node].token = next;
            }
        } else if (next != semi) {
            stmt_issue(c, C_PARSE_ISSUE_MALFORMED, next,
                       (TokenRange){first, semi + 1});
        }
    } else {
        CDeclParseResult decl =
            c_ast_parse_declaration(c->ast, (TokenRange){first, semi + 1});
        if (decl.status == C_DECL_PARSE_OK) {
            node = stmt_new(c, C_STMT_DECLARATION, first);
            c->ast->statements[node].declaration = decl.declaration;
        } else if (decl.status == C_DECL_PARSE_NOT_DECLARATION
                   && stmt_ambiguous_pointer_decl(c, first, semi)) {
            node = stmt_new(c, C_STMT_UNSUPPORTED, first);
            stmt_issue(c, C_PARSE_ISSUE_UNSUPPORTED, first,
                       (TokenRange){first, semi + 1});
        } else if (decl.status == C_DECL_PARSE_NOT_DECLARATION) {
            node = stmt_new(c, C_STMT_EXPRESSION, first);
            stmt_expression(c, node, (TokenRange){first, semi});
        } else {
            enum CParseIssueKind issue = C_PARSE_ISSUE_MALFORMED;
            if (decl.status == C_DECL_PARSE_UNSUPPORTED) {
                issue = C_PARSE_ISSUE_UNSUPPORTED;
            }
            node = stmt_new(c, C_STMT_UNSUPPORTED, first);
            stmt_issue(c, issue, decl.issue_token,
                       (TokenRange){first, semi + 1});
        }
    }
    c->ast->statements[node].tokens.end = semi + 1;
    c->pos = stmt_sig(c, semi + 1);
    return node;
}

CStmtParseResult
c_ast_parse_statement(CAst *ast, TokenRange range, CPreprocessorAnalysis *pp) {
    CStmtParseResult result = {-1, -1, -1, 0};
    CPreprocessorAnalysis local;
    StmtCursor c;
    int32 before;
    bool owned = false;
    if ((ast == NULL) || (ast->tokenization == NULL)
        || !token_range_is_valid(ast->tokenization, range)
        || ((pp != NULL) && (pp->tokenization != ast->tokenization))) {
        result.issue = C_PARSE_ISSUE_MALFORMED;
        return result;
    }
    if (pp == NULL) {
        c_preprocessor_analysis_init(&local, ast->tokenization);
        pp = &local;
        owned = true;
    }
    c = (StmtCursor){.ast = ast, .pp = pp, .range = range,
                     .pos = range.first};
    before = ast->issue_count;
    result.statement = stmt_parse_one(&c);
    result.next_token = c.pos;
    if (ast->issue_count > before) {
        result.issue = ast->issues[before].kind;
        result.issue_token = ast->issues[before].token;
    }
    if (owned) {
        c_preprocessor_analysis_free(pp);
    }
    return result;
}

#if TESTING_meta_stmt
#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"

static int32
stmt_test_find(Tokenization *t, char *name, int32 occurrence) {
    TokenRange all = {0, t->token_count};
    int32 i = -1;
    for (int32 j = 0; j <= occurrence; j += 1) {
        i = token_range_next_text(t, all, i, name, strlen32(name));
    }
    ASSERT_LT_VAR(i, t->token_count);
    return i;
}

static void
stmt_test_control_flow(void) {
    char *src = "{ int x = 1; if (x) f(); else if (a) g(); "
                "while (x) ++x; do { x -= 1; } while (x); "
                "for (int i = 0; i < 3; i += 1) ; "
                "switch (x) { case 1: break; default: goto done; } "
                "done: return x; }";
    Tokenization t = tokenize(src, strlen32(src));
    CAst ast;
    CStmtParseResult r;
    int32 x;
    ASSERT(c_ast_init(&ast, &t));
    r =
        c_ast_parse_statement(&ast, (TokenRange){0, t.token_count}, NULL);
    ASSERT_EQ(r.issue, 0);
    ASSERT_EQ(ast.statements[r.statement].kind, C_STMT_COMPOUND);
    x = ast.statements[r.statement].first_child;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_DECLARATION);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_IF);
    ASSERT_GE(ast.statements[x].else_child, 0);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_WHILE);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_DO);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_FOR);
    ASSERT_GE(ast.statements[x].declaration, 0);
    ASSERT_GE(ast.statements[x].expression, 0);
    ASSERT_GE(ast.statements[x].for_step_expression, 0);
    ASSERT_EQ(ast.statements[ast.statements[x].first_child].kind, C_STMT_EMPTY);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_SWITCH);
    x = ast.statements[x].next;
    ASSERT_EQ(ast.statements[x].kind, C_STMT_LABEL);
    ASSERT_EQ(ast.statements[x].token, stmt_test_find(&t, "done", 1));
    ASSERT_EQ(ast.current_scope, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
}

static void
stmt_test_preprocessor(void) {
    char *src = "{ int n = 0;\n"
                "#if A\n"
                "n += 1;\n"
                "#else\n"
                "n += 2;\n"
                "#endif\n"
                "return n; }";
    Tokenization t = tokenize(src, strlen32(src));
    CPreprocessorAnalysis pp;
    CAst ast;
    CStmtParseResult r;
    int32 first;
    int32 group;
    int32 a;
    int32 b;
    ASSERT(c_preprocessor_analysis_init(&pp, &t));
    ASSERT(c_ast_init(&ast, &t));
    r =
        c_ast_parse_statement(&ast, (TokenRange){0, t.token_count}, &pp);
    ASSERT_EQ(r.issue, 0);
    first = ast.statements[r.statement].first_child;
    group = ast.statements[first].next;
    ASSERT_EQ(ast.statements[group].kind, C_STMT_PP_GROUP);
    a = ast.statements[group].first_child;
    b = ast.statements[a].next;
    ASSERT_EQ(ast.statements[a].kind, C_STMT_PP_BRANCH);
    ASSERT_EQ(ast.statements[b].kind, C_STMT_PP_BRANCH);
    ASSERT_EQ(ast.statements[ast.statements[a].first_child].kind,
              C_STMT_EXPRESSION);
    ASSERT_EQ(ast.statements[ast.statements[b].first_child].kind,
              C_STMT_EXPRESSION);
    ASSERT_EQ(ast.statements[ast.statements[group].next].kind, C_STMT_RETURN);
    c_ast_free(&ast);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&t);
}

static void
stmt_test_recovery(void) {
    char *src = "{ _Generic(x, int: 1); good(); return (int)2; }";
    Tokenization t = tokenize(src, strlen32(src));
    CAst ast;
    CStmtParseResult r;
    int32 a;
    int32 b;
    int32 d;
    ASSERT(c_ast_init(&ast, &t));
    r =
        c_ast_parse_statement(&ast, (TokenRange){0, t.token_count}, NULL);
    ASSERT_GT(ast.issue_count, 0);
    a = ast.statements[r.statement].first_child;
    b = ast.statements[a].next;
    ASSERT_EQ(ast.statements[b].kind, C_STMT_EXPRESSION);
    ASSERT_GE(ast.statements[b].expression, 0);
    d = ast.statements[b].next;
    ASSERT_EQ(ast.statements[d].kind, C_STMT_RETURN);
    c_ast_free(&ast);
    free_tokenization(&t);
}

static void
stmt_test_malformed_headers(void) {
    char *src = "{ if () f(); if (x) typedef int Y; "
                "broken( ; valid(); }";
    Tokenization t = tokenize(src, strlen32(src));
    CAst ast;
    CStmtParseResult r;
    int32 child;
    ASSERT(c_ast_init(&ast, &t));
    r =
        c_ast_parse_statement(&ast, (TokenRange){0, t.token_count}, NULL);
    ASSERT_GE(ast.issue_count, 3);
    child = ast.statements[r.statement].first_child;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_IF);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_IF);
    ASSERT_EQ(c_ast_lookup(&ast, stmt_test_find(&t, "Y", 0)), C_NAME_UNKNOWN);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_UNSUPPORTED);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_EXPRESSION);
    ASSERT_GE(ast.statements[child].expression, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
}

static void
stmt_test_unresolved_pointer_declarations(void) {
    char *src = "{ Arena *arena; union GenericArrayHeader **headers; "
                "int a = 2; int b = 3; a * b; "
                "for (Arena *item = arena; item; item = item->next) {} }";
    Tokenization t = tokenize(src, strlen32(src));
    CAst ast;
    CStmtParseResult r;
    int32 child;
    ASSERT(c_ast_init(&ast, &t));
    r =
        c_ast_parse_statement(&ast, (TokenRange){0, t.token_count}, NULL);
    child = ast.statements[r.statement].first_child;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_UNSUPPORTED);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_DECLARATION);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_DECLARATION);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_DECLARATION);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_EXPRESSION);
    ASSERT_EQ(ast.expressions[ast.statements[child].expression].kind,
              C_EXPR_BINARY);
    child = ast.statements[child].next;
    ASSERT_EQ(ast.statements[child].kind, C_STMT_FOR);
    ASSERT_LT(ast.statements[child].for_init_expression, 0);
    ASSERT_GE(ast.statements[child].expression, 0);
    ASSERT_GE(ast.issue_count, 2);
    ASSERT_EQ(c_ast_lookup(&ast, stmt_test_find(&t, "Arena", 0)),
              C_NAME_UNKNOWN);
    ASSERT_EQ(c_ast_lookup(&ast, stmt_test_find(&t, "GenericArrayHeader", 0)),
              C_NAME_UNKNOWN);
    ASSERT_EQ(ast.current_scope, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
}

int
main(void) {
    stmt_test_control_flow();
    stmt_test_preprocessor();
    stmt_test_recovery();
    stmt_test_malformed_headers();
    stmt_test_unresolved_pointer_declarations();
    return 0;
}
#endif /* TESTING_meta_stmt */

#endif /* META_STMT_C */
