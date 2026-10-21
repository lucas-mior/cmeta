// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TYPE_HINTS_C)
#define CSANECHECK_TYPE_HINTS_C

#include "cbase.h"
#include "csck_type_hints.h"
#include "csck_tokens.h"

static bool
csane_hint_same(Token *a, Token *b) {
    return a->kind == TOKEN_IDENT && b->kind == TOKEN_IDENT
           && a->len == b->len && !memcmp64(a->text, b->text, a->len);
}

static void
csane_hint_add(CsaneTypeHints *hints, int32 token,
               enum CsaneTypeHintKind kind) {
    Token *candidate = &hints->tokens->tokens[token];
    if (candidate->kind != TOKEN_IDENT) {
        return;
    }
    for (int32 i = 0; i < hints->count; i += 1) {
        CsaneTypeHint *hint = &hints->items[i];
        if (hint->kind == kind
            && csane_hint_same(candidate,
                               &hints->tokens->tokens[hint->token])) {
            return;
        }
    }
    if (hints->count == hints->capacity) {
        int32 next = hints->capacity ? hints->capacity*2 : 32;
        hints->items = realloc2(hints->items, hints->capacity, next,
                                SIZEOF(*hints->items));
        hints->capacity = next;
    }
    hints->items[hints->count] = (CsaneTypeHint){token, kind};
    hints->count += 1;
    return;
}

bool
csane_type_hints_has(CsaneTypeHints *hints, Token *token,
                     enum CsaneTypeHintKind kind) {
    if (hints == NULL || hints->tokens == NULL || token == NULL
        || token->kind != TOKEN_IDENT) {
        return false;
    }
    for (int32 i = 0; i < hints->count; i += 1) {
        CsaneTypeHint *hint = &hints->items[i];
        if (hint->kind == kind
            && csane_hint_same(token, &hints->tokens->tokens[hint->token])) {
            return true;
        }
    }
    return false;
}

bool
csane_type_hints_typedef(CsaneTypeHints *hints, Token *token) {
    return csane_type_hints_has(hints, token, CSANE_TYPE_HINT_TYPEDEF);
}

bool
csane_type_hints_tag(CsaneTypeHints *hints, Token *token) {
    return csane_type_hints_has(hints, token, CSANE_TYPE_HINT_TAG);
}

static bool
csane_hint_word(Token *token, char *word) {
    return token->kind == TOKEN_IDENT && TOKEN_IS(token, word);
}

static bool
csane_hint_aggregate(Token *token) {
    return csane_hint_word(token, "struct") || csane_hint_word(token, "union")
           || csane_hint_word(token, "enum");
}

static int32
csane_hint_alias_before(CsaneTokenView *view, int32 end) {
    Tokenization *tokens = view->tokens;
    int32 i = csane_tokens_previous(view, end);
    if (i < view->range.first) {
        return -1;
    }
    if (TOKEN_IS(&tokens->tokens[i], "]")) {
        int32 open = csane_tokens_match(view, i);
        if (open < 0) {
            return -1;
        }
        i = csane_tokens_previous(view, open);
    }
    if (i < view->range.first || tokens->tokens[i].kind != TOKEN_IDENT) {
        return -1;
    }
    return i;
}

/* Collect simple typedef declarators only. Complex function-pointer typedefs
 * are intentionally left unknown rather than guessed. */
static void
csane_hint_typedef(CsaneTypeHints *hints, CsaneTokenView *view,
                   int32 first) {
    Tokenization *tokens = view->tokens;
    int32 segment = csane_tokens_next(view, first);
    int32 i = segment;
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &tokens->tokens[i];
        if (TOKEN_IS(token, "{") || TOKEN_IS(token, "(")
            || TOKEN_IS(token, "[")) {
            int32 close = csane_tokens_match(view, i);
            if (close < 0) {
                return;
            }
            i = csane_tokens_next(view, close);
            continue;
        }
        if (TOKEN_IS(token, ",") || TOKEN_IS(token, ";")) {
            int32 alias = csane_hint_alias_before(view, i);
            if (alias >= segment) {
                csane_hint_add(hints, alias, CSANE_TYPE_HINT_TYPEDEF);
            }
            if (TOKEN_IS(token, ";")) {
                return;
            }
            segment = csane_tokens_next(view, i);
        }
        i = csane_tokens_next(view, i);
    }
    return;
}

void
csane_type_hints_init(CsaneTypeHints *hints, CsaneRegions *regions) {
    *hints = (CsaneTypeHints){.tokens = regions->tokens};
    Tokenization *tokens = regions->tokens;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        int32 region;
        CsaneTokenView view;
        if (token->kind != TOKEN_IDENT) {
            continue;
        }
        region = csane_regions_at_token(regions, i);
        if (region < 0
            || regions->items[region].kind != CSANE_REGION_CODE
            || !csane_tokens_view(&view, tokens,
                                  regions->items[region].tokens)) {
            continue;
        }
        if (csane_hint_aggregate(token)) {
            int32 name = csane_tokens_next(&view, i);
            if (name < view.range.end
                && tokens->tokens[name].kind == TOKEN_IDENT) {
                csane_hint_add(hints, name, CSANE_TYPE_HINT_TAG);
            }
        }
        if (csane_hint_word(token, "typedef")) {
            csane_hint_typedef(hints, &view, i);
        }
    }
    return;
}

void
csane_type_hints_free(CsaneTypeHints *hints) {
    if (hints == NULL) {
        return;
    }
    free2(hints->items, (int64)hints->capacity*SIZEOF(*hints->items));
    *hints = (CsaneTypeHints){0};
    return;
}

#endif /* CSANECHECK_TYPE_HINTS_C */
