// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_BOUNDS_C)
#define CSANECHECK_BOUNDS_C

#include "cbase.h"
#include "csanecheck.h"
#include "csanecheck_tokens.h"

static TokenRange
csane_bounds_unwrap(CsaneFile *file, TokenRange range) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    while (range.first < range.end) {
        int32 first = token_range_first_significant(file->tokens, range);
        int32 last = token_range_last_significant(file->tokens, range);
        if (first >= range.end || !TOKEN_IS(&file->tokens->tokens[first], "(")
            || csane_tokens_match(&view, first) != last) {
            break;
        }
        range.first = first + 1;
        range.end = last;
    }
    return range;
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
    TokenRange content = {open + 1, close};
    if (!csane_tokens_view(&view, file->tokens, content)) {
        return false;
    }
    return csane_tokens_split(&view, ",", 1, args, 2) == 2;
}

static void
csane_bounds_pair(CsaneFile *file, CsaneStatement *a, CsaneStatement *b) {
    if (a->kind != CSANE_STMT_SIMPLE || b->kind != CSANE_STMT_SIMPLE) {
        return;
    }
    int32 aend = token_range_last_significant(file->tokens, a->tokens);
    int32 bend = token_range_last_significant(file->tokens, b->tokens);
    if (!csane_bounds_token(file, aend, ";")
        || !csane_bounds_token(file, bend, ";")) {
        return;
    }
    int32 bfirst = token_range_first_significant(file->tokens, b->tokens);
    SourceLocation end = tokenization_token_location(file->tokens, aend);
    SourceLocation next = tokenization_token_location(file->tokens, bfirst);
    if (next.line != end.line + 1) {
        return;
    }
    TokenRange arange = {a->tokens.first, aend};
    TokenRange brange = {b->tokens.first, bend};
    TokenRange lower[2];
    TokenRange upper[2];
    if (!csane_bounds_call(file, arange, "ASSERT_GE", lower)
        || !csane_bounds_call(file, brange, "ASSERT_LT", upper)
        || !csane_bounds_zero(file, lower[1])
        || !csane_bounds_reference(file, lower[0])
        || !csane_bounds_equal(file, lower[0], upper[0])) {
        return;
    }
    int32 first = token_range_first_significant(file->tokens, a->tokens);
    csane_rule_issue(file, file->tokens->tokens[first].offset,
                     "STYLE-ASSERT-BOUNDS",
                     "use ASSERT_BETWEEN for adjacent bounds");
}

void
csane_bounds_check(CsaneFile *file) {
    if (file->is_ryu || file->statements == NULL) {
        return;
    }
    CsaneStatements *statements = file->statements;
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *a = &statements->items[i];
        for (int32 j = i + 1; j < statements->count; j += 1) {
            CsaneStatement *b = &statements->items[j];

            if (a->function != b->function
                || a->owner_region != b->owner_region
                || a->parent != b->parent) {
                continue;
            }
            if (a->tokens.end > b->tokens.first) {
                continue;
            }
            bool separated = false;
            for (int32 k = a->source.end; k < b->source.start; k += 1) {
                char c = file->tokens->text[k];
                if (c != ' ' && c != '\t' && c != '\r'
                    && c != '\n') {
                    separated = true;
                    break;
                }
            }
            if (!separated) {
                csane_bounds_pair(file, a, b);
            }
            break;
        }
    }
}

#endif /* CSANECHECK_BOUNDS_C */
