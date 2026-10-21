// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_BOUNDS_C)
#define CSANECHECK_BOUNDS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

static TokenRange
csane_bounds_unwrap(CsaneFile *file, TokenRange range) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    return csane_tokens_unwrap(&view, range);
}

static bool
csane_bounds_token(CsaneFile *file, int32 index, char *name) {
    return index >= 0 && index < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[index], name);
}

static bool
csane_bounds_equal(CsaneFile *file, TokenRange a, TokenRange b) {
    a = csane_bounds_unwrap(file, a);
    b = csane_bounds_unwrap(file, b);
    int32 i = token_range_first_significant(file->tokens, a);
    int32 j = token_range_first_significant(file->tokens, b);
    while (i < a.end && j < b.end) {
        Token *lhs = &file->tokens->tokens[i];
        Token *rhs = &file->tokens->tokens[j];
        if (lhs->len != rhs->len
            || memcmp64(lhs->text, rhs->text, lhs->len)) {
            return false;
        }
        i = tokenization_next_significant(file->tokens, i);
        j = tokenization_next_significant(file->tokens, j);
    }
    return i >= a.end && j >= b.end;
}

static bool
csane_bounds_reference(CsaneFile *file, TokenRange range) {
    range = csane_bounds_unwrap(file, range);
    int32 i = token_range_first_significant(file->tokens, range);
    if (i >= range.end || file->tokens->tokens[i].kind != TOKEN_IDENT) {
        return false;
    }
    i = tokenization_next_significant(file->tokens, i);
    CsaneTokenView view = csane_tokens_full(file->tokens);
    while (i < range.end) {
        if (csane_bounds_token(file, i, ".")
            || csane_bounds_token(file, i, "->")) {
            i = tokenization_next_significant(file->tokens, i);
            if (i >= range.end || file->tokens->tokens[i].kind != TOKEN_IDENT) {
                return false;
            }
            i = tokenization_next_significant(file->tokens, i);
            continue;
        }
        if (!csane_bounds_token(file, i, "[")) {
            return false;
        }
        int32 close = csane_tokens_match(&view, i);
        if (close < 0 || close >= range.end) {
            return false;
        }
        int32 first = tokenization_next_significant(file->tokens, i);
        if (first >= close || tokenization_next_significant(file->tokens, first)
                              != close) {
            return false;
        }
        Token *index = &file->tokens->tokens[first];
        if (index->kind != TOKEN_IDENT && index->kind != TOKEN_LITERAL) {
            return false;
        }
        if (index->kind == TOKEN_LITERAL
            && (index->len == 0 || index->text[0] < '0'
                || index->text[0] > '9')) {
            return false;
        }
        i = tokenization_next_significant(file->tokens, close);
    }
    return true;
}

static bool
csane_bounds_zero(CsaneFile *file, TokenRange range) {
    range = csane_bounds_unwrap(file, range);
    int32 first = token_range_first_significant(file->tokens, range);
    return first < range.end && csane_bounds_token(file, first, "0")
           && tokenization_next_significant(file->tokens, first) >= range.end;
}

static bool
csane_bounds_call(CsaneFile *file, TokenRange range, char *name,
                  TokenRange *args) {
    range = csane_bounds_unwrap(file, range);
    int32 first = token_range_first_significant(file->tokens, range);
    if (first >= range.end || !csane_bounds_token(file, first, name)) {
        return false;
    }
    int32 open = tokenization_next_significant(file->tokens, first);
    CsaneTokenView view = csane_tokens_full(file->tokens);
    if (open >= range.end || !csane_bounds_token(file, open, "(")) {
        return false;
    }
    int32 close = csane_tokens_match(&view, open);
    if (close < 0 || close >= range.end
        || tokenization_next_significant(file->tokens, close) < range.end) {
        return false;
    }
    return csane_tokens_arguments(&view, open, args, 2) == 2;
}

