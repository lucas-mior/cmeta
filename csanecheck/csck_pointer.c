// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_POINTER_C)
#define CSANECHECK_POINTER_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

typedef struct CsanePointerSymbol {
    int32 name;
    int32 function;
    int32 scope;
    int32 region;
    int32 position;
    bool pointer;
} CsanePointerSymbol;

typedef struct CsanePointerTypes {
    int32 name;
    bool pointer;
} CsanePointerTypes;

typedef struct CsanePointerState {
    CsanePointerSymbol *symbols;
    CsanePointerTypes *aliases;
} CsanePointerState;

static bool
csane_pointer_word(Token *token, char *word, int32 length) {
    return TOKEN_IS(token, word, length);
}

static bool
csane_pointer_same(Token *a, Token *b) {
    return a->kind == TOKEN_IDENT && b->kind == TOKEN_IDENT
           && a->len == b->len && !memcmp64(a->text, b->text, a->len);
}

static bool
csane_pointer_qualifier(Token *token) {
    return csane_pointer_word(token, STRLIT("const"))
           || csane_pointer_word(token, STRLIT("volatile"))
           || csane_pointer_word(token, STRLIT("restrict"))
           || csane_pointer_word(token, STRLIT("static"))
           || csane_pointer_word(token, STRLIT("extern"))
           || csane_pointer_word(token, STRLIT("register"))
           || csane_pointer_word(token, STRLIT("inline"))
           || csane_pointer_word(token, STRLIT("_Atomic"))
           || csane_pointer_word(token, STRLIT("_Thread_local"));
}

/* Recognize declared types, not variable-name guesses. Unknown included
 * typedefs with no explicit '*' remain unclassified. */
static bool
csane_pointer_type(CsaneFile *file, CsanePointerState *state,
                   CsaneTokenView *view, int32 *position, bool *alias_pointer) {
    Tokenization *tokens = file->tokens;
    int32 i = *position;
    bool known = false;
    bool aggregate = false;

    *alias_pointer = false;
    while (i < view->range.end
           && csane_pointer_qualifier(&tokens->tokens[i])) {
        i = csane_tokens_next(view, i);
    }
    if (i >= view->range.end) {
        return false;
    }
    if (TOKEN_IS(&tokens->tokens[i], "struct")
        || TOKEN_IS(&tokens->tokens[i], "union")
        || TOKEN_IS(&tokens->tokens[i], "enum")) {
        aggregate = true;
        i = csane_tokens_next(view, i);
        if (i >= view->range.end
            || tokens->tokens[i].kind != TOKEN_IDENT) {
            return false;
        }
        i = csane_tokens_next(view, i);
        known = true;
    } else if (csane_name_kind(&tokens->tokens[i]) == CSANE_NAME_PRIMITIVE) {
        known = true;
        i = csane_tokens_next(view, i);
        while (i < view->range.end
               && csane_name_kind(&tokens->tokens[i])
                  == CSANE_NAME_PRIMITIVE) {
            i = csane_tokens_next(view, i);
        }
    } else if (csane_type_hints_typedef(file->type_hints,
                                        &tokens->tokens[i])
               || csane_name_kind(&tokens->tokens[i]) == CSANE_NAME_TYPE) {
        known = true;
        for (int32 a = ARRAY_LEN(state->aliases) - 1; a >= 0; a -= 1) {
            CsanePointerTypes *alias = &state->aliases[a];
            if (csane_pointer_same(&tokens->tokens[i],
                                   &file->tokens->tokens[alias->name])) {
                *alias_pointer = alias->pointer;
                break;
            }
        }
        i = csane_tokens_next(view, i);
    }
    if (!known || (aggregate && i < view->range.end
                   && TOKEN_IS(&tokens->tokens[i], "{"))) {
        return false;
    }
    while (i < view->range.end
           && csane_pointer_qualifier(&tokens->tokens[i])) {
        i = csane_tokens_next(view, i);
    }
    *position = i;
    return true;
}

/* A declarator is either '*name', 'name', or '(*name)(...)'. Avoid
 * expressions with a second identifier or an unexpected operator. */
