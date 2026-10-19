// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOKENS_C)
#define CSANECHECK_TOKENS_C

#include "cbase.h"
#include "csanecheck_tokens.h"

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

SourceRange
csane_tokens_source(CsaneTokenView *view) {
    if ((view == NULL) || (view->tokens == NULL)) {
        return (SourceRange){0};
    }
    return token_range_source_range(view->tokens, view->range);
}

#endif /* CSANECHECK_TOKENS_C */
