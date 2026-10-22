// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOKENS_C)
#define CSANECHECK_TOKENS_C

#include "cbase.h"
#include "csck_tokens.h"

bool
csane_token_macro_name(Token *token) {
    bool has_upper = false;
    if (token->kind != TOKEN_IDENT || token->len <= 0) {
        return false;
    }
    for (int32 i = 0; i < token->len; i += 1) {
        char c = token->text[i];
        if (c >= 'a' && c <= 'z') {
            return false;
        }
        if (c >= 'A' && c <= 'Z') {
            has_upper = true;
        }
    }
    return has_upper;
}

bool
csane_token_path_init(CsaneTokenPath *path, Tokenization *tokens,
                      TokenRange bounds) {
    if (path == NULL) {
        return false;
    }
    *path = (CsaneTokenPath){0};
    if (!token_range_is_valid(tokens, bounds)) {
        return false;
    }
    path->tokens = tokens;
    path->bounds = bounds;
    return true;
}

bool
csane_token_path_append(CsaneTokenPath *path, TokenRange range) {
    TokenRange *last;
    if (path == NULL || path->tokens == NULL
        || !token_range_is_valid(path->tokens, range)
        || range.first < path->bounds.first || range.end > path->bounds.end) {
        return false;
    }
    if (range.first == range.end) {
        return true;
    }
    if (path->count > 0) {
        last = &path->parts[path->count - 1];
        if (range.first < last->end) {
            return false;
        }
        if (range.first == last->end) {
            last->end = range.end;
            return true;
        }
    }
    if (path->count == path->capacity) {
        int32 next = path->capacity ? path->capacity*2 : 8;
        path->parts = realloc2(path->parts, path->capacity, next,
                               SIZEOF(*path->parts));
        path->capacity = next;
    }
    path->parts[path->count] = range;
    path->count += 1;
    return true;
}

void
csane_token_path_free(CsaneTokenPath *path) {
    if (path == NULL) {
        return;
    }
    free2(path->parts, (int64)path->capacity*SIZEOF(*path->parts));
    *path = (CsaneTokenPath){0};
    return;
}

bool
csane_tokens_path_view(CsaneTokenView *view, CsaneTokenPath *path) {
    if (view == NULL) {
        return false;
    }
    *view = (CsaneTokenView){0};
    if (path == NULL || path->tokens == NULL
        || !token_range_is_valid(path->tokens, path->bounds)) {
        return false;
    }
    *view = (CsaneTokenView){path->tokens, path->bounds, path};
    return true;
}

bool
csane_tokens_view(CsaneTokenView *view, Tokenization *tokens,
                  TokenRange range) {
    if (view == NULL) {
        return false;
    }
    *view = (CsaneTokenView){0};
    if (!token_range_is_valid(tokens, range)) {
        return false;
    }
    *view = (CsaneTokenView){tokens, range, NULL};
    return true;
}

bool
csane_tokens_subview(CsaneTokenView *child, CsaneTokenView *parent,
                     TokenRange range) {
    if (child == NULL) {
        return false;
    }
    *child = (CsaneTokenView){0};
    if (parent == NULL || parent->tokens == NULL
        || !token_range_is_valid(parent->tokens, range)
        || range.first < parent->range.first
        || range.end > parent->range.end) {
        return false;
    }
    *child = *parent;
    child->range = range;
    return true;
}

CsaneTokenView
csane_tokens_full(Tokenization *tokens) {
    CsaneTokenView view = {0};
    if (tokens != NULL) {
        (void)csane_tokens_view(&view, tokens,
                                (TokenRange){0, tokens->token_count});
    }
    return view;
}

static bool
csane_tokens_part(CsaneTokenView *view, int32 part, TokenRange *result) {
    TokenRange range;
    if (view == NULL || view->path == NULL || result == NULL
        || part < 0 || part >= view->path->count) {
        return false;
    }
    range = view->path->parts[part];
    if (range.first < view->range.first) {
        range.first = view->range.first;
    }
    if (range.end > view->range.end) {
        range.end = view->range.end;
    }
    if (range.first >= range.end) {
        return false;
    }
    *result = range;
    return true;
}

