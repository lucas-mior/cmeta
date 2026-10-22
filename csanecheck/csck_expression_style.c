// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_EXPRESSION_STYLE_C)
#define CSANECHECK_EXPRESSION_STYLE_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"
#include "csck_regions.h"

static int32
csane_style_first(CsaneFile *file, TokenRange range) {
    return token_range_first_significant(file->tokens, range);
}

/* Perl tests whether the argument starts with an unprefixed string
 * literal, not whether its complete expression is a string constant. */
static bool
csane_style_string_start(CsaneFile *file, TokenRange range) {
    int32 first = csane_style_first(file, range);
    Token *token;

    if (first >= range.end) {
        return false;
    }
    token = &file->tokens->tokens[first];
    return token->kind == TOKEN_LITERAL && token->len >= 2
           && token->text[0] == '"';
}

static bool
csane_style_decimal(CsaneFile *file, TokenRange range) {
    int32 first = csane_style_first(file, range);
    Token *token;

    if (first >= range.end
        || tokenization_next_significant(file->tokens, first) < range.end) {
        return false;
    }
    token = &file->tokens->tokens[first];
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

static bool
csane_style_lower_name(CsaneFile *file, TokenRange range) {
    int32 first = csane_style_first(file, range);
    Token *token;

    if (first >= range.end
        || tokenization_next_significant(file->tokens, first) < range.end) {
        return false;
    }
    token = &file->tokens->tokens[first];
    if (token->kind != TOKEN_IDENT || token->len == 0) {
        return false;
    }
    for (int32 i = 0; i < token->len; i += 1) {
        char c = token->text[i];
        if ((c < 'a' || c > 'z') && c != '_') {
            return false;
        }
    }
    return true;
}

/* Only one conversion, with the same grammar as Perl's format_spec.
 * The caller separately verifies that this token is the entire argument. */
static bool
csane_style_conversion(Token *token) {
    int32 i = 2;
    char c;

    if (token->kind != TOKEN_LITERAL || token->len < 4
        || token->text[0] != '"' || token->text[1] != '%'
        || token->text[token->len - 1] != '"') {
        return false;
    }
    while (i < token->len - 1) {
        char c = token->text[i];
        if (c != '-' && c != '+' && c != ' ' && c != '#'
            && c != '0') {
            break;
        }
        i += 1;
    }
    while (i < token->len - 1 && token->text[i] >= '0'
           && token->text[i] <= '9') {
        i += 1;
    }
    if (i < token->len - 1 && token->text[i] == '.') {
        i += 1;
        if (i < token->len - 1 && token->text[i] == '*') {
            i += 1;
        } else {
            int32 before = i;
            while (i < token->len - 1 && token->text[i] >= '0'
                   && token->text[i] <= '9') {
                i += 1;
            }
            if (before == i) {
                return false;
            }
        }
    }
    if (i + 2 < token->len && token->text[i] == 'h') {
        i += 1;
        if (token->text[i] == 'h') {
            i += 1;
        }
    } else if (i + 2 < token->len && token->text[i] == 'l') {
        i += 1;
        if (token->text[i] == 'l') {
            i += 1;
        }
    } else if (i + 2 < token->len) {
        char c = token->text[i];
        if (c == 'j' || c == 'z' || c == 't' || c == 'L') {
            i += 1;
        }
    }
    if (i + 2 != token->len) {
        return false;
    }
    c = token->text[i];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int32
csane_style_arguments(CsaneFile *file, int32 open, int32 close,
                      TokenRange *args, int32 capacity) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    if (csane_tokens_match(&view, open) != close) {
        return -1;
    }
    return csane_tokens_arguments(&view, open, args, capacity);
}

static int32
csane_style_strlit_argument(CsaneFile *file, TokenRange range) {
    int32 first = csane_style_first(file, range);
    int32 open;
    int32 literal;

    if (first >= range.end
        || !TOKEN_IS(&file->tokens->tokens[first], "STRLIT")) {
        return -1;
    }
    open = tokenization_next_significant(file->tokens, first);
    if (open >= range.end
        || !TOKEN_IS(&file->tokens->tokens[open], "(")) {
        return -1;
    }
    literal = tokenization_next_significant(file->tokens, open);
    if (literal >= range.end
        || !csane_style_string_start(file,
                                     (TokenRange){literal, range.end})) {
        return -1;
    }
    return first;
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
        /* The legacy idiom is ASSERT(lowercase_name OP decimal); with
         * no additional expression or parenthesized operand. */
        int32 first = csane_style_first(file, args[0]);
        int32 op = tokenization_next_significant(tokens, first);
        if (op < args[0].end
            && (TOKEN_IS(&tokens->tokens[op], ">")
                || TOKEN_IS(&tokens->tokens[op], ">=")
                || TOKEN_IS(&tokens->tokens[op], "<")
                || TOKEN_IS(&tokens->tokens[op], "<="))
            && csane_style_lower_name(file, (TokenRange){first, op})
            && csane_style_decimal(file, (TokenRange){op + 1,
                                                     args[0].end})) {
            CsaneTokenView view = csane_tokens_full(tokens);
            int32 semicolon = csane_tokens_next(&view, close);
            if (semicolon < tokens->token_count
                && TOKEN_IS(&tokens->tokens[semicolon], ";")) {
                csane_rule_issue(file, token->offset, "STYLE-ASSERT-REL",
                                 "use ASSERT_GE/GT/LE/LT for integer "
                                 "comparison");
            }
        }
    }
    if (TOKEN_IS(token, "STRLIT_LEN") && count >= 1
        && csane_style_string_start(file, args[0])) {
        csane_rule_issue(file, token->offset, "STYLE-STRLIT-LEN",
                         "STRLIT_LEN called on a literal string");
    }
    if (TOKEN_IS(token, "STREQUAL")) {
        for (int32 i = 0; i < count; i += 1) {
            int32 strlit = csane_style_strlit_argument(file, args[i]);
            if (strlit >= 0) {
                csane_rule_issue(file, tokens->tokens[args[i].first].offset,
                                 "STYLE-STREQUAL",
                                 "STREQUAL called with STRLIT literal");
                break;
            }
        }
    }
    if (TOKEN_IS(token, "str_printf") && count >= 3) {
        TokenRange format = args[1];
        int32 first = csane_style_first(file, format);
        if (first < format.end
            && tokenization_next_significant(tokens, first) >= format.end
            && csane_style_conversion(&tokens->tokens[first])
            && tokens->tokens[first].offset > 0
            && tokens->tokens[first].offset + tokens->tokens[first].len
               < tokens->text_len
            && tokens->text[tokens->tokens[first].offset - 1] == ' '
            && tokens->text[tokens->tokens[first].offset
                            + tokens->tokens[first].len] == ',') {
            csane_rule_issue(file, tokens->tokens[format.first].offset,
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
        int32 next;
        int32 previous;
        CsaneTokenView local;
        CsaneRegions *r;
        int32 region;
        enum CsaneRegionKind region_kind;
        int32 replacement;
        int32 branch;
        int32 close;
        TokenRange range;
        enum CsaneLocalKind kind;

        if (token->kind != TOKEN_IDENT
            || (!TOKEN_IS(token, "ASSERT")
                && !TOKEN_IS(token, "STRLIT_LEN")
                && !TOKEN_IS(token, "STREQUAL")
                && !TOKEN_IS(token, "str_printf"))) {
            continue;
        }
        next = csane_tokens_next(&view, i);
        if (next >= tokens->token_count
            || !TOKEN_IS(&tokens->tokens[next], "(")) {
            continue;
        }
        previous = csane_tokens_previous(&view, i);
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
        /* A replacement list or conditional branch must not inherit
         * unmatched delimiters from unrelated preprocessing alternatives.
         * For ordinary code use the full view when its enclosing region
         * cannot contain the entire invocation. */
        local = view;
        r = file->regions;
        region = csane_regions_at_token(r, i);
        region_kind = CSANE_REGION_MACRO_REPLACEMENT;
        replacement = csane_regions_enclosing(r, region, region_kind);
        region_kind = CSANE_REGION_CONDITIONAL_BRANCH;
        branch = csane_regions_enclosing(r, region, region_kind);
        if (replacement >= 0) {
            if (!csane_tokens_view(&local, tokens,
                                   file->regions->items[replacement].tokens)) {
                continue;
            }
        } else {
            if (branch >= 0) {
                CsaneTokenView candidate;
                if (csane_tokens_view(&candidate, tokens,
                                      file->regions->items[branch].tokens)
                    && csane_tokens_match(&candidate, next) >= 0) {
                    local = candidate;
                }
            }
            /* Keep the legacy safeguard for malformed ordinary code.
             * Inside a conditional alternative, an incomplete surrounding
             * block does not invalidate an independently matched call. */
            if (branch < 0) {
                bool unclosed = false;
                for (int32 j = i - 1; j >= local.range.first; j -= 1) {
                    if (TOKEN_IS(&tokens->tokens[j], "{")
                        && csane_tokens_match(&local, j) < 0) {
                        unclosed = true;
                        break;
                    }
                }
                if (unclosed) {
                    continue;
                }
            }
        }
        close = csane_tokens_match(&local, next);
        if (close < 0) {
            continue;
        }
        range = (TokenRange){0, tokens->token_count};
        kind = csane_classify_invocation(tokens, range, i);
        if (kind == CSANE_LOCAL_CALL
            || kind == CSANE_LOCAL_MACRO_INVOCATION) {
            csane_style_one(file, i, next, close);
        }
    }
}

#endif /* CSANECHECK_EXPRESSION_STYLE_C */
