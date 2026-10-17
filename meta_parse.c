// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_PARSE_C)
#define META_PARSE_C

#if !defined(TESTING_meta_parse)
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_meta_parse 1
#else
#define TESTING_meta_parse 0
#endif
#endif

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"
#include "meta_stmt.c"

static void *c_ast_storage_grow(void *, int32 *, int32, int32);

typedef struct UnitCursor {
    CAst *ast;
    CPreprocessorAnalysis *pp;
    TokenRange range;
    int32 pos;
    int32 depth;
} UnitCursor;

static int32
unit_sig(UnitCursor *c, int32 start) {
    return token_range_next_significant(c->ast->tokenization, c->range,
                                        start - 1);
}

static int32
unit_new(UnitCursor *c, enum CExternalKind kind, TokenRange tokens) {
    CAst *ast = c->ast;
    int32 i = ast->external_count;
    ast->externals = c_ast_storage_grow(ast->externals,
                                        &ast->external_capacity,
                                        i, SIZEOF(CExternal));
    ast->externals[i] = (CExternal){
        .tokens = tokens, .kind = kind, .declaration = -1,
        .function_body = -1, .first_child = -1, .next = -1,
        .directive = -1,
    };
    ast->external_count += 1;
    return i;
}

static int32
unit_boundary(UnitCursor *c, int32 first) {
    for (int32 i = first; i < c->range.end; i += 1) {
        int32 d = c->pp->token_directives[i];
        if ((d >= 0) && (c->pp->directives[d].hash_token == i)) {
            return i;
        }
    }
    return c->range.end;
}

static void
unit_append(UnitCursor *c, int32 parent, int32 *head,
            int32 *tail, int32 node) {
    if (*tail >= 0) {
        c->ast->externals[*tail].next = node;
    } else if (parent >= 0) {
        c->ast->externals[parent].first_child = node;
    } else {
        *head = node;
    }
    *tail = node;
}

static int32
unit_name(CAst *ast, int32 root) {
    while (root >= 0) {
        CDeclaratorNode *d = &ast->declarators[root];
        if (d->kind == C_DECL_NAME) {
            return d->name_token;
        }
        root = d->inner;
    }
    return -1;
}

static int32
unit_function_declarator(CAst *ast, int32 root) {
    while (root >= 0) {
        CDeclaratorNode *d = &ast->declarators[root];
        if (d->kind == C_DECL_FUNCTION) {
            return root;
        }
        root = d->inner;
    }
    return -1;
}

static int32
unit_function_body(UnitCursor *c, int32 declaration) {
    CAst *ast = c->ast;
    CDeclaration *decl = &ast->declarations[declaration];
    int32 function = unit_function_declarator(ast, decl->first_declarator);
    int32 param;
    int32 open = decl->body.first;
    int32 close = decl->body.end - 1;
    StmtCursor stmts = {.ast = ast, .pp = c->pp,
                        .range = {open + 1, close},
                        .pos = open + 1};
    int32 node = stmt_new(&stmts, C_STMT_COMPOUND, open);
    ast->statements[node].tokens = decl->body;
    c_ast_scope_push(ast);
    /* Parameter names are visible in the function body, not outside it. */
    if (function >= 0) {
        param = ast->declarators[function].first_parameter;
        while (param >= 0) {
            int32 root = ast->declarations[param].first_declarator;
            int32 name = unit_name(ast, root);
            if (name >= 0) {
                c_ast_bind(ast, name, C_NAME_ORDINARY);
            }
            param = ast->declarations[param].next;
        }
    }
    stmt_parse_sequence(&stmts, node);
    c_ast_scope_pop(ast);
    return node;
}