static bool
csane_tokens_contains(CsaneTokenView *view, int32 index) {
    TokenRange part;
    if (view == NULL || view->tokens == NULL || index < view->range.first
        || index >= view->range.end) {
        return false;
    }
    if (view->path == NULL) {
        return true;
    }
    for (int32 i = 0; i < view->path->count; i += 1) {
        if (csane_tokens_part(view, i, &part)
            && index >= part.first && index < part.end) {
            return true;
        }
    }
    return false;
}

int32
csane_tokens_next(CsaneTokenView *view, int32 cursor) {
    TokenRange part;
    int32 next;
    if (view == NULL || view->tokens == NULL) {
        return -1;
    }
    if (view->path == NULL) {
        return token_range_next_significant(view->tokens, view->range, cursor);
    }
    for (int32 i = 0; i < view->path->count; i += 1) {
        if (!csane_tokens_part(view, i, &part) || cursor >= part.end) {
            continue;
        }
        next = token_range_next_significant(view->tokens, part, cursor);
        if (next < part.end) {
            return next;
        }
    }
    return view->range.end;
}

int32
csane_tokens_previous(CsaneTokenView *view, int32 cursor) {
    TokenRange part;
    int32 previous;
    if (view == NULL || view->tokens == NULL) {
        return -1;
    }
    if (view->path == NULL) {
        return token_range_previous_significant(view->tokens, view->range,
                                                cursor);
    }
    for (int32 i = view->path->count - 1; i >= 0; i -= 1) {
        if (!csane_tokens_part(view, i, &part) || cursor <= part.first) {
            continue;
        }
        previous = token_range_previous_significant(view->tokens, part, cursor);
        if (previous >= part.first) {
            return previous;
        }
    }
    return view->range.first - 1;
}

static bool
csane_tokens_pair(Token *open, Token *close) {
    return (TOKEN_IS(open, "(") && TOKEN_IS(close, ")"))
           || (TOKEN_IS(open, "[") && TOKEN_IS(close, "]"))
           || (TOKEN_IS(open, "{") && TOKEN_IS(close, "}"));
}

static int32
csane_tokens_match_forward(CsaneTokenView *view, int32 index) {
    Token *open = &view->tokens->tokens[index];
    int32 i = csane_tokens_next(view, index);
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &view->tokens->tokens[i];
        if (token_is_open_delimiter(token)) {
            int32 close = csane_tokens_match_forward(view, i);
            if (close < 0) {
                return -1;
            }
            i = csane_tokens_next(view, close);
            continue;
        }
        if (token_is_close_delimiter(token)) {
            return csane_tokens_pair(open, token) ? i : -1;
        }
        i = csane_tokens_next(view, i);
    }
    return -1;
}

static int32
csane_tokens_match_reverse(CsaneTokenView *view, int32 index) {
    Token *close = &view->tokens->tokens[index];
    int32 i = csane_tokens_previous(view, index);
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &view->tokens->tokens[i];
        if (token_is_close_delimiter(token)) {
            int32 open = csane_tokens_match_reverse(view, i);
            if (open < 0) {
                return -1;
            }
            i = csane_tokens_previous(view, open);
            continue;
        }
        if (token_is_open_delimiter(token)) {
            return csane_tokens_pair(token, close) ? i : -1;
        }
        i = csane_tokens_previous(view, i);
    }
    return -1;
}

int32
csane_tokens_match(CsaneTokenView *view, int32 index) {
    Token *token;
    Tokenization *tokens;
    TokenRange range;
    if (!csane_tokens_contains(view, index)) {
        return -1;
    }
    token = &view->tokens->tokens[index];
    tokens = view->tokens;
    range = view->range;
    if (view->path == NULL) {
        if (token_is_open_delimiter(token)) {
            return token_range_matching_delimiter_forward(tokens, range, index);
        }
        if (token_is_close_delimiter(token)) {
            return token_range_matching_delimiter_reverse(tokens, range, index);
        }
        return -1;
    }
    if (token_is_open_delimiter(token)) {
        return csane_tokens_match_forward(view, index);
    }
    if (token_is_close_delimiter(token)) {
        return csane_tokens_match_reverse(view, index);
    }
    return -1;
}

