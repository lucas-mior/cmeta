// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_SCOPE_C)
#define META_SCOPE_C

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"

bool
c_ast_init(CAst *ast, Tokenization *tokenization) {
    if ((ast == NULL) || (tokenization == NULL)
        || (ARRAY_LEN(tokenization->tokens) < 0)) {
        return false;
    }
    *ast = (CAst){0};
    ast->tokenization = tokenization;
    ast->current_scope = -1;
    c_ast_scope_push(ast);
    return true;
}

void
c_ast_free(CAst *ast) {
    if (ast == NULL) {
        return;
    }

    ARRAY_FREE(ast->declarators);
    ARRAY_FREE(ast->declarations);
    ARRAY_FREE(ast->enumerators);
    ARRAY_FREE(ast->records);
    ARRAY_FREE(ast->expressions);
    ARRAY_FREE(ast->arguments);
    ARRAY_FREE(ast->statements);
    ARRAY_FREE(ast->externals);
    ARRAY_FREE(ast->issues);
    ARRAY_FREE(ast->scopes);
    ARRAY_FREE(ast->bindings);

    *ast = (CAst){0};
    ast->current_scope = -1;
    return;
}

int32
c_ast_scope_push(CAst *ast) {
    int32 index;

    ASSERT(ast != NULL);
    index = ARRAY_LEN(ast->scopes);
    ARRAY_PUSH(ast->scopes, (CScope){.parent = ast->current_scope});
    ast->current_scope = index;
    return index;
}

bool
c_ast_scope_pop(CAst *ast) {
    if ((ast == NULL) || (ast->current_scope <= 0)) {
        return false;
    }
    ast->current_scope = ast->scopes[ast->current_scope].parent;
    return true;
}

static bool
ast_names_equal(CAst *ast, int32 a, int32 b) {
    Token *x = &ast->tokenization->tokens[a];
    Token *y = &ast->tokenization->tokens[b];
    return (x->len == y->len) && (memcmp64(x->text, y->text, x->len) == 0);
}

bool
c_ast_bind(CAst *ast, int32 name, enum CNameClass name_class) {
    if ((ast == NULL) || (ast->tokenization == NULL)
        || (ast->current_scope < 0) || (name < 0)
        || (name >= ARRAY_LEN(ast->tokenization->tokens))
        || (ast->tokenization->tokens[name].kind != TOKEN_IDENT)
        || (name_class <= C_NAME_UNKNOWN)
        || (name_class > C_NAME_INFERRED_TYPEDEF)) {
        return false;
    }
    ARRAY_PUSH(ast->bindings, (CBinding){ast->current_scope, name,
                                        name_class});
    return true;
}

static enum CNameClass
c_ast_lookup_internal(CAst *ast, int32 name, bool tag) {
    if ((ast == NULL) || (ast->tokenization == NULL) || (name < 0)
        || (name >= ARRAY_LEN(ast->tokenization->tokens))) {
        return C_NAME_UNKNOWN;
    }
    if (ast->tokenization->tokens[name].kind != TOKEN_IDENT) {
        return C_NAME_UNKNOWN;
    }
    for (int32 scope = ast->current_scope; scope >= 0;
         scope = ast->scopes[scope].parent) {
        for (int32 i = ARRAY_LEN(ast->bindings) - 1; i >= 0; i -= 1) {
            CBinding *binding = &ast->bindings[i];
            bool is_tag = binding->name_class >= C_NAME_TAG_STRUCT
                          && binding->name_class <= C_NAME_TAG_ENUM;
            if ((binding->scope == scope) && (is_tag == tag)
                && ast_names_equal(ast, name, binding->name_token)) {
                return binding->name_class;
            }
        }
    }
    return C_NAME_UNKNOWN;
}

enum CNameClass
c_ast_lookup(CAst *ast, int32 name) {
    return c_ast_lookup_internal(ast, name, false);
}

enum CNameClass
c_ast_lookup_tag(CAst *ast, int32 name) {
    return c_ast_lookup_internal(ast, name, true);
}

#endif /* META_SCOPE_C */