static int32
csane_pointer_declarator(CsaneFile *file, CsaneTokenView *view,
                         TokenRange segment, bool alias_pointer,
                         bool *pointer) {
    Tokenization *tokens = file->tokens;
    int32 end = csane_tokens_top_level(view, segment.first, STRLIT("="));
    int32 i = segment.first;
    int32 name = -1;
    bool star = false;
    bool paren = false;

    if (end < 0) {
        return -1;
    }
    if (end > segment.end) {
        end = segment.end;
    }
    if (i < end && TOKEN_IS(&tokens->tokens[i], "(")) {
        paren = true;
        i = csane_tokens_next(view, i);
    }
    while (i < end && (TOKEN_IS(&tokens->tokens[i], "*")
                       || csane_pointer_qualifier(&tokens->tokens[i]))) {
        if (TOKEN_IS(&tokens->tokens[i], "*")) {
            star = true;
        }
        i = csane_tokens_next(view, i);
    }
    if (i >= end || tokens->tokens[i].kind != TOKEN_IDENT) {
        return -1;
    }
    name = i;
    i = csane_tokens_next(view, i);
    if (paren) {
        if (i >= end || !TOKEN_IS(&tokens->tokens[i], ")")) {
            return -1;
        }
        i = csane_tokens_next(view, i);
    }
    *pointer = star || alias_pointer;
    if (!paren && i < end && TOKEN_IS(&tokens->tokens[i], "[")) {
        *pointer = false;
    }
    while (i < end && (TOKEN_IS(&tokens->tokens[i], "[")
                       || TOKEN_IS(&tokens->tokens[i], "("))) {
        int32 close = csane_tokens_match(view, i);
        if (close < 0 || close >= end) {
            return -1;
        }
        i = csane_tokens_next(view, close);
    }
    return i == end ? name : -1;
}

static void
csane_pointer_declarations(CsaneFile *file, CsanePointerState *state,
                           TokenRange range, int32 function, int32 scope,
                           int32 region) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    int32 first;
    int32 end;
    int32 part;
    bool alias_pointer;
    bool typedef_decl = false;

    if (!csane_tokens_view(&view, tokens, range)) {
        return;
    }
    first = csane_tokens_next(&view, range.first - 1);
    end = token_range_last_significant(tokens, range);
    if (end < first) {
        return;
    }
    if (TOKEN_IS(&tokens->tokens[end], ";")) {
        view.range.end = end;
    }
    if (TOKEN_IS(&tokens->tokens[first], "typedef")) {
        typedef_decl = true;
        first = csane_tokens_next(&view, first);
    }
    if (!csane_pointer_type(file, state, &view, &first, &alias_pointer)) {
        return;
    }
    part = first;
    while (part < view.range.end) {
        int32 comma = csane_tokens_top_level(&view, part, STRLIT(","));
        TokenRange segment;
        bool pointer;
        int32 name;

        if (comma < 0) {
            return;
        }
        segment = (TokenRange){part, comma};
        name = csane_pointer_declarator(file, &view, segment,
                                        alias_pointer, &pointer);
        if (name >= 0) {
            if (typedef_decl) {
                ARRAY_PUSH(state->aliases,
                           (CsanePointerTypes){name, pointer});
            } else {
                ARRAY_PUSH(state->symbols, (CsanePointerSymbol){
                    .name = name,
                    .function = function,
                    .scope = scope,
                    .region = region,
                    .position = tokens->tokens[name].offset,
                    .pointer = pointer,
                });
            }
        }
        if (comma >= view.range.end) {
            break;
        }
        part = csane_tokens_next(&view, comma);
    }
    return;
}

static void
csane_pointer_parameters(CsaneFile *file, CsanePointerState *state,
                         int32 function) {
    CsaneTopItem *top = &file->toplevel->items[function];
    CsaneTokenView view;
    int32 open;
    int32 close;
    int32 part;

    if (top->kind != CSANE_TOP_FUNCTION_DEFINITION
        || top->name_token < 0
        || !csane_tokens_view(&view, file->tokens, top->header)) {
        return;
    }
    open = csane_tokens_next(&view, top->name_token);
    if (open >= view.range.end
        || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0) {
        return;
    }
    view.range = (TokenRange){open + 1, close};
    part = csane_tokens_next(&view, open);
    while (part < close) {
        int32 comma = csane_tokens_top_level(&view, part, STRLIT(","));
        if (comma < 0) {
            return;
        }
        csane_pointer_declarations(file, state,
                                   (TokenRange){part, comma}, function,
                                   -1, top->owner_region);
        if (comma >= close) {
            break;
        }
        part = csane_tokens_next(&view, comma);
    }
}

/* Only ancestor block declarations belong to the lexical scope of the if.
 * Preserve non-pointer declarations so that they shadow outer pointers. */
static bool
csane_pointer_scope(CsaneFile *file, CsanePointerSymbol *symbol,
                    CsaneStatement *stmt) {
    int32 scope = stmt->parent;
    if (symbol->scope == -1) {
        return true;
    }
    while (scope >= 0) {
        if (scope == symbol->scope) {
            return true;
        }
        scope = file->statements->items[scope].parent;
    }
    return false;
}