bool
csane_tokens_group(CsaneTokenView *view, int32 open,
                   TokenRange *content, int32 *closing) {
    int32 close;
    if (!csane_tokens_contains(view, open)
        || !token_is_open_delimiter(&view->tokens->tokens[open])) {
        return false;
    }
    close = csane_tokens_match(view, open);
    if (close < 0) {
        return false;
    }
    if (content != NULL) {
        *content = (TokenRange){open + 1, close};
    }
    if (closing != NULL) {
        *closing = close;
    }
    return true;
}

static TokenRange
csane_tokens_trim(CsaneTokenView *view) {
    TokenRange result;
    int32 first = csane_tokens_next(view, view->range.first - 1);
    int32 last;
    if (first < view->range.first || first >= view->range.end) {
        return (TokenRange){view->range.end, view->range.end};
    }
    last = csane_tokens_previous(view, view->range.end);
    result.first = first;
    result.end = last + 1;
    return result;
}

TokenRange
csane_tokens_unwrap(CsaneTokenView *view, TokenRange range) {
    CsaneTokenView local;
    TokenRange trimmed;
    int32 first;
    int32 last;
    if (!csane_tokens_subview(&local, view, range)) {
        return range;
    }
    for (;;) {
        trimmed = csane_tokens_trim(&local);
        first = trimmed.first;
        last = trimmed.end - 1;
        if (first >= last || !TOKEN_IS(&view->tokens->tokens[first], "(")
            || csane_tokens_match(&local, first) != last) {
            break;
        }
        range = (TokenRange){first + 1, last};
        if (!csane_tokens_subview(&local, view, range)) {
            break;
        }
    }
    return range;
}

bool
csane_tokens_balanced(CsaneTokenView *view) {
    int32 i;
    if (view == NULL || view->tokens == NULL) {
        return false;
    }
    if (view->path == NULL) {
        return token_range_is_balanced(view->tokens, view->range);
    }
    i = csane_tokens_next(view, view->range.first - 1);
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &view->tokens->tokens[i];
        if (token_is_open_delimiter(token)) {
            int32 close = csane_tokens_match(view, i);
            if (close < 0) {
                return false;
            }
            i = csane_tokens_next(view, close);
            continue;
        }
        if (token_is_close_delimiter(token)) {
            return false;
        }
        i = csane_tokens_next(view, i);
    }
    return true;
}

int32
csane_tokens_top_level(CsaneTokenView *view, int32 cursor,
                       char *text, int32 len) {
    int32 i;
    if (view == NULL || view->tokens == NULL || text == NULL || len <= 0
        || !csane_tokens_balanced(view)) {
        return -1;
    }
    if (view->path == NULL) {
        TokenDelimiterDepth zero = {0};
        return token_range_next_text_at_depth(view->tokens, view->range,
                                              cursor, zero, text, len);
    }
    i = csane_tokens_next(view, cursor);
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &view->tokens->tokens[i];
        if (TOKEN_IS(token, text, len)) {
            return i;
        }
        if (token_is_open_delimiter(token)) {
            i = csane_tokens_match(view, i);
            if (i < 0) {
                return -1;
            }
        }
        i = csane_tokens_next(view, i);
    }
    return view->range.end;
}

static bool
csane_tokens_empty(CsaneTokenView *view) {
    int32 first;
    if (view == NULL || view->tokens == NULL) {
        return true;
    }
    first = csane_tokens_next(view, view->range.first - 1);
    return first < view->range.first || first >= view->range.end;
}