static int32
unit_recover(UnitCursor *c, int32 first) {
    TokenRange limited = {first, unit_boundary(c, first)};
    TokenDelimiterDepth depth = {0};
    int32 semi = token_range_next_text_at_depth(c->ast->tokenization,
                                                limited, first - 1,
                                                depth, ";", 1);
    if (semi < limited.end) {
        return unit_sig(c, semi + 1);
    }
    for (int32 i = unit_sig(c, first); i < limited.end;
         i = unit_sig(c, i + 1)) {
        if (TOKEN_IS(&c->ast->tokenization->tokens[i], ";")) {
            return unit_sig(c, i + 1);
        }
    }
    return limited.end;
}

static int32
unit_unsupported(UnitCursor *c, int32 first,
                 enum CParseIssueKind kind, int32 at) {
    int32 end = unit_recover(c, first);
    int32 node;
    StmtCursor issue = {.ast = c->ast};
    if (end <= first) {
        end = unit_sig(c, first + 1);
    }
    node = unit_new(c, C_EXTERNAL_UNSUPPORTED, (TokenRange){first, end});
    stmt_issue(&issue, kind, at, (TokenRange){first, end});
    c->pos = end;
    return node;
}

static void unit_parse_region(UnitCursor *, int32, int32 *, int32 *);

static int32
unit_preprocessor(UnitCursor *c, int32 first, int32 directive) {
    int32 group = -1;
    int32 node;
    CPreprocessorDirective *d = &c->pp->directives[directive];
    for (int32 i = 0; i < c->pp->group_count; i += 1) {
        int32 branch = c->pp->groups[i].first_branch;
        if ((branch >= 0)
            && (c->pp->branches[branch].directive_index == directive)) {
            group = i;
            break;
        }
    }
    if (group < 0) {
        node = unit_new(c, C_EXTERNAL_PP_DIRECTIVE,
                        (TokenRange){first, d->tokens.end});
        c->ast->externals[node].directive = directive;
        c->pos = unit_sig(c, d->tokens.end);
        return node;
    }
    CPreprocessorGroup *g = &c->pp->groups[group];
    if ((g->endif_directive < 0)
        || (c->pp->directives[g->endif_directive].tokens.end > c->range.end)) {
        return unit_unsupported(c, first, C_PARSE_ISSUE_PREPROCESSOR, first);
    }
    node = unit_new(c, C_EXTERNAL_PP_GROUP,
                    (TokenRange){first,
                        c->pp->directives[g->endif_directive].tokens.end});
    c->ast->externals[node].directive = group;
    int32 head = -1;
    int32 tail = -1;
    for (int32 b = g->first_branch; b >= 0;
         b = c->pp->branches[b].next_branch) {
        CPreprocessorBranch *branch = &c->pp->branches[b];
        UnitCursor inner = *c;
        int32 part = unit_new(c, C_EXTERNAL_PP_BRANCH, branch->body_tokens);
        c->ast->externals[part].directive = branch->directive_index;
        unit_append(c, node, &head, &tail, part);
        inner.range = branch->body_tokens;
        inner.pos = inner.range.first;
        inner.depth += 1;
        c_ast_scope_push(c->ast);
        int32 child_head = -1;
        int32 child_tail = -1;
        unit_parse_region(&inner, part, &child_head, &child_tail);
        c_ast_scope_pop(c->ast);
    }
    c->pos = unit_sig(c, c->ast->externals[node].tokens.end);
    return node;
}

static int32
unit_parse_one(UnitCursor *c) {
    int32 first = unit_sig(c, c->pos);
    int32 directive;
    int32 end;
    CDeclParseResult result;
    int32 node;
    if (first >= c->range.end) {
        return -1;
    }
    if (c->depth >= 128) {
        return unit_unsupported(c, first, C_PARSE_ISSUE_UNSUPPORTED, first);
    }
    directive = c->pp->token_directives[first];
    if ((directive >= 0)
        && (c->pp->directives[directive].hash_token == first)) {
        return unit_preprocessor(c, first, directive);
    }
    end = unit_boundary(c, first);
    result = c_ast_parse_declaration(c->ast, (TokenRange){first, end});
    if (result.status != C_DECL_PARSE_OK) {
        enum CParseIssueKind issue = C_PARSE_ISSUE_MALFORMED;
        if (result.status == C_DECL_PARSE_UNSUPPORTED
            || result.status == C_DECL_PARSE_NOT_DECLARATION) {
            issue = C_PARSE_ISSUE_UNSUPPORTED;
        }
        return unit_unsupported(c, first, issue, result.issue_token);
    }
    node = unit_new(c, C_EXTERNAL_DECLARATION,
                    c->ast->declarations[result.declaration].tokens);
    c->ast->externals[node].declaration = result.declaration;
    if (c->ast->declarations[result.declaration].is_function_definition) {
        c->ast->externals[node].function_body =
            unit_function_body(c, result.declaration);
    }
    c->pos = result.next_token;
    return node;
}

