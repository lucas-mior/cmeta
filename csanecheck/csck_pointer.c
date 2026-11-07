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

/* A declarator is either '*name', 'name', or '(*name)(...)'. Avoid
 * expressions with a second identifier or an unexpected operator. */

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
    bool _i38_predicate;
    bool _i38_result;
    do {
        CsaneTokenView *_i38_view = &view;
        int32 *_i38_position = &first;
        bool *_i38_alias_pointer = &alias_pointer;

        Tokenization *tokens = file->tokens;
        int32 i = *_i38_position;
        bool known = false;
        bool aggregate = false;

        *_i38_alias_pointer = false;
        while (i < _i38_view->range.end
               && csane_pointer_qualifier(&tokens->tokens[i])) {
            i = csane_tokens_next(_i38_view, i);
        }
        if (i >= _i38_view->range.end) {
            _i38_result = false;
            goto _i38_done;
        }
        if (TOKEN_IS(&tokens->tokens[i], "struct")
            || TOKEN_IS(&tokens->tokens[i], "union")
            || TOKEN_IS(&tokens->tokens[i], "enum")) {
            aggregate = true;
            i = csane_tokens_next(_i38_view, i);
            if (i >= _i38_view->range.end
                || tokens->tokens[i].kind != TOKEN_IDENT) {
                _i38_result = false;
                goto _i38_done;
            }
            i = csane_tokens_next(_i38_view, i);
            known = true;
        } else if (csane_name_kind(&tokens->tokens[i])
            == CSANE_NAME_PRIMITIVE) {
            known = true;
            i = csane_tokens_next(_i38_view, i);
            while (i < _i38_view->range.end
                   && csane_name_kind(&tokens->tokens[i])
                      == CSANE_NAME_PRIMITIVE) {
                i = csane_tokens_next(_i38_view, i);
            }
        } else if (csane_type_hints_typedef(file->type_hints,
                                            &tokens->tokens[i])
                   || csane_name_kind(&tokens->tokens[i]) == CSANE_NAME_TYPE) {
            known = true;
            for (int32 a = ARRAY_LEN(state->aliases) - 1; a >= 0; a -= 1) {
                CsanePointerTypes *alias = &state->aliases[a];
                if (csane_pointer_same(&tokens->tokens[i],
                                       &file->tokens->tokens[alias->name])) {
                    *_i38_alias_pointer = alias->pointer;
                    break;
                }
            }
            i = csane_tokens_next(_i38_view, i);
        }
        if (!known || (aggregate && i < _i38_view->range.end
                       && TOKEN_IS(&tokens->tokens[i], "{"))) {
            _i38_result = false;
            goto _i38_done;
        }
        while (i < _i38_view->range.end
               && csane_pointer_qualifier(&tokens->tokens[i])) {
            i = csane_tokens_next(_i38_view, i);
        }
        *_i38_position = i;
        _i38_result = true;
        goto _i38_done;
        _i38_done: ;
    } while (false);
    _i38_predicate = !_i38_result;
    if (_i38_predicate) {
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
        int32 _i39_result;
        do {
            CsaneTokenView *_i39_view = &view;
            bool *_i39_pointer = &pointer;

            Tokenization *tokens = file->tokens;
            int32 end = csane_tokens_top_level(_i39_view, segment.first,
                                               STRLIT("="));
            int32 i = segment.first;
            int32 name = -1;
            bool star = false;
            bool paren = false;

            if (end < 0) {
                _i39_result = -1;
                goto _i39_done;
            }
            if (end > segment.end) {
                end = segment.end;
            }
            if (i < end && TOKEN_IS(&tokens->tokens[i], "(")) {
                paren = true;
                i = csane_tokens_next(_i39_view, i);
            }
            while (i < end) {
                Token *current = &tokens->tokens[i];
                if (!TOKEN_IS(current, "*")
                    && !csane_pointer_qualifier(current)) {
                    break;
                }
                if (TOKEN_IS(current, "*")) {
                    star = true;
                }
                i = csane_tokens_next(_i39_view, i);
            }
            if (i >= end || tokens->tokens[i].kind != TOKEN_IDENT) {
                _i39_result = -1;
                goto _i39_done;
            }
            name = i;
            i = csane_tokens_next(_i39_view, i);
            if (paren) {
                if (i >= end || !TOKEN_IS(&tokens->tokens[i], ")")) {
                    _i39_result = -1;
                    goto _i39_done;
                }
                i = csane_tokens_next(_i39_view, i);
            }
            *_i39_pointer = star || alias_pointer;
            if (!paren && i < end && TOKEN_IS(&tokens->tokens[i], "[")) {
                *_i39_pointer = false;
            }
            while (i < end && (TOKEN_IS(&tokens->tokens[i], "[")
                               || TOKEN_IS(&tokens->tokens[i], "("))) {
                int32 close = csane_tokens_match(_i39_view, i);
                if (close < 0 || close >= end) {
                    _i39_result = -1;
                    goto _i39_done;
                }
                i = csane_tokens_next(_i39_view, close);
            }
            _i39_result = i == end ? name : -1;
            goto _i39_done;
            _i39_done: ;
        } while (false);
        name = _i39_result;
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


/* Only ancestor block declarations belong to the lexical scope of the if.
 * Preserve non-pointer declarations so that they shadow outer pointers. */


static bool
csane_pointer_known(CsaneFile *file, CsanePointerState *state,
                    CsaneStatement *stmt, int32 name) {
    Token *wanted = &file->tokens->tokens[name];
    for (int32 i = ARRAY_LEN(state->symbols) - 1; i >= 0; i -= 1) {
        CsanePointerSymbol *symbol = &state->symbols[i];
        bool _i42_predicate;
        _i42_predicate = symbol->function != stmt->function;
        if (_i42_predicate) {
            _i42_predicate = symbol->function >= 0;
        }
        if (!_i42_predicate) {
            _i42_predicate = symbol->position >= stmt->source.start;
        }
        if (!_i42_predicate) {
            bool _i42_result;
            do {
                int32 _i42_ancestor = symbol->region;
                int32 _i42_region = stmt->owner_region;

                if (_i42_ancestor < 0) {
                    _i42_result = true;
                    goto _i42_done;
                }
                for (int32 i = _i42_region; i >= 0; i =
                    file->regions->items[i].parent) {
                    if (i == _i42_ancestor) {
                        _i42_result = true;
                        goto _i42_done;
                    }
                }
                _i42_result = false;
                goto _i42_done;
                _i42_done: ;
            } while (false);
            _i42_predicate = !_i42_result;
        }
        if (!_i42_predicate) {
            Token *symbol_token = &file->tokens->tokens[symbol->name];
            _i42_predicate = !csane_pointer_same(wanted, symbol_token);
        }
        if (_i42_predicate) {
            continue;
        }
        bool _i41_predicate;
        _i41_predicate = symbol->function < 0;
        if (!_i41_predicate) {
            bool _i41_result;
            do {
                int32 scope = stmt->parent;
                if (symbol->scope == -1) {
                    _i41_result = true;
                    goto _i41_done;
                }
                while (scope >= 0) {
                    if (scope == symbol->scope) {
                        _i41_result = true;
                        goto _i41_done;
                    }
                    scope = file->statements->items[scope].parent;
                }
                _i41_result = false;
                goto _i41_done;
                _i41_done: ;
            } while (false);
            _i41_predicate = _i41_result;
        }
        if (_i41_predicate) {
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
        do {
            CsanePointerState *_i40_state = &state;
            int32 _i40_function = i;

            CsaneTopItem *top = &file->toplevel->items[_i40_function];
            CsaneTokenView view;
            int32 open;
            int32 close;
            int32 part;

            if (top->kind != CSANE_TOP_FUNCTION_DEFINITION
                || top->name_token < 0
                || !csane_tokens_view(&view, file->tokens, top->header)) {
                goto _i40_done;
            }
            open = csane_tokens_next(&view, top->name_token);
            if (open >= view.range.end
                || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
                goto _i40_done;
            }
            close = csane_tokens_match(&view, open);
            if (close < 0) {
                goto _i40_done;
            }
            view.range = (TokenRange){open + 1, close};
            part = csane_tokens_next(&view, open);
            while (part < close) {
                int32 comma = csane_tokens_top_level(&view, part, STRLIT(","));
                if (comma < 0) {
                    goto _i40_done;
                }
                TokenRange segment = {part, comma};
                csane_pointer_declarations(file, _i40_state, segment,
                                           _i40_function, -1,
                                           top->owner_region);
                if (comma >= close) {
                    break;
                }
                part = csane_tokens_next(&view, comma);
            }
            _i40_done: ;
        } while (false);
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
        bool _i45_predicate;
        bool _i43_result;
        do {
            CsaneTokenView *_i43_view = &view;
            TokenRange _i43_range = condition;
            TokenRange *_i43_guard = &guard;

            int32 comparison;
            TokenRange left;
            TokenRange right;
            CsaneTokenView content;

            _i43_range = csane_tokens_unwrap(_i43_view, _i43_range);
            if (!csane_tokens_subview(&content, _i43_view, _i43_range)) {
                _i43_result = false;
                goto _i43_done;
            }
            _i43_view = &content;
            if (csane_pointer_lvalue(file, _i43_view, _i43_range)) {
                *_i43_guard = _i43_range;
                _i43_result = true;
                goto _i43_done;
            }
            comparison = csane_tokens_top_level(_i43_view, _i43_range.first,
                                                STRLIT("!="));
            if (comparison < _i43_range.first || comparison >= _i43_range.end) {
                _i43_result = false;
                goto _i43_done;
            }
            left = (TokenRange){_i43_range.first, comparison};
            right = (TokenRange){comparison + 1, _i43_range.end};
            if (csane_pointer_lvalue(file, _i43_view, left)
                && csane_pointer_null(file, _i43_view, right)) {
                *_i43_guard = csane_tokens_unwrap(_i43_view, left);
                _i43_result = true;
                goto _i43_done;
            }
            if (csane_pointer_null(file, _i43_view, left)
                && csane_pointer_lvalue(file, _i43_view, right)) {
                *_i43_guard = csane_tokens_unwrap(_i43_view, right);
                _i43_result = true;
                goto _i43_done;
            }
            _i43_result = false;
            goto _i43_done;
            _i43_done: ;
        } while (false);
        _i45_predicate = _i43_result;
        if (_i45_predicate) {
            bool _i45_result;
            do {
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
                    _i45_result = false;
                    goto _i45_done;
                }
                body = &file->statements->items[body_index];
                if (body->kind == CSANE_STMT_BLOCK) {
                    if (body->first_child < 0
                        || body->first_child != body->last_child) {
                        _i45_result = false;
                        goto _i45_done;
                    }
                    body = &file->statements->items[body->first_child];
                }
                if (body->kind != CSANE_STMT_SIMPLE
                    || !csane_tokens_view(&view, tokens, body->tokens)) {
                    _i45_result = false;
                    goto _i45_done;
                }
                first = csane_tokens_next(&view, body->tokens.first - 1);
                open = csane_tokens_next(&view, first);
                if (first >= view.range.end || open >= view.range.end
                    || !TOKEN_IS(&tokens->tokens[first], "free2")
                    || !TOKEN_IS(&tokens->tokens[open], "(")) {
                    _i45_result = false;
                    goto _i45_done;
                }
                close = csane_tokens_match(&view, open);
                int32 next = close < 0 ? -1 : csane_tokens_next(&view, close);
                if (close < 0 || next >= view.range.end
                    || !TOKEN_IS(&tokens->tokens[next], ";")) {
                    _i45_result = false;
                    goto _i45_done;
                }
                count = csane_tokens_arguments(&view, open, args, LENGTH(args));
                if (count != 2) {
                    _i45_result = false;
                    goto _i45_done;
                }
                       bool _i44_result;
                       do {
                           TokenRange _i44_a = guard;
                           TokenRange _i44_b = args[0];

                           Tokenization *tokens = file->tokens;
                           CsaneTokenView view_a;
                           CsaneTokenView view_b;
                           int32 i;
                           int32 j;

                           if (!csane_tokens_view(&view_a, tokens, _i44_a)
                               || !csane_tokens_view(&view_b, tokens, _i44_b)) {
                               _i44_result = false;
                               goto _i44_done;
                           }
                           _i44_a = csane_tokens_unwrap(&view_a, _i44_a);
                           _i44_b = csane_tokens_unwrap(&view_b, _i44_b);
                           i = csane_tokens_next(&view_a, _i44_a.first - 1);
                           j = csane_tokens_next(&view_b, _i44_b.first - 1);
                           while (i < _i44_a.end && j < _i44_b.end) {
                               Token *a = &tokens->tokens[i];
                               Token *b = &tokens->tokens[j];
                               if (a->len != b->len
                                   || memcmp64(a->text, b->text, a->len)) {
                                   _i44_result = false;
                                   goto _i44_done;
                               }
                               i = csane_tokens_next(&view_a, i);
                               j = csane_tokens_next(&view_b, j);
                           }
                           _i44_result = i >= _i44_a.end && j >= _i44_b.end;
                           goto _i44_done;
                           _i44_done: ;
                       } while (false);
                _i45_result = csane_pointer_lvalue(file, &view, args[0])
                       && _i44_result;
                goto _i45_done;
                _i45_done: ;
            } while (false);
            _i45_predicate = _i45_result;
        }
        if (_i45_predicate) {
            csane_rule_issue(file, file->tokens->tokens[guard.first].offset,
                             STRLIT("STYLE-FREE2-GUARD"),
                             "free2 already accepts NULL; remove the guard");
        }
    }
    ARRAY_FREE(state.aliases);
    ARRAY_FREE(state.symbols);
}

#endif /* CSANECHECK_POINTER_C */
