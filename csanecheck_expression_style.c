// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_EXPRESSION_STYLE_C)
#define CSANECHECK_EXPRESSION_STYLE_C

#include "cbase.h"
#include "csanecheck.h"
#include "csanecheck_tokens.h"
#include "csanecheck_regions.h"

static int32
csane_style_first(CsaneFile *file, TokenRange range) {
    return token_range_first_significant(file->tokens, range);
}

static TokenRange
csane_style_unwrap(CsaneFile *file, TokenRange range) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    while (range.first < range.end) {
        int32 first = csane_style_first(file, range);
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
csane_style_string(CsaneFile *file, TokenRange range) {
    range = csane_style_unwrap(file, range);
    int32 first = csane_style_first(file, range);
    if (first >= range.end) {
        return false;
    }
    Token *token = &file->tokens->tokens[first];
    int32 quote = literal_quote_index(token->text, token->len, 0);
    return token->kind == TOKEN_LITERAL && quote >= 0
           && token->text[quote] == '"'
           && tokenization_next_significant(file->tokens, first) >= range.end;
}

static bool
csane_style_integer(CsaneFile *file, TokenRange range) {
    range = csane_style_unwrap(file, range);
    int32 first = csane_style_first(file, range);
    if (first >= range.end
        || tokenization_next_significant(file->tokens, first) < range.end) {
        return false;
    }
    Token *token = &file->tokens->tokens[first];
    if (token->kind != TOKEN_LITERAL || token->len == 0) {
        return false;
    }
    for (int32 i = 0; i < token->len; i += 1) {
        if (token->text[i] < '0' || token->text[i] > '9') {
            return false;
        }
    }
    return true;
}

static int32
csane_style_arguments(CsaneFile *file, int32 open, int32 close,
                      TokenRange *args, int32 capacity) {
    TokenRange content = {open + 1, close};
    if (csane_style_first(file, content) >= close) {
        return 0;
    }
    CsaneTokenView view;
    if (!csane_tokens_view(&view, file->tokens, content)) {
        return -1;
    }
    return csane_tokens_split(&view, ",", 1, args, capacity);
}

static bool
csane_style_named_call(CsaneFile *file, TokenRange range, char *name,
                       TokenRange *args, int32 capacity, int32 *count) {
    range = csane_style_unwrap(file, range);
    int32 first = csane_style_first(file, range);
    if (first >= range.end || !TOKEN_IS(&file->tokens->tokens[first], name)) {
        return false;
    }
    int32 open = tokenization_next_significant(file->tokens, first);
    CsaneTokenView view = csane_tokens_full(file->tokens);
    if (open >= range.end || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
        return false;
    }
    int32 close = csane_tokens_match(&view, open);
    if (close < 0 || close >= range.end
        || tokenization_next_significant(file->tokens, close) < range.end) {
        return false;
    }
    *count = csane_style_arguments(file, open, close, args, capacity);
    return *count >= 0;
}

static void
csane_style_one(CsaneFile *file, int32 name, int32 open, int32 close) {
    Tokenization *tokens = file->tokens;
    Token *token = &tokens->tokens[name];
    TokenRange args[256];
    int32 count = csane_style_arguments(file, open, close, args, 256);
    if (count < 0) {
        return;
    }
    if (TOKEN_IS(token, "ASSERT") && count == 1) {
        TokenRange condition = csane_style_unwrap(file, args[0]);
        CsaneTokenView view;
        if (!csane_tokens_view(&view, tokens, condition)) {
            return;
        }
        for (int32 i = condition.first; i < condition.end; i += 1) {
            Token *op = &tokens->tokens[i];
            if (!TOKEN_IS(op, ">") && !TOKEN_IS(op, ">=")
                && !TOKEN_IS(op, "<") && !TOKEN_IS(op, "<=")) {
                continue;
            }
            TokenRange lhs = {condition.first, i};
            TokenRange rhs = {i + 1, condition.end};
            lhs = csane_style_unwrap(file, lhs);
            int32 first = csane_style_first(file, lhs);
            if (first < lhs.end && tokens->tokens[first].kind == TOKEN_IDENT
                && tokenization_next_significant(tokens, first) >= lhs.end
                && csane_style_integer(file, rhs)) {
                csane_rule_issue(file, token->offset,
                                 "STYLE-ASSERT-REL",
                                 "use ASSERT_GE/GT/LE/LT for integer "
                                 "comparison");
                break;
            }
        }
    }
    if (TOKEN_IS(token, "STRLIT_LEN") && count == 1
        && csane_style_string(file, args[0])) {
        csane_rule_issue(file, token->offset, "STYLE-STRLIT-LEN",
                         "STRLIT_LEN called on a literal string");
    }
    if (TOKEN_IS(token, "STREQUAL")) {
        for (int32 i = 0; i < count; i += 1) {
            TokenRange nested[2];
            int32 nested_count = 0;
            if (csane_style_named_call(file, args[i], "STRLIT", nested, 2,
                                       &nested_count)
                && nested_count == 1 && csane_style_string(file, nested[0])) {
                csane_rule_issue(file, token->offset, "STYLE-STREQUAL",
                                 "STREQUAL called with STRLIT literal");
                break;
            }
        }
    }
    if (TOKEN_IS(token, "str_printf") && count >= 3
        && csane_style_string(file, args[1])) {
        TokenRange format = csane_style_unwrap(file, args[1]);
        int32 first = csane_style_first(file, format);
        if (first < format.end
            && csane_format_conversion(&tokens->tokens[first])) {
            csane_rule_issue(file, tokens->tokens[first].offset,
                             "STYLE-PRINTF",
                             "format contains only one conversion");
        }
    }
}

void
csane_expression_style_check(CsaneFile *file) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    Tokenization *tokens = file->tokens;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token->kind != TOKEN_IDENT
            || (!TOKEN_IS(token, "ASSERT")
                && !TOKEN_IS(token, "STRLIT_LEN")
                && !TOKEN_IS(token, "STREQUAL")
                && !TOKEN_IS(token, "str_printf"))) {
            continue;
        }
        int32 next = csane_tokens_next(&view, i);
        if (next >= tokens->token_count
            || !TOKEN_IS(&tokens->tokens[next], "(")) {
            continue;
        }
        int32 previous = csane_tokens_previous(&view, i);
        if (previous >= 0
            && (TOKEN_IS(&tokens->tokens[previous], ".")
                || TOKEN_IS(&tokens->tokens[previous], "->")
                || csane_type_name(&tokens->tokens[previous]))) {
            continue;
        }
        if (file->preprocessor->token_roles != NULL
            && file->preprocessor->token_roles[i] != C_PP_TOKEN_CODE
            && file->preprocessor->token_roles[i]
               != C_PP_TOKEN_MACRO_REPLACEMENT) {
            continue;
        }
        /* Macro replacements are separate source regions. Never allow an
         * unrelated declaration, conditional branch or macro to influence
         * their delimiter matching. Ordinary code retains the legacy view
         * until the remaining callers migrate to region-local navigation. */
        CsaneTokenView local = view;
        int32 region = csane_regions_at_token(file->regions, i);
        if (region >= 0 && file->regions->items[region].kind
                           == CSANE_REGION_MACRO_REPLACEMENT) {
            if (!csane_tokens_view(&local, tokens,
                                   file->regions->items[region].tokens)) {
                continue;
            }
        } else {
            /* Preserve conservative behavior for incomplete code. */
            bool unclosed = false;
            for (int32 j = i - 1; j >= 0; j -= 1) {
                if (TOKEN_IS(&tokens->tokens[j], "{")
                    && csane_tokens_match(&view, j) < 0) {
                    unclosed = true;
                    break;
                }
            }
            if (unclosed) {
                continue;
            }
        }
        int32 close = csane_tokens_match(&local, next);
        if (close < 0) {
            continue;
        }
        TokenRange range = {0, tokens->token_count};
        enum CsaneLocalKind kind = csane_classify_invocation(tokens, range, i);
        if (kind == CSANE_LOCAL_CALL
            || kind == CSANE_LOCAL_MACRO_INVOCATION) {
            csane_style_one(file, i, next, close);
        }
    }
}

#endif /* CSANECHECK_EXPRESSION_STYLE_C */