/* Check the source gap rather than the statement tree. A macro replacement
 * may contain legitimate assertion statements without CsaneStatement nodes.
 * Comments are trivia, and a macro continuation is permitted before the one
 * required physical newline. Other intervening code breaks adjacency. */
static bool
csane_bounds_adjacent(CsaneFile *file, int32 semicolon, int32 next,
                      bool macro) {
    Tokenization *tokens = file->tokens;
    Token *last = &tokens->tokens[semicolon];
    Token *first = &tokens->tokens[next];
    SourceLocation a = tokenization_token_location(tokens, semicolon);
    SourceLocation b = tokenization_token_location(tokens, next);
    if (b.line != a.line + 1) {
        return false;
    }
    int32 cursor = last->offset + last->len;
    int32 end = first->offset;
    for (int32 i = semicolon + 1; i < next; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token->offset < cursor || token->offset >= end) {
            continue;
        }
        if (token->kind != TOKEN_COMMENT && !token_is_trivia(token)
            && !(macro && TOKEN_IS(token, "\\"))) {
            return false;
        }
        if (token->kind == TOKEN_COMMENT) {
            cursor = token->offset + token->len;
        }
    }
    int32 newlines = 0;
    for (int32 i = last->offset + last->len; i < end; i += 1) {
        if (tokens->text[i] == '\n') {
            newlines += 1;
        }
    }
    return newlines == 1;
}

static void
csane_bounds_at(CsaneFile *file, int32 first) {
    Tokenization *tokens = file->tokens;
    if (!csane_bounds_token(file, first, "ASSERT_GE")) {
        return;
    }
    CsaneTokenView view = csane_tokens_full(tokens);
    int32 open = csane_tokens_next(&view, first);
    if (!csane_bounds_token(file, open, "(")) {
        return;
    }
    int32 close = csane_tokens_match(&view, open);
    if (close < 0) {
        return;
    }
    TokenRange lower[2];
    if (!csane_bounds_call(file, (TokenRange){first, close + 1},
                           "ASSERT_GE", lower)
        || !csane_bounds_zero(file, lower[1])
        || !csane_bounds_reference(file, lower[0])) {
        return;
    }
    int32 semicolon = csane_tokens_next(&view, close);
    if (!csane_bounds_token(file, semicolon, ";")) {
        return;
    }
    int32 region = csane_regions_at_token(file->regions, first);
    if (region < 0) {
        return;
    }
    bool macro = csane_regions_enclosing(file->regions, region,
                                         CSANE_REGION_MACRO_REPLACEMENT) >= 0;
    int32 upper = csane_tokens_next(&view, semicolon);
    if (macro && csane_bounds_token(file, upper, "\\")) {
        upper = csane_tokens_next(&view, upper);
    }
    if (!csane_bounds_token(file, upper, "ASSERT_LT")
        || region != csane_regions_at_token(file->regions, upper)) {
        return;
    }
    if (!csane_bounds_adjacent(file, semicolon, upper, macro)) {
        return;
    }
    open = csane_tokens_next(&view, upper);
    if (!csane_bounds_token(file, open, "(")) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0) {
        return;
    }
    TokenRange upper_args[2];
    if (!csane_bounds_call(file, (TokenRange){upper, close + 1},
                           "ASSERT_LT", upper_args)
        || !csane_bounds_equal(file, lower[0], upper_args[0])
        || token_range_first_significant(tokens, upper_args[1])
           >= upper_args[1].end
        || !csane_bounds_token(file, csane_tokens_next(&view, close), ";")) {
        return;
    }
    csane_rule_issue(file, tokens->tokens[first].offset,
                     "STYLE-ASSERT-BOUNDS",
                     "use ASSERT_BETWEEN for adjacent bounds");
}

void
csane_bounds_check(CsaneFile *file) {
    if (file->is_ryu || file->regions == NULL) {
        return;
    }
    for (int32 i = 0; i < file->tokens->token_count; i += 1) {
        csane_bounds_at(file, i);
    }
}

#endif /* CSANECHECK_BOUNDS_C */
