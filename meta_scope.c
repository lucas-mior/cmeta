// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_SCOPE_C)
#define META_SCOPE_C

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"

static void *
c_ast_storage_grow(void *old, int32 *capacity, int32 count, int32 unit) {
    int32 next;

    if (count < *capacity) {
        return old;
    }
    ASSERT_LT(*capacity, INT32_MAX/2);
    next = 16;
    if (*capacity != 0) {
        next = *capacity*2;
    }
    if (old == NULL) {
        old = malloc2((int64)next*unit);
    } else {
        old = realloc2(old, *capacity, next, unit);
    }
    *capacity = next;
    return old;
}

bool
c_ast_init(CAst *ast, Tokenization *tokenization) {
    if ((ast == NULL) || (tokenization == NULL)
        || (tokenization->token_count < 0)) {
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
    free2(ast->declarators,
          (int64)ast->declarator_capacity*SIZEOF(CDeclaratorNode));
    free2(ast->declarations,
          (int64)ast->declaration_capacity*SIZEOF(CDeclaration));
    free2(ast->enumerators,
          (int64)ast->enumerator_capacity*SIZEOF(CEnumerator));
    free2(ast->records, (int64)ast->record_capacity*SIZEOF(CRecord));
    free2(ast->expressions,
          (int64)ast->expression_capacity*SIZEOF(CExpression));
    free2(ast->arguments,
          (int64)ast->argument_capacity*SIZEOF(CExprArgument));
    free2(ast->statements,
          (int64)ast->statement_capacity*SIZEOF(CStatement));
    free2(ast->externals,
          (int64)ast->external_capacity*SIZEOF(CExternal));
    free2(ast->issues,
          (int64)ast->issue_capacity*SIZEOF(CParseIssue));
    free2(ast->scopes, (int64)ast->scope_capacity*SIZEOF(CScope));
    free2(ast->bindings, (int64)ast->binding_capacity*SIZEOF(CBinding));
    *ast = (CAst){0};
    ast->current_scope = -1;
    return;
}

int32
c_ast_scope_push(CAst *ast) {
    int32 index;

    ASSERT(ast != NULL);
    index = ast->scope_count;
    ast->scopes = c_ast_storage_grow(ast->scopes, &ast->scope_capacity,
                                     index, SIZEOF(CScope));
    ast->scopes[index] = (CScope){.parent = ast->current_scope};
    ast->scope_count += 1;
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
    int32 count;

    if ((ast == NULL) || (ast->tokenization == NULL)
        || (ast->current_scope < 0) || (name < 0)
        || (name >= ast->tokenization->token_count)
        || (ast->tokenization->tokens[name].kind != TOKEN_IDENT)
        || (name_class <= C_NAME_UNKNOWN)
        || (name_class > C_NAME_TAG_ENUM)) {
        return false;
    }
    count = ast->binding_count;
    ast->bindings =
        c_ast_storage_grow(ast->bindings, &ast->binding_capacity,
                           count, SIZEOF(CBinding));
    ast->bindings[count] = (CBinding){ast->current_scope, name, name_class};
    ast->binding_count += 1;
    return true;
}

static enum CNameClass
c_ast_lookup_internal(CAst *ast, int32 name, bool tag) {
    if ((ast == NULL) || (ast->tokenization == NULL) || (name < 0)
        || (name >= ast->tokenization->token_count)) {
        return C_NAME_UNKNOWN;
    }
    if (ast->tokenization->tokens[name].kind != TOKEN_IDENT) {
        return C_NAME_UNKNOWN;
    }
    for (int32 scope = ast->current_scope; scope >= 0;
         scope = ast->scopes[scope].parent) {
        for (int32 i = ast->binding_count - 1; i >= 0; i -= 1) {
            CBinding *binding = &ast->bindings[i];
            bool is_tag = binding->name_class >= C_NAME_TAG_STRUCT;
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