static bool
csane_pointer_region(CsaneFile *file, int32 ancestor, int32 region) {
    if (ancestor < 0) {
        return true;
    }
    for (int32 i = region; i >= 0; i = file->regions->items[i].parent) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

static bool
csane_pointer_known(CsaneFile *file, CsanePointerState *state,
                    CsaneStatement *stmt, int32 name) {
    Token *wanted = &file->tokens->tokens[name];
    for (int32 i = ARRAY_LEN(state->symbols) - 1; i >= 0; i -= 1) {
        CsanePointerSymbol *symbol = &state->symbols[i];
        if ((symbol->function != stmt->function && symbol->function >= 0)
            || symbol->position >= stmt->source.start
            || !csane_pointer_region(file, symbol->region,
                                     stmt->owner_region)
            || !csane_pointer_same(wanted,
                                   &file->tokens->tokens[symbol->name])) {
            continue;
        }
        if (symbol->function < 0
            || csane_pointer_scope(file, symbol, stmt)) {
            return symbol->pointer;
        }
    }
    return false;
}

static int32
csane_pointer_ident(CsaneFile *file, CsaneTokenView *view, TokenRange range) {
    Tokenization *tokens = file->tokens;
    int32 first;
    int32 after;
    range = csane_tokens_unwrap(view, range);
    first = csane_tokens_next(view, range.first - 1);
    if (first >= range.end || tokens->tokens[first].kind != TOKEN_IDENT) {
        return -1;
    }
    after = csane_tokens_next(view, first);
    return after >= range.end ? first : -1;
}

/* A simple lvalue may be an identifier or a chain of member accesses.
 * It cannot be a call or an expression with potential side effects. */
static bool
csane_pointer_lvalue(CsaneFile *file, CsaneTokenView *view, TokenRange range) {
    Tokenization *tokens = file->tokens;
    int32 i;

    range = csane_tokens_unwrap(view, range);
    i = csane_tokens_next(view, range.first - 1);
    if (i >= range.end || tokens->tokens[i].kind != TOKEN_IDENT
        || TOKEN_IS(&tokens->tokens[i], "NULL")) {
        return false;
    }
    i = csane_tokens_next(view, i);
    while (i < range.end) {
        int32 member;
        if (!TOKEN_IS(&tokens->tokens[i], ".")
            && !TOKEN_IS(&tokens->tokens[i], "->")) {
            return false;
        }
        member = csane_tokens_next(view, i);
        if (member >= range.end
            || tokens->tokens[member].kind != TOKEN_IDENT) {
            return false;
        }
        i = csane_tokens_next(view, member);
    }
    return true;
}

static bool
csane_pointer_null(CsaneFile *file, CsaneTokenView *view, TokenRange range) {
    Tokenization *tokens = file->tokens;
    int32 first;

    range = csane_tokens_unwrap(view, range);
    first = csane_tokens_next(view, range.first - 1);
    return first < range.end && TOKEN_IS(&tokens->tokens[first], "NULL")
           && csane_tokens_next(view, first) >= range.end;
}

/* Recognize exactly `p`, `object->p`, or those expressions compared with
 * NULL. This is intentionally narrower than general boolean expressions. */
static bool
csane_pointer_positive(CsaneFile *file, CsaneTokenView *view,
                       TokenRange range, TokenRange *guard) {
    int32 comparison;
    TokenRange left;
    TokenRange right;
    CsaneTokenView content;

    range = csane_tokens_unwrap(view, range);
    if (!csane_tokens_subview(&content, view, range)) {
        return false;
    }
    view = &content;
    if (csane_pointer_lvalue(file, view, range)) {
        *guard = range;
        return true;
    }
    comparison = csane_tokens_top_level(view, range.first, STRLIT("!="));
    if (comparison < range.first || comparison >= range.end) {
        return false;
    }
    left = (TokenRange){range.first, comparison};
    right = (TokenRange){comparison + 1, range.end};
    if (csane_pointer_lvalue(file, view, left)
        && csane_pointer_null(file, view, right)) {
        *guard = csane_tokens_unwrap(view, left);
        return true;
    }
    if (csane_pointer_null(file, view, left)
        && csane_pointer_lvalue(file, view, right)) {
        *guard = csane_tokens_unwrap(view, right);
        return true;
    }
    return false;
}

static bool
csane_pointer_same_range(CsaneFile *file, TokenRange a, TokenRange b) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view_a;
    CsaneTokenView view_b;
    int32 i;
    int32 j;

    if (!csane_tokens_view(&view_a, tokens, a)
        || !csane_tokens_view(&view_b, tokens, b)) {
        return false;
    }
    a = csane_tokens_unwrap(&view_a, a);
    b = csane_tokens_unwrap(&view_b, b);
    i = csane_tokens_next(&view_a, a.first - 1);
    j = csane_tokens_next(&view_b, b.first - 1);
    while (i < a.end && j < b.end) {
        if (tokens->tokens[i].len != tokens->tokens[j].len
            || memcmp64(tokens->tokens[i].text,
                        tokens->tokens[j].text, tokens->tokens[i].len)) {
            return false;
        }
        i = csane_tokens_next(&view_a, i);
        j = csane_tokens_next(&view_b, j);
    }
    return i >= a.end && j >= b.end;
}

