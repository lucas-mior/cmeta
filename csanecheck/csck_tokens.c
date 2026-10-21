// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOKENS_C)
#define CSANECHECK_TOKENS_C

#include "cbase.h"
#include "csck_tokens.h"

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
    *view = (CsaneTokenView){tokens, range};
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
        || range.first < parent->range.first
        || range.end > parent->range.end) {
        return false;
    }
    return csane_tokens_view(child, parent->tokens, range);
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

int32
csane_tokens_next(CsaneTokenView *view, int32 cursor) {
    if ((view == NULL) || (view->tokens == NULL)) {
        return -1;
    }
    return token_range_next_significant(view->tokens, view->range, cursor);
}

int32
csane_tokens_previous(CsaneTokenView *view, int32 cursor) {
    if ((view == NULL) || (view->tokens == NULL)) {
        return -1;
    }
    return token_range_previous_significant(view->tokens, view->range, cursor);
}

int32
csane_tokens_match(CsaneTokenView *view, int32 index) {
    Token *token;
    Tokenization *tokens;
    TokenRange range;
    if ((view == NULL) || (view->tokens == NULL)
        || (index < view->range.first) || (index >= view->range.end)) {
        return -1;
    }
    tokens = view->tokens;
    range = view->range;
    token = &tokens->tokens[index];
    if (token_is_open_delimiter(token)) {
        return token_range_matching_delimiter_forward(tokens, range, index);
    }
    if (token_is_close_delimiter(token)) {
        return token_range_matching_delimiter_reverse(tokens, range, index);
    }
    return -1;
}

bool
csane_tokens_group(CsaneTokenView *view, int32 open,
                   TokenRange *content, int32 *closing) {
    if (view == NULL || view->tokens == NULL
        || open < view->range.first || open >= view->range.end
        || !token_is_open_delimiter(&view->tokens->tokens[open])) {
        return false;
    }
    int32 close = csane_tokens_match(view, open);
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

TokenRange
csane_tokens_unwrap(CsaneTokenView *view, TokenRange range) {
    CsaneTokenView local;
    if (!csane_tokens_subview(&local, view, range)) {
        return range;
    }
    for (;;) {
        TokenRange trimmed = token_range_trim_trivia(view->tokens, range);
        int32 first = trimmed.first;
        int32 last = trimmed.end - 1;
        if (first >= last
            || !TOKEN_IS(&view->tokens->tokens[first], "(")
            || !csane_tokens_subview(&local, view, trimmed)
            || csane_tokens_match(&local, first) != last) {
            break;
        }
        range = (TokenRange){first + 1, last};
    }
    return range;
}

bool
csane_tokens_balanced(CsaneTokenView *view) {
    return (view != NULL) && (view->tokens != NULL)
           && token_range_is_balanced(view->tokens, view->range);
}

int32
csane_tokens_top_level(CsaneTokenView *view, int32 cursor,
                       char *text, int32 len) {
    if ((view == NULL) || (view->tokens == NULL) || (text == NULL)
        || (len <= 0) || !csane_tokens_balanced(view)) {
        return -1;
    }
    TokenDelimiterDepth zero = {0};
    return token_range_next_text_at_depth(view->tokens, view->range, cursor,
                                          zero, text, len);
}

int32
csane_tokens_split(CsaneTokenView *view, char *separator, int32 len,
                   TokenRange *items, int32 capacity) {
    if ((view == NULL) || (view->tokens == NULL)) {
        return -1;
    }
    return token_range_split_collect(view->tokens, view->range, separator,
                                     len, items, capacity);
}

int32
csane_tokens_arguments(CsaneTokenView *view, int32 open,
                       TokenRange *args, int32 capacity) {
    TokenRange content;
    CsaneTokenView inner;
    if (view == NULL || view->tokens == NULL || capacity < 0
        || (capacity > 0 && args == NULL)
        || open < view->range.first || open >= view->range.end
        || !TOKEN_IS(&view->tokens->tokens[open], "(")
        || !csane_tokens_group(view, open, &content, NULL)
        || !csane_tokens_subview(&inner, view, content)) {
        return -1;
    }
    if (token_range_is_empty(view->tokens, content)) {
        return 0;
    }
    int32 count = csane_tokens_split(&inner, ",", 1, args, capacity);
    if (count < 0 || count > capacity) {
        return -1;
    }
    for (int32 i = 0; i < count; i += 1) {
        if (token_range_is_empty(view->tokens, args[i])) {
            return -1;
        }
    }
    return count;
}

bool
csane_tokens_assignment(CsaneTokenView *view, TokenRange *left,
                        TokenRange *right, int32 *operator_index) {
    static char *operators[] = {
        "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=",
    };
    if (view == NULL || view->tokens == NULL
        || !csane_tokens_balanced(view)) {
        return false;
    }
    int32 chosen = view->range.end;
    for (int32 i = 0; i < SIZEOF(operators)/SIZEOF(operators[0]); i += 1) {
        char *op = operators[i];
        int32 index = csane_tokens_top_level(view, view->range.first - 1,
                                             op, strlen32(op));
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
    TokenRange lhs = {view->range.first, chosen};
    TokenRange rhs = {chosen + 1, view->range.end};
    if (token_range_is_empty(view->tokens, lhs)
        || token_range_is_empty(view->tokens, rhs)) {
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
    if ((view == NULL) || (view->tokens == NULL)) {
        return (SourceRange){0};
    }
    return token_range_source_range(view->tokens, view->range);
}

#endif /* CSANECHECK_TOKENS_C */
