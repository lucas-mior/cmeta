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
        c = token->text[i];
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
        c = token->text[i];
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
                csane_rule_issue(file, token->offset,
                                 STRLIT("STYLE-ASSERT-REL"),
                                 "use ASSERT_GE/GT/LE/LT for integer "
                                 "comparison");
            }
        }
    }
    if (TOKEN_IS(token, "ASSERT_CLOSE") && count == 3) {
        int32 tolerance = csane_style_first(file, args[2]);
        if (tolerance < args[2].end
            && TOKEN_IS(&tokens->tokens[tolerance], "0.0")
            && tokenization_next_significant(tokens, tolerance)
               >= args[2].end) {
            csane_rule_issue(file, token->offset,
                             STRLIT("STYLE-ASSERT-CLOSE-ZERO"),
                             "use ASSERT_EQUAL instead of ASSERT_CLOSE "
                             "with zero tolerance");
        }
    }
    if (TOKEN_IS(token, "STRLIT_LEN") && count >= 1
        && csane_style_string_start(file, args[0])) {
        csane_rule_issue(file, token->offset, STRLIT("STYLE-STRLIT-LEN"),
                         "STRLIT_LEN called on a literal string");
    }
    if (TOKEN_IS(token, "STREQUAL")) {
        for (int32 i = 0; i < count; i += 1) {
            int32 strlit = csane_style_strlit_argument(file, args[i]);
            if (strlit >= 0) {
                csane_rule_issue(file, tokens->tokens[args[i].first].offset,
                                 STRLIT("STYLE-STREQUAL"),
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
                             STRLIT("STYLE-PRINTF"),
                             "format contains only one conversion");
        }
    }
}

/* Accept plain C integer tokens, including octal/hex/binary forms and
 * the standard U/L/LL suffixes. Decimal and hexadecimal floating-point
 * literals must not be treated as integer arguments. */