/* Compare one immediate operand against NULL; a parenthesized identifier is
 * still an operand, but a member expression has no established field type. */
static int32
csane_pointer_ne_name(CsaneFile *file, CsaneTokenView *view, int32 op) {
    Tokenization *tokens = file->tokens;
    int32 left = csane_tokens_previous(view, op);
    int32 right = csane_tokens_next(view, op);
    TokenRange lhs;
    TokenRange rhs;
    int32 name;

    if (left < view->range.first || right >= view->range.end) {
        return -1;
    }
    lhs = (TokenRange){left, op};
    rhs = (TokenRange){right, right + 1};
    if (TOKEN_IS(&tokens->tokens[left], ")")) {
        int32 open = csane_tokens_match(view, left);
        if (open < view->range.first) {
            return -1;
        }
        lhs.first = open;
    }
    if (TOKEN_IS(&tokens->tokens[right], "(")) {
        int32 close = csane_tokens_match(view, right);
        if (close < 0) {
            return -1;
        }
        rhs.end = close + 1;
    }
    if (csane_pointer_null(file, view, lhs)) {
        name = csane_pointer_ident(file, view, rhs);
    } else if (csane_pointer_null(file, view, rhs)) {
        name = csane_pointer_ident(file, view, lhs);
    } else {
        name = -1;
    }
    return name;
}

static void
csane_pointer_condition(CsaneFile *file, CsanePointerState *state,
                        CsaneStatement *stmt, CsaneTokenView *view,
                        TokenRange range) {
    Tokenization *tokens = file->tokens;
    int32 first = csane_tokens_next(view, range.first - 1);

    for (int32 i = first; i < range.end; i = csane_tokens_next(view, i)) {
        Token *token = &tokens->tokens[i];
        if (TOKEN_IS(token, "(")) {
            int32 prev = csane_tokens_previous(view, i);
            int32 close = csane_tokens_match(view, i);
            bool call = prev >= range.first
                        && (tokens->tokens[prev].kind == TOKEN_IDENT
                            || TOKEN_IS(&tokens->tokens[prev], "]"));
            if (close < 0 || close >= range.end) {
                return;
            }
            if (!call) {
                csane_pointer_condition(file, state, stmt, view,
                                        (TokenRange){i + 1, close});
            }
            i = close;
        } else if (TOKEN_IS(token, "!")) {
            int32 operand = csane_tokens_next(view, i);
            int32 end = csane_tokens_next(view, operand);
            int32 name = -1;
            if (operand >= range.end) {
                continue;
            }
            if (TOKEN_IS(&tokens->tokens[operand], "(")) {
                int32 close = csane_tokens_match(view, operand);
                if (close >= 0 && close < range.end) {
                    TokenRange content = {operand + 1, close};
                    name = csane_pointer_ident(file, view, content);
                }
            } else if (tokens->tokens[operand].kind == TOKEN_IDENT
                       && (end >= range.end
                           || (!TOKEN_IS(&tokens->tokens[end], "->")
                               && !TOKEN_IS(&tokens->tokens[end], ".")
                               && !TOKEN_IS(&tokens->tokens[end], "[")))) {
                name = operand;
            }
            if (name >= 0 && csane_pointer_known(file, state, stmt, name)) {
                csane_rule_issue(file, token->offset,
                                 STRLIT("STYLE-POINTER-NOT"),
                                 "use `pointer == NULL` instead of `!pointer`");
            }
        } else if (TOKEN_IS(token, "!=")) {
            int32 name = csane_pointer_ne_name(file, view, i);
            if (name >= 0 && csane_pointer_known(file, state, stmt, name)) {
                csane_rule_issue(file, token->offset,
                                 STRLIT("STYLE-POINTER-NE-NULL"),
                                 "use `if (pointer)` instead of `!= NULL`");
            }
        }
    }
}