static void
unit_parse_region(UnitCursor *c, int32 parent,
                  int32 *head, int32 *tail) {
    int32 local_head = -1;
    int32 local_tail = -1;
    while (unit_sig(c, c->pos) < c->range.end) {
        int32 first = unit_sig(c, c->pos);
        int32 node;
        c->pos = first;
        node = unit_parse_one(c);
        if (node >= 0) {
            unit_append(c, parent, &local_head, &local_tail, node);
        }
        if (c->pos <= first) {
            StmtCursor issue = {.ast = c->ast};
            stmt_issue(&issue, C_PARSE_ISSUE_MALFORMED, first,
                       (TokenRange){first, first + 1});
            c->pos = unit_sig(c, first + 1);
        }
    }
    if (parent < 0) {
        *head = local_head;
    }
    *tail = local_tail;
}

CUnitParseResult
c_ast_parse_translation_unit(CAst *ast, TokenRange range,
                             CPreprocessorAnalysis *pp) {
    CUnitParseResult result = {-1, 0, 0};
    CPreprocessorAnalysis local;
    bool owned = false;
    int32 tail = -1;
    int32 before;
    int32 start;
    if ((ast == NULL) || (ast->tokenization == NULL)
        || !token_range_is_valid(ast->tokenization, range)
        || ((pp != NULL) && (pp->tokenization != ast->tokenization))) {
        return result;
    }
    if (pp == NULL) {
        c_preprocessor_analysis_init(&local, ast->tokenization);
        pp = &local;
        owned = true;
    }
    before = ast->issue_count;
    start = ast->external_count;
    UnitCursor c = {.ast = ast, .pp = pp, .range = range,
                    .pos = range.first};
    for (int32 i = 0; i < pp->issue_count; i += 1) {
        StmtCursor issue = {.ast = ast};
        int32 d = pp->issues[i].directive_index;
        int32 token = pp->directives[d].hash_token;
        stmt_issue(&issue, C_PARSE_ISSUE_PREPROCESSOR, token,
                   pp->directives[d].tokens);
    }
    unit_parse_region(&c, -1, &result.first_external, &tail);
    result.external_count = ast->external_count - start;
    result.issue_count = ast->issue_count - before;
    if (owned) {
        c_preprocessor_analysis_free(pp);
    }
    return result;
}

#if TESTING_meta_parse
#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"

static int32
unit_test_find(Tokenization *t, char *word, int32 occurrence) {
    TokenRange all = {0, t->token_count};
    int32 i = -1;
    for (int32 j = 0; j <= occurrence; j += 1) {
        i = token_range_next_text(t, all, i, word, strlen32(word));
    }
    ASSERT_LT_VAR(i, t->token_count);
    return i;
}