int32
csane_tokens_split(CsaneTokenView *view, char *separator, int32 len,
                   TokenRange *items, int32 capacity) {
    int32 count = 0;
    int32 first;
    int32 i;
    if (view == NULL || view->tokens == NULL || separator == NULL || len <= 0
        || capacity < 0 || (capacity > 0 && items == NULL)
        || !csane_tokens_balanced(view)) {
        return -1;
    }
    if (view->path == NULL) {
        return token_range_split_collect(view->tokens, view->range, separator,
                                         len, items, capacity);
    }
    if (csane_tokens_empty(view)) {
        return 0;
    }
    first = view->range.first;
    i = csane_tokens_next(view, view->range.first - 1);
    while (i >= view->range.first && i < view->range.end) {
        Token *token = &view->tokens->tokens[i];
        if (token_is_open_delimiter(token)) {
            i = csane_tokens_match(view, i);
            if (i < 0) {
                return -1;
            }
        } else if (TOKEN_IS(token, separator, len)) {
            if (count < capacity) {
                items[count] = (TokenRange){first, i};
            }
            count += 1;
            first = i + 1;
        }
        i = csane_tokens_next(view, i);
    }
    if (count < capacity) {
        items[count] = (TokenRange){first, view->range.end};
    }
    count += 1;
    return count;
}

int32
csane_tokens_arguments(CsaneTokenView *view, int32 open,
                       TokenRange *args, int32 capacity) {
    TokenRange content;
    CsaneTokenView inner;
    int32 count;
    if (view == NULL || view->tokens == NULL || capacity < 0
        || (capacity > 0 && args == NULL)
        || !csane_tokens_contains(view, open)
        || !TOKEN_IS(&view->tokens->tokens[open], "(")
        || !csane_tokens_group(view, open, &content, NULL)
        || !csane_tokens_subview(&inner, view, content)) {
        return -1;
    }
    if (csane_tokens_empty(&inner)) {
        return 0;
    }
    count = csane_tokens_split(&inner, STRLIT(","), args, capacity);
    if (count < 0 || count > capacity) {
        return -1;
    }
    for (int32 i = 0; i < count; i += 1) {
        CsaneTokenView argument;
        if (!csane_tokens_subview(&argument, &inner, args[i])
            || csane_tokens_empty(&argument)) {
            return -1;
        }
    }
    return count;
}

static StrFlex *csane_token_assignments[] = {
    SFLIT("="), SFLIT("+="), SFLIT("-="), SFLIT("*="),
    SFLIT("/="), SFLIT("%="), SFLIT("<<="), SFLIT(">>="),
    SFLIT("&="), SFLIT("^="), SFLIT("|="),
};

bool
csane_tokens_assignment(CsaneTokenView *view, TokenRange *left,
                        TokenRange *right, int32 *operator_index) {
    int32 chosen;
    int32 count = SIZEOF(csane_token_assignments)
                  /SIZEOF(csane_token_assignments[0]);
    TokenRange lhs;
    TokenRange rhs;
    CsaneTokenView lhs_view;
    CsaneTokenView rhs_view;
    if (view == NULL || view->tokens == NULL
        || !csane_tokens_balanced(view)) {
        return false;
    }
    chosen = view->range.end;
    for (int32 i = 0; i < count; i += 1) {
        StrFlex *op = csane_token_assignments[i];
        int32 index = csane_tokens_top_level(view, view->range.first - 1,
                                             op->data, op->len);
        if (index < 0) {
            return false;
        }
        if (index < chosen) {
            chosen = index;
        }
    }
    if (chosen == view->range.end) {
        return false;
    }
    lhs = (TokenRange){view->range.first, chosen};
    rhs = (TokenRange){chosen + 1, view->range.end};
    if (!csane_tokens_subview(&lhs_view, view, lhs)
        || !csane_tokens_subview(&rhs_view, view, rhs)
        || csane_tokens_empty(&lhs_view) || csane_tokens_empty(&rhs_view)) {
        return false;
    }
    if (left != NULL) {
        *left = lhs;
    }
    if (right != NULL) {
        *right = rhs;
    }
    if (operator_index != NULL) {
        *operator_index = chosen;
    }
    return true;
}

SourceRange
csane_tokens_source(CsaneTokenView *view) {
    if (view == NULL || view->tokens == NULL) {
        return (SourceRange){0};
    }
    return token_range_source_range(view->tokens, view->range);
}

#endif /* CSANECHECK_TOKENS_C */