static bool
csane_pointer_free_body(CsaneFile *file, CsaneStatement *stmt,
                        TokenRange guard) {
    CsaneStatement *body = NULL;
    CsaneTokenView view;
    Tokenization *tokens = file->tokens;
    TokenRange args[3];
    int32 first;
    int32 open;
    int32 close;
    int32 count;
    int32 body_index = stmt->body;

    if (body_index < 0 || stmt->alternative >= 0) {
        return false;
    }
    body = &file->statements->items[body_index];
    if (body->kind == CSANE_STMT_BLOCK) {
        if (body->first_child < 0
            || body->first_child != body->last_child) {
            return false;
        }
        body = &file->statements->items[body->first_child];
    }
    if (body->kind != CSANE_STMT_SIMPLE
        || !csane_tokens_view(&view, tokens, body->tokens)) {
        return false;
    }
    first = csane_tokens_next(&view, body->tokens.first - 1);
    open = csane_tokens_next(&view, first);
    if (first >= view.range.end || open >= view.range.end
        || !TOKEN_IS(&tokens->tokens[first], "free2")
        || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return false;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0 || csane_tokens_next(&view, close) >= view.range.end
        || !TOKEN_IS(&tokens->tokens[csane_tokens_next(&view, close)], ";")) {
        return false;
    }
    count = csane_tokens_arguments(&view, open, args, LENGTH(args));
    if (count != 2) {
        return false;
    }
    return csane_pointer_lvalue(file, &view, args[0])
           && csane_pointer_same_range(file, guard, args[0]);
}

/* Variables introduced by `for (Type name = ...; ...)` have the for
 * statement as their scope, not the surrounding block. */
static void
csane_pointer_for(CsaneFile *file, CsanePointerState *state,
                  CsaneStatement *stmt, int32 index) {
    CsaneTokenView view;
    CsaneTokenView init;
    int32 first;
    int32 open;
    int32 close;
    int32 semicolon;

    if (!csane_tokens_view(&view, file->tokens, stmt->header)) {
        return;
    }
    first = csane_tokens_next(&view, stmt->header.first - 1);
    open = csane_tokens_next(&view, first);
    if (first >= view.range.end || open >= view.range.end
        || !TOKEN_IS(&file->tokens->tokens[first], "for")
        || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0
        || !csane_tokens_subview(&init, &view,
                                 (TokenRange){open + 1, close})) {
        return;
    }
    semicolon = csane_tokens_top_level(&init, init.range.first - 1,
                                       STRLIT(";"));
    if (semicolon < 0 || semicolon >= init.range.end) {
        return;
    }
    csane_pointer_declarations(file, state,
                               (TokenRange){open + 1, semicolon},
                               stmt->function, index,
                               stmt->owner_region);
}

void
csane_pointer_check(CsaneFile *file) {
    CsanePointerState state = {0};

    if (file->statements == NULL || file->toplevel == NULL
        || file->is_ryu || csane_calls_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION) {
            csane_pointer_declarations(file, &state, item->tokens,
                                       -1, -1, item->owner_region);
        }
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        csane_pointer_parameters(file, &state, i);
    }
    for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        CsaneTokenView view;
        int32 first;
        int32 open;
        int32 close;
        TokenRange condition;
        TokenRange guard;

        if (stmt->kind == CSANE_STMT_FOR) {
            csane_pointer_for(file, &state, stmt, i);
        }
        if (stmt->kind == CSANE_STMT_SIMPLE) {
            csane_pointer_declarations(file, &state, stmt->tokens,
                                       stmt->function, stmt->parent,
                                       stmt->owner_region);
        }
        if (stmt->kind != CSANE_STMT_IF
            || !csane_tokens_view(&view, file->tokens, stmt->header)) {
            continue;
        }
        first = csane_tokens_next(&view, stmt->header.first - 1);
        open = csane_tokens_next(&view, first);
        if (!TOKEN_IS(&file->tokens->tokens[first], "if")
            || open >= view.range.end
            || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
            continue;
        }
        close = csane_tokens_match(&view, open);
        if (close < 0) {
            continue;
        }
        condition = (TokenRange){open + 1, close};
        csane_pointer_condition(file, &state, stmt, &view, condition);
        if (csane_pointer_positive(file, &view, condition, &guard)
            && csane_pointer_free_body(file, stmt, guard)) {
            csane_rule_issue(file, file->tokens->tokens[guard.first].offset,
                             STRLIT("STYLE-FREE2-GUARD"),
                             "free2 already accepts NULL; remove the guard");
        }
    }
    ARRAY_FREE(state.aliases);
    ARRAY_FREE(state.symbols);
}

#endif /* CSANECHECK_POINTER_C */