static void
unit_test_functions(void) {
    char *source = "typedef int T; int data; "
                   "int f(int T, int q) { "
                   "if (q) { return (T)+q; } return T; } "
                   "int g(void) { T x = 4; return x; }";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    ASSERT(c_ast_init(&ast, &t));
    CUnitParseResult r =
        c_ast_parse_translation_unit(&ast,
                                     (TokenRange){0, t.token_count}, NULL);
    ASSERT_EQ(r.issue_count, 0);
    ASSERT_EQ(r.external_count, 4);
    int32 f = ast.externals[r.first_external].next;
    f = ast.externals[f].next;
    ASSERT_GE(ast.externals[f].function_body, 0);
    int32 body = ast.externals[f].function_body;
    int32 stmt = ast.statements[body].first_child;
    ASSERT_EQ(ast.statements[stmt].kind, C_STMT_IF);
    int32 compound = ast.statements[stmt].first_child;
    int32 ret = ast.statements[compound].first_child;
    int32 expr = ast.statements[ret].expression;
    ASSERT_EQ(ast.expressions[expr].kind, C_EXPR_BINARY);
    ASSERT_EQ(ast.expressions[ast.expressions[expr].left].kind, C_EXPR_PAREN);
    ASSERT_EQ(c_ast_lookup(&ast, unit_test_find(&t, "T", 4)), C_NAME_TYPEDEF);
    int32 g = ast.externals[f].next;
    ASSERT_GE(ast.externals[g].function_body, 0);
    ASSERT_EQ(ast.current_scope, 0);
    c_ast_free(&ast);
    free_tokenization(&t);
}

static void
unit_test_alternatives(void) {
    char *source = "#define X 1\n"
                   "#if A\n"
                   "typedef int Choice;\n"
                   "int a(void) { return 1; }\n"
                   "#else\n"
                   "int b(void) { return 2; }\n"
                   "#endif\n"
                   "int c(void) { return X; }\n";
    Tokenization t = tokenize(source, strlen32(source));
    CPreprocessorAnalysis pp;
    CAst ast;
    ASSERT(c_preprocessor_analysis_init(&pp, &t));
    ASSERT(c_ast_init(&ast, &t));
    CUnitParseResult r =
        c_ast_parse_translation_unit(&ast, (TokenRange){0, t.token_count}, &pp);
    ASSERT_EQ(r.issue_count, 0);
    int32 first = r.first_external;
    ASSERT_EQ(ast.externals[first].kind, C_EXTERNAL_PP_DIRECTIVE);
    int32 group = ast.externals[first].next;
    ASSERT_EQ(ast.externals[group].kind, C_EXTERNAL_PP_GROUP);
    int32 branch = ast.externals[group].first_child;
    ASSERT_EQ(ast.externals[branch].kind, C_EXTERNAL_PP_BRANCH);
    int32 next = ast.externals[branch].next;
    ASSERT_EQ(ast.externals[next].kind, C_EXTERNAL_PP_BRANCH);
    ASSERT_EQ(ast.externals[ast.externals[group].next].kind,
              C_EXTERNAL_DECLARATION);
    ASSERT_EQ(c_ast_lookup(&ast, unit_test_find(&t, "Choice", 0)),
              C_NAME_UNKNOWN);
    c_ast_free(&ast);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&t);
}

static void
unit_test_malformed_and_recovery(void) {
    char *source = "__attribute__((unused)) int bad; "
                   "int good(void) { return 1; } "
                   "int fine;";
    Tokenization t = tokenize(source, strlen32(source));
    CAst ast;
    ASSERT(c_ast_init(&ast, &t));
    CUnitParseResult r =
        c_ast_parse_translation_unit(&ast,
                                     (TokenRange){0, t.token_count}, NULL);
    ASSERT_GT(r.issue_count, 0);
    ASSERT_EQ(ast.externals[r.first_external].kind, C_EXTERNAL_UNSUPPORTED);
    int32 second = ast.externals[r.first_external].next;
    ASSERT_EQ(ast.externals[second].kind, C_EXTERNAL_DECLARATION);
    ASSERT_GE(ast.externals[second].function_body, 0);
    ASSERT_EQ(ast.externals[ast.externals[second].next].kind,
              C_EXTERNAL_DECLARATION);
    c_ast_free(&ast);
    free_tokenization(&t);
}

int
main(void) {
    unit_test_functions();
    unit_test_alternatives();
    unit_test_malformed_and_recovery();
    return 0;
}
#endif /* TESTING_meta_parse */

#endif /* META_PARSE_C */