static bool
csane_style_integer_literal(Token *token) {
    int32 radix = 10;
    int32 i = 0;
    int32 digits = 0;
    int32 unsigned_count = 0;
    int32 long_count = 0;
    char long_case = 0;

    if (token->kind != TOKEN_LITERAL || token->len == 0
        || token->text[0] < '0' || token->text[0] > '9') {
        return false;
    }
    if (token->text[0] == '0' && token->len >= 2) {
        if (token->text[1] == 'x' || token->text[1] == 'X') {
            radix = 16;
            i = 2;
        } else if (token->text[1] == 'b' || token->text[1] == 'B') {
            radix = 2;
            i = 2;
        } else {
            radix = 8;
        }
    }
    for (; i < token->len; i += 1) {
        char c = token->text[i];
        int32 digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (radix == 16 && c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (radix == 16 && c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        if (digit < 0 || digit >= radix) {
            break;
        }
        digits += 1;
    }
    if (digits == 0) {
        return false;
    }
    for (; i < token->len; i += 1) {
        char c = token->text[i];
        if (c == 'u' || c == 'U') {
            unsigned_count += 1;
            if (unsigned_count > 1) {
                return false;
            }
        } else if (c == 'l' || c == 'L') {
            long_count += 1;
            if (long_count > 2
                || (long_case != 0 && c != long_case)) {
                return false;
            }
            long_case = c;
        } else {
            return false;
        }
    }
    return true;
}

/* Return the integer literal when the complete expression consists only
 * of a possibly signed/parenthesized integer constant. */
static int32
csane_style_integer_operand(CsaneTokenView *view, TokenRange range) {
    CsaneTokenView local;
    Tokenization *tokens = view->tokens;
    int32 literal;

    range = csane_tokens_unwrap(view, range);
    if (!csane_tokens_subview(&local, view, range)) {
        return -1;
    }
    literal = csane_tokens_next(&local, range.first - 1);
    if (literal < range.end
        && (TOKEN_IS(&tokens->tokens[literal], "+")
            || TOKEN_IS(&tokens->tokens[literal], "-"))) {
        literal = csane_tokens_next(&local, literal);
    }
    if (literal >= range.end
        || !csane_style_integer_literal(&tokens->tokens[literal])
        || csane_tokens_next(&local, literal) < range.end) {
        return -1;
    }
    return literal;
}

/* A cast is redundant for ordinary call arguments only when int32/int64
 * casts a single integer literal. MAXOF/MINOF reject any integer-literal
 * cast, including other numeric types and typedefs. */
static int32
csane_style_literal_cast(CsaneTokenView *view, TokenRange range,
                         bool restrict_type) {
    CsaneTokenView local;
    Tokenization *tokens = view->tokens;
    int32 open;
    int32 type;
    int32 after_type;
    int32 close;

    range = csane_tokens_unwrap(view, range);
    if (!csane_tokens_subview(&local, view, range)) {
        return -1;
    }
    open = csane_tokens_next(&local, range.first - 1);
    if (open >= range.end || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return -1;
    }
    type = csane_tokens_next(&local, open);
    if (type >= range.end || tokens->tokens[type].kind != TOKEN_IDENT) {
        return -1;
    }
    if (restrict_type
        && !TOKEN_IS(&tokens->tokens[type], "int32")
        && !TOKEN_IS(&tokens->tokens[type], "int64")) {
        return -1;
    }
    after_type = csane_tokens_next(&local, type);
    if (!restrict_type) {
        while (after_type < range.end
               && tokens->tokens[after_type].kind == TOKEN_IDENT) {
            after_type = csane_tokens_next(&local, after_type);
        }
    }
    close = after_type;
    if (close >= range.end || !TOKEN_IS(&tokens->tokens[close], ")")
        || csane_tokens_match(&local, open) != close) {
        return -1;
    }
    if (csane_style_integer_operand(&local,
                                    (TokenRange){close + 1, range.end}) < 0) {
        return -1;
    }
    return open;
}

/* Literal arguments defeat the purpose of type-derived limits. A typed
 * constant already has an explicitly chosen type; use its named bound. */
static void
csane_style_minmax_literal(CsaneFile *file, CsaneTokenView *view,
                           int32 name, int32 open) {
    TokenRange args[2];
    int32 count = csane_tokens_arguments(view, open, args, 2);
    int32 literal;

    if (count != 1) {
        return;
    }
    literal = csane_style_integer_operand(view, args[0]);
    if (literal < 0) {
        literal = csane_style_literal_cast(view, args[0], false);
    }
    if (literal >= 0) {
        csane_rule_issue(file, file->tokens->tokens[name].offset,
                         STRLIT("STYLE-MINMAX-LITERAL"),
                         "use a named type limit instead of MAXOF/MINOF "
                         "on an integer literal");
    }
}

static void
csane_style_literal_cast_arguments(CsaneFile *file, CsaneTokenView *view,
                                   int32 open) {
    TokenRange args[256];
    int32 count = csane_tokens_arguments(view, open, args, 256);
    if (count < 0) {
        return;
    }
    for (int32 i = 0; i < count; i += 1) {
        int32 cast = csane_style_literal_cast(view, args[i], true);
        if (cast >= 0) {
            csane_rule_issue(file, file->tokens->tokens[cast].offset,
                             STRLIT("STYLE-LITERAL-INT-CAST"),
                             "pass integer literals without int32/int64 casts");
        }
    }
}

static bool
csane_style_call_keyword(Token *token) {
    return TOKEN_IS(token, "if") || TOKEN_IS(token, "for")
           || TOKEN_IS(token, "while") || TOKEN_IS(token, "switch")
           || TOKEN_IS(token, "sizeof") || TOKEN_IS(token, "_Alignof")
           || TOKEN_IS(token, "_Generic")
           || TOKEN_IS(token, "_Static_assert")
           || TOKEN_IS(token, "typeof")
           || TOKEN_IS(token, "__typeof__");
}

/* A cast immediately before a rounding call discards the checked
 * conversion provided by the project's int64 rounding wrappers. */
static void
csane_style_round64(CsaneFile *file, CsaneTokenView *view, int32 name) {
    Tokenization *tokens = file->tokens;
    int32 cast_close = csane_tokens_previous(view, name);
    int32 type;
    int32 cast_open;

    if (cast_close < view->range.first
        || !TOKEN_IS(&tokens->tokens[cast_close], ")")) {
        return;
    }
    type = csane_tokens_previous(view, cast_close);
    if (type < view->range.first
        || !TOKEN_IS(&tokens->tokens[type], "int64")) {
        return;
    }
    cast_open = csane_tokens_previous(view, type);
    if (cast_open < view->range.first
        || !TOKEN_IS(&tokens->tokens[cast_open], "(")
        || csane_tokens_match(view, cast_open) != cast_close) {
        return;
    }
    if (TOKEN_IS(&tokens->tokens[name], "floor")) {
        csane_rule_issue(file, tokens->tokens[cast_open].offset,
                         STRLIT("STYLE-ROUND64"),
                         "use floor64(x) instead of (int64)floor(x)");
    } else if (TOKEN_IS(&tokens->tokens[name], "ceil")) {
        csane_rule_issue(file, tokens->tokens[cast_open].offset,
                         STRLIT("STYLE-ROUND64"),
                         "use ceil64(x) instead of (int64)ceil(x)");
    } else {
        csane_rule_issue(file, tokens->tokens[cast_open].offset,
                         STRLIT("STYLE-ROUND64"),
                         "use round64(x) instead of (int64)round(x)");
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
            || csane_style_call_keyword(token)) {
            continue;
        }
        next = csane_tokens_next(&view, i);
        if (next >= tokens->token_count
            || !TOKEN_IS(&tokens->tokens[next], "(")) {
            continue;
        }
        previous = csane_tokens_previous(&view, i);
        if (previous >= 0
            && csane_type_name(&tokens->tokens[previous])) {
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
            /* Keep the existing malformed-block safeguard for the older
             * rules. Do not scan all earlier braces for each new call:
             * that would make whole-file checking quadratic. Conditional
             * alternatives still use their independently matched calls. */
            if (branch < 0
                && (TOKEN_IS(token, "ASSERT")
                    || TOKEN_IS(token, "ASSERT_CLOSE")
                    || TOKEN_IS(token, "STRLIT_LEN")
                    || TOKEN_IS(token, "STREQUAL")
                    || TOKEN_IS(token, "str_printf")
                    || TOKEN_IS(token, "floor")
                    || TOKEN_IS(token, "ceil")
                    || TOKEN_IS(token, "round"))) {
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
            if (TOKEN_IS(token, "MAXOF") || TOKEN_IS(token, "MINOF")) {
                csane_style_minmax_literal(file, &local, i, next);
            } else {
                csane_style_literal_cast_arguments(file, &local, next);
            }
            if (previous >= 0
                && (TOKEN_IS(&tokens->tokens[previous], ".")
                    || TOKEN_IS(&tokens->tokens[previous], "->"))) {
                continue;
            }
            if (TOKEN_IS(token, "floor") || TOKEN_IS(token, "ceil")
                || TOKEN_IS(token, "round")) {
                csane_style_round64(file, &local, i);
            } else if (TOKEN_IS(token, "ASSERT")
                       || TOKEN_IS(token, "ASSERT_CLOSE")
                       || TOKEN_IS(token, "STRLIT_LEN")
                       || TOKEN_IS(token, "STREQUAL")
                       || TOKEN_IS(token, "str_printf")) {
                csane_style_one(file, i, next, close);
            }
        }
    }
}

#endif /* CSANECHECK_EXPRESSION_STYLE_C */
