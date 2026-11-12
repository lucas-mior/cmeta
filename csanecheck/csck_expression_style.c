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

/* Recognize an argument consisting solely of string literals, possibly
 * concatenated or wrapped in redundant parentheses. */
static bool
csane_style_literal_argument(CsaneFile *file, TokenRange range) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    CsaneTokenView argument;
    int32 index;
    bool found = false;

    range = csane_tokens_unwrap(&view, range);
    if (!csane_tokens_subview(&argument, &view, range)) {
        return false;
    }
    index = csane_tokens_next(&argument, range.first - 1);
    while (index < range.end) {
        Token *token = &file->tokens->tokens[index];
        int32 quote;

        if (c_preprocessor_token_is_line_splice(file->tokens, index)) {
            index = csane_tokens_next(&argument, index);
            continue;
        }
        if (token->kind != TOKEN_LITERAL) {
            return false;
        }
        quote = literal_quote_index(token->text, token->len, 0);
        if (quote < 0 || token->text[quote] != '"') {
            return false;
        }
        found = true;
        index = csane_tokens_next(&argument, index);
    }
    return found;
}

/* Only one conversion, with the same grammar as Perl's format_spec.
 * The caller separately verifies that this token is the entire argument. */

static int32
csane_style_strlit_argument(CsaneFile *file, TokenRange range) {
    int32 first = csane_style_first(file, range);
    int32 open;
    int32 literal;
    Token *token;

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
    if (literal >= range.end) {
        return -1;
    }
    token = &file->tokens->tokens[literal];
    if (token->kind != TOKEN_LITERAL || token->len < 2
        || token->text[0] != '"') {
        return -1;
    }
    return first;
}

/* Accept only a complete member expression, or a member's indexed item.
 * Other operations (including calls and pointer arithmetic) may change what
 * the expression measures, so they are not evidence for a stored length. */
static bool
csane_style_strlen32_member(CsaneFile *file, CsaneTokenView *view,
                            int32 open, bool *indexed) {
    TokenRange args[2];
    TokenRange range;
    CsaneTokenView argument;
    Tokenization *tokens = file->tokens;
    int32 i;
    bool member = false;
    bool item = false;

    if (csane_tokens_arguments(view, open, args, 2) != 1) {
        return false;
    }
    range = csane_tokens_unwrap(view, args[0]);
    if (!csane_tokens_subview(&argument, view, range)) {
        return false;
    }
    i = csane_tokens_next(&argument, range.first - 1);
    if (i >= range.end || tokens->tokens[i].kind != TOKEN_IDENT) {
        return false;
    }
    i = csane_tokens_next(&argument, i);
    while (i < range.end) {
        Token *token = &tokens->tokens[i];
        if (TOKEN_IS(token, ".") || TOKEN_IS(token, "->")) {
            i = csane_tokens_next(&argument, i);
            if (i >= range.end || tokens->tokens[i].kind != TOKEN_IDENT) {
                return false;
            }
            member = true;
            item = false;
            i = csane_tokens_next(&argument, i);
        } else if (TOKEN_IS(token, "[")) {
            int32 close = csane_tokens_match(&argument, i);
            if (close < 0
                || csane_tokens_next(&argument, i) == close) {
                return false;
            }
            item = member;
            member = false;
            i = csane_tokens_next(&argument, close);
        } else {
            return false;
        }
    }
    *indexed = item;
    return member || item;
}

/* Assigning the measured length into the matching parallel _lens array
 * initializes the stored length. Both owner paths and indices must agree. */
static bool
csane_style_strlen32_length_assignment(CsaneFile *file,
                                       CsaneTokenView *view, int32 name,
                                       int32 open, int32 close) {
    TokenRange args[1];
    TokenRange operand;
    Tokenization *tokens = file->tokens;
    CsaneTokenView source;
    Token *length_token = NULL;
    Token *string_token = NULL;
    int32 array_open;
    int32 field;
    int32 left;
    int32 right;
    int32 before;
    int32 after;
    int32 equal;
    int32 start = name;
    int32 end = close;

    if (csane_tokens_arguments(view, open, args, 1) != 1) {
        return false;
    }
    operand = csane_tokens_unwrap(view, args[0]);
    if (!csane_tokens_subview(&source, view, operand)) {
        return false;
    }
    right = csane_tokens_previous(&source, operand.end);
    if (right < operand.first || !TOKEN_IS(&tokens->tokens[right], "]")) {
        return false;
    }
    array_open = csane_tokens_match(&source, right);
    if (array_open < 0) {
        return false;
    }
    field = csane_tokens_previous(&source, array_open);
    if (field < operand.first
        || tokens->tokens[field].kind != TOKEN_IDENT) {
        return false;
    }
    for (;;) {
        before = csane_tokens_previous(view, start);
        after = csane_tokens_next(view, end);
        if (before < view->range.first || after >= view->range.end
            || !TOKEN_IS(&tokens->tokens[before], "(")
            || !TOKEN_IS(&tokens->tokens[after], ")")
            || csane_tokens_match(view, before) != after) {
            break;
        }
        start = before;
        end = after;
    }
    equal = csane_tokens_previous(view, start);
    if (equal < view->range.first
        || !TOKEN_IS(&tokens->tokens[equal], "=")) {
        return false;
    }
    after = csane_tokens_next(view, end);
    if (after < view->range.end
        && !TOKEN_IS(&tokens->tokens[after], ";")
        && !TOKEN_IS(&tokens->tokens[after], ",")
        && !TOKEN_IS(&tokens->tokens[after], ")")) {
        return false;
    }
    left = csane_tokens_previous(view, equal);
    while (right >= operand.first) {
        if (left < view->range.first) {
            return false;
        }
        length_token = &tokens->tokens[left];
        string_token = &tokens->tokens[right];
        if (right == field) {
            if (length_token->kind != TOKEN_IDENT
                || length_token->len != string_token->len + 5
                || memcmp64(length_token->text, string_token->text,
                            string_token->len)
                || !ENDS_WITH(length_token->text, length_token->len, "_lens")) {
                return false;
            }
        } else if (length_token->kind != string_token->kind
                   || length_token->len != string_token->len
                   || memcmp64(length_token->text, string_token->text,
                               length_token->len)) {
            return false;
        }
        left = csane_tokens_previous(view, left);
        right = csane_tokens_previous(&source, right);
    }
    return true;
}

/* Is this argument exactly strlen32(member), possibly parenthesized? */
static int32
csane_style_strequal_strlen32(CsaneFile *file, CsaneTokenView *view,
                              TokenRange range) {
    Tokenization *tokens = file->tokens;
    bool indexed = false;
    int32 name;
    int32 open;
    int32 close;

    range = csane_tokens_unwrap(view, range);
    name = csane_style_first(file, range);
    if (name >= range.end
        || !TOKEN_IS(&tokens->tokens[name], "strlen32")) {
        return -1;
    }
    open = csane_tokens_next(view, name);
    if (open >= range.end
        || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return -1;
    }
    close = csane_tokens_match(view, open);
    if (close < 0 || close >= range.end
        || csane_tokens_next(view, close) < range.end) {
        return -1;
    }
    if (csane_style_strlen32_member(file, view, open, &indexed)
        && !indexed) {
        return name;
    }
    return -1;
}

static void
csane_style_one(CsaneFile *file, CsaneTokenView *view,
                int32 name, int32 open, int32 close) {
    int32 count;
    Tokenization *tokens = file->tokens;
    Token *token = &tokens->tokens[name];
    TokenRange args[256];
    int32 in_result_arguments;
    do {
        int32 in_capacity = 256;

        if (csane_tokens_match(view, open) != close) {
            in_result_arguments = -1;
            break;
        }
        in_result_arguments = csane_tokens_arguments(view, open,
                                                     args, in_capacity);
    } while (false);
    count = in_result_arguments;
    if (count < 0) {
        return;
    }
    if (TOKEN_IS(token, "ASSERT_CLOSE") && count == 3) {
        int32 tolerance = csane_style_first(file, args[2]);
        if (tolerance < args[2].end
            && TOKEN_IS(&tokens->tokens[tolerance], "0.0")
            && tokenization_next_significant(tokens, tolerance)
               >= args[2].end) {
            csane_rule_issue(file, token->offset,
                             STRLIT("STYLE-ASSERT-CLOSE-ZERO"),
                             "use ASSERT_EQ instead of ASSERT_CLOSE "
                             "with zero tolerance");
        }
    }
    if (TOKEN_IS(token, "STRLIT_LEN") && count == 1
        && csane_style_literal_argument(file, args[0])) {
        csane_rule_issue(file, token->offset, STRLIT("STYLE-STRLIT-LEN"),
                         "do not call STRLIT_LEN on a string literal");
    }
    if (TOKEN_IS(token, "SIZEOF") && count == 1
        && csane_style_literal_argument(file, args[0])) {
        csane_rule_issue(file, token->offset, STRLIT("STYLE-SIZEOF-LITERAL"),
                         "do not call SIZEOF on a string literal");
    }
    if (TOKEN_IS(token, "LENGTH") && count == 1
        && csane_style_literal_argument(file, args[0])) {
        csane_rule_issue(file, token->offset, STRLIT("STYLE-LENGTH-LITERAL"),
                         "do not call LENGTH on a string literal");
    }
    if (TOKEN_IS(token, "STREQUAL")
        || TOKEN_IS(token, "BEGINS_WITH")
        || TOKEN_IS(token, "ENDS_WITH")) {
        /* The three-argument form computes the last literal's length.
         * The first string must not carry a handwritten length either. */
        if ((count == 3 || count == 4)
            && csane_style_literal_argument(file, args[0])) {
            csane_rule_issue(file, tokens->tokens[args[0].first].offset,
                             STRLIT("STYLE-LITERAL-LENGTH"),
                             "do not pass a string literal with an explicit "
                             "length; use the literal-aware macro form");
        }
        if (count == 4 && csane_style_literal_argument(file, args[2])) {
            csane_rule_issue(file, tokens->tokens[args[2].first].offset,
                             STRLIT("STYLE-LITERAL-LENGTH"),
                             "do not specify the length of a string literal; "
                             "use the three-argument macro form");
        }
    }
    if (TOKEN_IS(token, "STREQUAL") && (count == 3 || count == 4)) {
        for (int32 i = 0; i < count; i += 1) {
            int32 strlen_name =
                csane_style_strequal_strlen32(file, view, args[i]);
            if (strlen_name >= 0) {
                csane_rule_issue(file, tokens->tokens[strlen_name].offset,
                                 STRLIT("STYLE-STRLEN32-REDUNDANT"),
                                 "STREQUAL length for a struct member "
                                 "must use the stored length");
            }
        }
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
        bool _i29_predicate;
        _i29_predicate = first < format.end;
        if (_i29_predicate) {
            _i29_predicate = tokenization_next_significant(tokens,
                                                           first) >= format.end;
        }
        if (_i29_predicate) {
            bool _i29_result;
            do {
                Token *_i29_token = &tokens->tokens[first];

                int32 i = 2;
                char c;

                if (_i29_token->kind != TOKEN_LITERAL || _i29_token->len < 4
                    || _i29_token->text[0] != '"' || _i29_token->text[1] != '%'
                    || _i29_token->text[_i29_token->len - 1] != '"') {
                    _i29_result = false;
                    goto _i29_done;
                }
                while (i < _i29_token->len - 1) {
                    c = _i29_token->text[i];
                    if (c != '-' && c != '+' && c != ' ' && c != '#'
                        && c != '0') {
                        break;
                    }
                    i += 1;
                }
                while (i < _i29_token->len - 1 && _i29_token->text[i] >= '0'
                       && _i29_token->text[i] <= '9') {
                    i += 1;
                }
                if (i < _i29_token->len - 1 && _i29_token->text[i] == '.') {
                    i += 1;
                    if (i < _i29_token->len - 1 && _i29_token->text[i] == '*') {
                        i += 1;
                    } else {
                        int32 before = i;
                        while (i < _i29_token->len - 1
                            && _i29_token->text[i] >= '0'
                               && _i29_token->text[i] <= '9') {
                            i += 1;
                        }
                        if (before == i) {
                            _i29_result = false;
                            goto _i29_done;
                        }
                    }
                }
                if (i + 2 < _i29_token->len && _i29_token->text[i] == 'h') {
                    i += 1;
                    if (_i29_token->text[i] == 'h') {
                        i += 1;
                    }
                } else if (i + 2 < _i29_token->len
                    && _i29_token->text[i] == 'l') {
                    i += 1;
                    if (_i29_token->text[i] == 'l') {
                        i += 1;
                    }
                } else if (i + 2 < _i29_token->len) {
                    c = _i29_token->text[i];
                    if (c == 'j' || c == 'z' || c == 't' || c == 'L') {
                        i += 1;
                    }
                }
                if (i + 2 != _i29_token->len) {
                    _i29_result = false;
                    goto _i29_done;
                }
                c = _i29_token->text[i];
                _i29_result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                goto _i29_done;
                _i29_done: ;
            } while (false);
            _i29_predicate = _i29_result;
        }
        if (_i29_predicate) {
            _i29_predicate = tokens->tokens[first].offset > 0;
        }
        if (_i29_predicate) {
            _i29_predicate =
                tokens->tokens[first].offset + tokens->tokens[first].len
               < tokens->text_len;
        }
        if (_i29_predicate) {
            _i29_predicate =
                tokens->text[tokens->tokens[first].offset - 1] == ' ';
        }
        if (_i29_predicate) {
            Token *first_token = &tokens->tokens[first];
            _i29_predicate = tokens->text[first_token->offset
                                          + first_token->len] == ',';
        }
        if (_i29_predicate) {
            csane_rule_issue(file, tokens->tokens[format.first].offset,
                             STRLIT("STYLE-PRINTF"),
                             "format contains only one conversion");
        }
    }
}

/* Match a single absolute difference, not arbitrary calculations involving
 * fabs. ASSERT_CLOSE accepts float/double, so exclude long-double fabsl. */

static bool
csane_style_assert_tolerance(CsaneFile *file, CsaneTokenView *view,
                             int32 name, int32 open) {
                                 bool _i30_result;
                                 bool _i30_predicate;
    TokenRange args[1];
    TokenRange condition;
    TokenRange difference;
    CsaneTokenView expression;
    int32 comparison = -1;
    int32 i;

    if (csane_tokens_arguments(view, open, args, 1) != 1) {
        return false;
    }
    condition = csane_tokens_unwrap(view, args[0]);
    if (!csane_tokens_subview(&expression, view, condition)) {
        return false;
    }
    i = csane_tokens_next(&expression, condition.first - 1);
    while (i < condition.end) {
        Token *token = &view->tokens->tokens[i];
        if (TOKEN_IS(token, "&&") || TOKEN_IS(token, "||")
            || TOKEN_IS(token, "==") || TOKEN_IS(token, "!=")) {
            return false;
        }
        if (TOKEN_IS(token, "<") || TOKEN_IS(token, "<=")
            || TOKEN_IS(token, ">") || TOKEN_IS(token, ">=")) {
            if (comparison >= 0) {
                return false;
            }
            comparison = i;
        }
        if (token_is_open_delimiter(token)) {
            int32 close = csane_tokens_match(&expression, i);
            if (close < 0) {
                return false;
            }
            i = csane_tokens_next(&expression, close);
        } else {
            i = csane_tokens_next(&expression, i);
        }
    }
    if (comparison < 0) {
        return false;
    }
    if (TOKEN_IS(&view->tokens->tokens[comparison], "<")
        || TOKEN_IS(&view->tokens->tokens[comparison], "<=")) {
        difference = (TokenRange){condition.first, comparison};
    } else if (TOKEN_IS(&view->tokens->tokens[comparison], ">")
               || TOKEN_IS(&view->tokens->tokens[comparison], ">=")) {
        difference = (TokenRange){comparison + 1, condition.end};
    } else {
        return false;
    }
    do {
        CsaneTokenView *_i30_view = &expression;
        TokenRange _i30_range = difference;

        CsaneTokenView nested_expression;
        CsaneTokenView nested_difference;
        TokenRange nested_args[1];
        int32 nested_name;
        int32 nested_open;
        int32 close;
        int32 minus;

        _i30_range = csane_tokens_unwrap(_i30_view, _i30_range);
        if (!csane_tokens_subview(&nested_expression, _i30_view, _i30_range)) {
            _i30_result = false;
            goto _i30_done;
        }
        nested_name = csane_tokens_next(&nested_expression,
                                        _i30_range.first - 1);
        if (nested_name >= _i30_range.end
            || (!TOKEN_IS(&_i30_view->tokens->tokens[nested_name], "fabs")
                && !TOKEN_IS(&_i30_view->tokens->tokens[nested_name],
                             "fabsf"))) {
            _i30_result = false;
            goto _i30_done;
        }
        nested_open = csane_tokens_next(&nested_expression, nested_name);
        if (nested_open >= _i30_range.end
            || !TOKEN_IS(&_i30_view->tokens->tokens[nested_open], "(")) {
            _i30_result = false;
            goto _i30_done;
        }
        close = csane_tokens_match(&nested_expression, nested_open);
        if (close < 0 || csane_tokens_next(&nested_expression,
                                           close) < _i30_range.end
            || csane_tokens_arguments(&nested_expression, nested_open,
                                      nested_args, 1) != 1) {
            _i30_result = false;
            goto _i30_done;
        }
        _i30_range = csane_tokens_unwrap(&nested_expression, nested_args[0]);
        if (!csane_tokens_subview(&nested_difference, &nested_expression,
                                  _i30_range)) {
            _i30_result = false;
            goto _i30_done;
        }
        minus = csane_tokens_top_level(&nested_difference, _i30_range.first - 1,
                                       "-", 1);
        if (minus <= csane_tokens_next(&nested_difference, _i30_range.first - 1)
            || minus >= _i30_range.end
            || csane_tokens_next(&nested_difference, minus) >= _i30_range.end) {
            _i30_result = false;
            goto _i30_done;
        }
        /* Chained subtraction is not a direct comparison of two operands. */
        _i30_result = csane_tokens_top_level(&nested_difference, minus, "-", 1)
               == _i30_range.end;
        goto _i30_done;
        _i30_done: ;
    } while (false);
    _i30_predicate = _i30_result;
    if (_i30_predicate) {
        csane_rule_issue(file, view->tokens->tokens[name].offset,
                         STRLIT("STYLE-ASSERT-CLOSE"),
                         "use ASSERT_CLOSE(a, b, tol) for floating-point "
                         "tolerance comparisons");
        return true;
    }
    return false;
}

/* Only replace a standalone top-level ordered comparison. A relational
 * operator nested inside another expression is not the assertion condition. */
static void
csane_style_assert_rel(CsaneFile *file, CsaneTokenView *view,
                       int32 name, int32 open) {
    TokenRange args[1];
    TokenRange condition;
    CsaneTokenView expression;
    int32 comparison = -1;
    int32 i;
    char *message;

    if (csane_tokens_arguments(view, open, args, 1) != 1) {
        return;
    }
    condition = csane_tokens_unwrap(view, args[0]);
    if (!csane_tokens_subview(&expression, view, condition)) {
        return;
    }
    i = csane_tokens_next(&expression, condition.first - 1);
    while (i < condition.end) {
        Token *token = &view->tokens->tokens[i];
        if (TOKEN_IS(token, "&&") || TOKEN_IS(token, "||")
            || TOKEN_IS(token, "==") || TOKEN_IS(token, "!=")
            || TOKEN_IS(token, "?") || TOKEN_IS(token, ":")
            || TOKEN_IS(token, "=")) {
            return;
        }
        if (TOKEN_IS(token, "<") || TOKEN_IS(token, "<=")
            || TOKEN_IS(token, ">") || TOKEN_IS(token, ">=")) {
            if (comparison >= 0) {
                return;
            }
            comparison = i;
        }
        if (token_is_open_delimiter(token)) {
            int32 close = csane_tokens_match(&expression, i);
            if (close < 0) {
                return;
            }
            i = csane_tokens_next(&expression, close);
        } else {
            i = csane_tokens_next(&expression, i);
        }
    }
    if (comparison <= csane_tokens_next(&expression, condition.first - 1)
        || csane_tokens_next(&expression, comparison) >= condition.end) {
        return;
    }
    if (TOKEN_IS(&view->tokens->tokens[comparison], ">")) {
        message = "use ASSERT_GT(lhs, rhs) for constant rhs, otherwise "
                  "ASSERT_GT_VAR(lhs, rhs)";
    } else if (TOKEN_IS(&view->tokens->tokens[comparison], ">=")) {
        message = "use ASSERT_GE(lhs, rhs) for constant rhs, otherwise "
                  "ASSERT_GE_VAR(lhs, rhs)";
    } else if (TOKEN_IS(&view->tokens->tokens[comparison], "<")) {
        message = "use ASSERT_LT(lhs, rhs) for constant rhs, otherwise "
                  "ASSERT_LT_VAR(lhs, rhs)";
    } else {
        message = "use ASSERT_LE(lhs, rhs) for constant rhs, otherwise "
                  "ASSERT_LE_VAR(lhs, rhs)";
    }
    csane_rule_issue(file, view->tokens->tokens[name].offset,
                     STRLIT("STYLE-ASSERT-REL"), message);
}

/* Accept plain C integer tokens, including octal/hex/binary forms and
 * the standard U/L/LL suffixes. Decimal and hexadecimal floating-point
 * literals must not be treated as integer arguments. */

/* Return the integer literal when the complete expression consists only
 * of a possibly signed/parenthesized integer constant. */
static int32
csane_style_integer_operand(CsaneTokenView *view, TokenRange range) {
    bool _i31_predicate;
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
    _i31_predicate = literal >= range.end;
    if (!_i31_predicate) {
        bool _i31_result;
        do {
            Token *_i31_token = &tokens->tokens[literal];

            int32 radix = 10;
            int32 i = 0;
            int32 digits = 0;
            int32 unsigned_count = 0;
            int32 long_count = 0;
            char long_case = 0;

            if (_i31_token->kind != TOKEN_LITERAL || _i31_token->len == 0
                || _i31_token->text[0] < '0' || _i31_token->text[0] > '9') {
                _i31_result = false;
                goto _i31_done;
            }
            if (_i31_token->text[0] == '0' && _i31_token->len >= 2) {
                if (_i31_token->text[1] == 'x' || _i31_token->text[1] == 'X') {
                    radix = 16;
                    i = 2;
                } else if (_i31_token->text[1] == 'b'
                    || _i31_token->text[1] == 'B') {
                    radix = 2;
                    i = 2;
                } else {
                    radix = 8;
                }
            }
            for (; i < _i31_token->len; i += 1) {
                char c = _i31_token->text[i];
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
                _i31_result = false;
                goto _i31_done;
            }
            for (; i < _i31_token->len; i += 1) {
                char c = _i31_token->text[i];
                if (c == 'u' || c == 'U') {
                    unsigned_count += 1;
                    if (unsigned_count > 1) {
                        _i31_result = false;
                        goto _i31_done;
                    }
                } else if (c == 'l' || c == 'L') {
                    long_count += 1;
                    if (long_count > 2
                        || (long_case != 0 && c != long_case)) {
                        _i31_result = false;
                        goto _i31_done;
                    }
                    long_case = c;
                } else {
                    _i31_result = false;
                    goto _i31_done;
                }
            }
            _i31_result = true;
            goto _i31_done;
            _i31_done: ;
        } while (false);
        _i31_predicate = !_i31_result;
    }
    if (!_i31_predicate) {
        _i31_predicate = csane_tokens_next(&local, literal) < range.end;
    }
    if (_i31_predicate) {
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

/* A fixed-array count belongs to LENGTH, not a manually divided
 * SIZEOF pair. Match complete token-local operands, including comments and
 * line breaks, and require the same array name on both sides. */
static void
csane_style_sizeof_length(CsaneFile *file, CsaneTokenView *view,
                          int32 name, int32 open, int32 close) {
    Tokenization *tokens = file->tokens;
    int32 array = csane_tokens_next(view, open);
    int32 divide;
    int32 second;
    int32 second_open;
    int32 second_array;
    int32 bracket;
    int32 zero;
    int32 bracket_close;
    int32 second_close;
    bool dereferenced;

    if (array >= close || tokens->tokens[array].kind != TOKEN_IDENT
        || csane_tokens_next(view, array) != close) {
        return;
    }
    divide = csane_tokens_next(view, close);
    if (divide >= view->range.end
        || !TOKEN_IS(&tokens->tokens[divide], "/")) {
        return;
    }
    second = csane_tokens_next(view, divide);
    if (second >= view->range.end
        || !TOKEN_IS(&tokens->tokens[second], "SIZEOF")) {
        return;
    }
    second_open = csane_tokens_next(view, second);
    if (second_open >= view->range.end
        || !TOKEN_IS(&tokens->tokens[second_open], "(")) {
        return;
    }
    second_array = csane_tokens_next(view, second_open);
    dereferenced = (second_array < view->range.end)
                   && TOKEN_IS(&tokens->tokens[second_array], "*");
    if (dereferenced) {
        second_array = csane_tokens_next(view, second_array);
    }
    if (second_array >= view->range.end
        || tokens->tokens[second_array].kind != TOKEN_IDENT
        || tokens->tokens[array].len != tokens->tokens[second_array].len
        || memcmp64(tokens->tokens[array].text,
                    tokens->tokens[second_array].text,
                    tokens->tokens[array].len) != 0) {
        return;
    }
    if (dereferenced) {
        second_close = csane_tokens_next(view, second_array);
    } else {
        bracket = csane_tokens_next(view, second_array);
        if (bracket >= view->range.end
            || !TOKEN_IS(&tokens->tokens[bracket], "[")) {
            return;
        }
        zero = csane_tokens_next(view, bracket);
        if (zero >= view->range.end
            || !TOKEN_IS(&tokens->tokens[zero], "0")) {
            return;
        }
        bracket_close = csane_tokens_next(view, zero);
        if (bracket_close >= view->range.end
            || !TOKEN_IS(&tokens->tokens[bracket_close], "]")
            || csane_tokens_match(view, bracket) != bracket_close) {
            return;
        }
        second_close = csane_tokens_next(view, bracket_close);
    }
    if (second_close >= view->range.end
        || !TOKEN_IS(&tokens->tokens[second_close], ")")
        || csane_tokens_match(view, second_open) != second_close) {
        return;
    }
    csane_rule_issue(file, tokens->tokens[name].offset,
                     STRLIT("STYLE-SIZEOF-LENGTH"),
                     "use LENGTH(array) instead of manual SIZEOF division");
}

/* In a SIZEOF multiplication, the other signed integer operand is
 * already converted to int64. Restrict this syntactic check to one simple
 * name (optionally parenthesized); casts around compound expressions or
 * signed unary operations may change the evaluation semantics. */
static int32
csane_style_sizeof_cast_operand(CsaneTokenView *view, int32 first,
                                int32 *last) {
    Tokenization *tokens = view->tokens;
    int32 name = first;
    int32 close;

    if (first < view->range.first || first >= view->range.end) {
        return -1;
    }
    if (TOKEN_IS(&tokens->tokens[first], "(")) {
        close = csane_tokens_match(view, first);
        name = csane_tokens_next(view, first);
        if (close < 0 || name >= close
            || csane_tokens_next(view, name) != close) {
            return -1;
        }
        *last = close;
    } else {
        *last = first;
    }
    if (tokens->tokens[name].kind != TOKEN_IDENT) {
        return -1;
    }
    return name;
}

static void
csane_style_sizeof_int64_cast(CsaneFile *file, CsaneTokenView *view,
                              int32 name, int32 close) {
    Tokenization *expression_tokens = view->tokens;
    int32 before = csane_tokens_previous(view, name);
    int32 after = csane_tokens_next(view, close);
    int32 cast;

    if (before >= view->range.first
        && TOKEN_IS(&expression_tokens->tokens[before], "*")) {
        int32 in_result_before;
        do {
            int32 in_multiply = before;

            Tokenization *tokens = view->tokens;
            int32 last = csane_tokens_previous(view, in_multiply);
            int32 first = last;
            int32 cast_close;
            int32 type;
            int32 cast_open;

            if (last < view->range.first) {
                in_result_before = -1;
                break;
            }
            if (TOKEN_IS(&tokens->tokens[last], ")")) {
                first = csane_tokens_match(view, last);
                if (first < 0) {
                    in_result_before = -1;
                    break;
                }
            }
            if (csane_style_sizeof_cast_operand(view, first, &last) < 0
                || csane_tokens_next(view, last) != in_multiply) {
                in_result_before = -1;
                break;
            }
            cast_close = csane_tokens_previous(view, first);
            if (cast_close < view->range.first
                || !TOKEN_IS(&tokens->tokens[cast_close], ")")) {
                in_result_before = -1;
                break;
            }
            type = csane_tokens_previous(view, cast_close);
            if (type < view->range.first
                || !TOKEN_IS(&tokens->tokens[type], "int64")) {
                in_result_before = -1;
                break;
            }
            cast_open = csane_tokens_previous(view, type);
            if (cast_open < view->range.first
                || !TOKEN_IS(&tokens->tokens[cast_open], "(")
                || csane_tokens_match(view, cast_open) != cast_close) {
                in_result_before = -1;
                break;
            }
            in_result_before = cast_open;
        } while (false);
        cast = in_result_before;
        if (cast >= 0) {
            csane_rule_issue(file, expression_tokens->tokens[cast].offset,
                             STRLIT("STYLE-SIZEOF-INT64-CAST"),
                             "remove redundant int64 cast before "
                             "multiplication by SIZEOF");
        }
    }
    if (after < view->range.end
        && TOKEN_IS(&expression_tokens->tokens[after], "*")) {
        int32 in_result_after;
        do {
            int32 in_multiply = after;

            Tokenization *tokens = view->tokens;
            int32 cast_open = csane_tokens_next(view, in_multiply);
            int32 type;
            int32 cast_close;
            int32 first;
            int32 last;
            int32 next;

            if (cast_open >= view->range.end
                || !TOKEN_IS(&tokens->tokens[cast_open], "(")) {
                in_result_after = -1;
                break;
            }
            type = csane_tokens_next(view, cast_open);
            if (type >= view->range.end
                || !TOKEN_IS(&tokens->tokens[type], "int64")) {
                in_result_after = -1;
                break;
            }
            cast_close = csane_tokens_next(view, type);
            if (cast_close >= view->range.end
                || !TOKEN_IS(&tokens->tokens[cast_close], ")")
                || csane_tokens_match(view, cast_open) != cast_close) {
                in_result_after = -1;
                break;
            }
            first = csane_tokens_next(view, cast_close);
            if (csane_style_sizeof_cast_operand(view, first, &last) < 0) {
                in_result_after = -1;
                break;
            }
            /* Do not treat a function call, member access, or indexed
             * expression as just its first identifier. */
            next = csane_tokens_next(view, last);
            if (next < view->range.end
                && (TOKEN_IS(&tokens->tokens[next], "(")
                    || TOKEN_IS(&tokens->tokens[next], "[")
                    || TOKEN_IS(&tokens->tokens[next], ".")
                    || TOKEN_IS(&tokens->tokens[next], "->")
                    || TOKEN_IS(&tokens->tokens[next], "++")
                    || TOKEN_IS(&tokens->tokens[next], "--"))) {
                in_result_after = -1;
                break;
            }
            in_result_after = cast_open;
        } while (false);
        cast = in_result_after;
        if (cast >= 0) {
            csane_rule_issue(file, expression_tokens->tokens[cast].offset,
                             STRLIT("STYLE-SIZEOF-INT64-CAST"),
                             "remove redundant int64 cast before "
                             "multiplication by SIZEOF");
        }
    }
}

/* Only an entire parenthesized bitwise AND operand is a flag test.
 * Inspect tokens beside != rather than limiting the rule to if headers, so
 * assignments, returns, and nested boolean expressions also qualify. */
static bool
csane_style_bitwise_operand(CsaneTokenView *view, int32 comparison,
                            bool before) {
    Tokenization *tokens = view->tokens;
    CsaneTokenView local;
    TokenRange range;
    int32 open;
    int32 close;
    int32 edge;
    bool found = false;

    if (before) {
        close = csane_tokens_previous(view, comparison);
        if (close < view->range.first
            || !TOKEN_IS(&tokens->tokens[close], ")")) {
            return false;
        }
        open = csane_tokens_match(view, close);
    } else {
        open = csane_tokens_next(view, comparison);
        if (open >= view->range.end
            || !TOKEN_IS(&tokens->tokens[open], "(")) {
            return false;
        }
        close = csane_tokens_match(view, open);
    }
    if (open < view->range.first || close < 0) {
        return false;
    }
    /* A mask must be the whole operand, not the tail of a sum or the
     * first term of a larger expression. */
    edge = before ? csane_tokens_previous(view, open)
                  : csane_tokens_next(view, close);
    if (edge >= view->range.first && edge < view->range.end) {
        Token *boundary = &tokens->tokens[edge];
        if (before) {
            if (!TOKEN_IS(boundary, "(") && !TOKEN_IS(boundary, "{")
                && !TOKEN_IS(boundary, ",")
                && !TOKEN_IS(boundary, "=")
                && !TOKEN_IS(boundary, "&&")
                && !TOKEN_IS(boundary, "||")
                && !TOKEN_IS(boundary, "?")
                && !TOKEN_IS(boundary, ":")
                && !TOKEN_IS(boundary, "return")) {
                return false;
            }
        } else if (!TOKEN_IS(boundary, ")")
                   && !TOKEN_IS(boundary, "]")
                   && !TOKEN_IS(boundary, "}")
                   && !TOKEN_IS(boundary, ",")
                   && !TOKEN_IS(boundary, ";")
                   && !TOKEN_IS(boundary, "&&")
                   && !TOKEN_IS(boundary, "||")
                   && !TOKEN_IS(boundary, "?")
                   && !TOKEN_IS(boundary, ":")) {
            return false;
        }
    }
    range = csane_tokens_unwrap(view, (TokenRange){open, close + 1});
    if (!csane_tokens_subview(&local, view, range)) {
        return false;
    }
    edge = csane_tokens_next(&local, range.first - 1);
    for (int32 i = edge; i < range.end; i = csane_tokens_next(&local, i)) {
        Token *token = &tokens->tokens[i];
        if (TOKEN_IS(token, "(") || TOKEN_IS(token, "[")) {
            int32 matching = csane_tokens_match(&local, i);
            if (matching < 0) {
                return false;
            }
            i = matching;
        } else if (TOKEN_IS(token, "&")) {
            int32 previous = csane_tokens_previous(&local, i);
            int32 next = csane_tokens_next(&local, i);
            if (previous < edge || next >= range.end) {
                return false;
            }
            found = true;
        }
    }
    return found;
}

/* Match a whole count/zero operand beside !=. Identifiers, member
 * accesses, and direct calls are named counts; arbitrary expressions are not.
 * Use names instead of guessing the type of a variable or return value. */
static bool
csane_style_count_operand(CsaneTokenView *view, int32 op, bool before,
                          bool count) {
    Tokenization *tokens = view->tokens;
    CsaneTokenView local;
    TokenRange range;
    int32 edge;
    int32 other;
    int32 i;
    int32 last;

    if (before) {
        edge = csane_tokens_previous(view, op);
        if (edge < view->range.first) {
            return false;
        }
        range.end = edge + 1;
        range.first = edge;
        if (TOKEN_IS(&tokens->tokens[edge], ")")) {
            int32 name;

            range.first = csane_tokens_match(view, edge);
            if (range.first < view->range.first) {
                return false;
            }

            /* A direct call ends at ')'; include its name and owner. */
            name = csane_tokens_previous(view, range.first);
            if (name >= view->range.first
                && tokens->tokens[name].kind == TOKEN_IDENT
                && !TOKEN_IS(&tokens->tokens[name], "if")
                && !TOKEN_IS(&tokens->tokens[name], "while")
                && !TOKEN_IS(&tokens->tokens[name], "for")
                && !TOKEN_IS(&tokens->tokens[name], "switch")) {
                range.first = name;
            }
        }
        if (tokens->tokens[range.first].kind == TOKEN_IDENT) {
            for (;;) {
                int32 separator = csane_tokens_previous(view, range.first);
                int32 owner = csane_tokens_previous(view, separator);
                if (separator < view->range.first || owner < view->range.first
                    || (!TOKEN_IS(&tokens->tokens[separator], ".")
                        && !TOKEN_IS(&tokens->tokens[separator], "->"))
                    || tokens->tokens[owner].kind != TOKEN_IDENT) {
                    break;
                }
                range.first = owner;
            }
        }
        if (range.first < view->range.first) {
            return false;
        }
        other = csane_tokens_previous(view, range.first);
        if (other >= view->range.first) {
            Token *token = &tokens->tokens[other];
            if (!TOKEN_IS(token, "(") && !TOKEN_IS(token, "[")
                && !TOKEN_IS(token, "{") && !TOKEN_IS(token, ",")
                && !TOKEN_IS(token, ";") && !TOKEN_IS(token, "=")
                && !TOKEN_IS(token, "&&") && !TOKEN_IS(token, "||")
                && !TOKEN_IS(token, "?") && !TOKEN_IS(token, ":")
                && !TOKEN_IS(token, "return")) {
                return false;
            }
        }
    } else {
        edge = csane_tokens_next(view, op);
        if (edge >= view->range.end) {
            return false;
        }
        range.first = edge;
        if (TOKEN_IS(&tokens->tokens[edge], "(")) {
            int32 close = csane_tokens_match(view, edge);
            if (close < 0) {
                return false;
            }
            range.end = close + 1;
        } else {
            range.end = edge + 1;
            if (tokens->tokens[edge].kind == TOKEN_IDENT) {
                int32 call;
                for (;;) {
                    int32 separator = csane_tokens_next(view, range.end - 1);
                    int32 member = csane_tokens_next(view, separator);
                    if (separator >= view->range.end
                        || member >= view->range.end
                        || (!TOKEN_IS(&tokens->tokens[separator], ".")
                            && !TOKEN_IS(&tokens->tokens[separator], "->"))
                        || tokens->tokens[member].kind != TOKEN_IDENT) {
                        break;
                    }
                    range.end = member + 1;
                }

                call = csane_tokens_next(view, range.end - 1);
                if (call < view->range.end
                    && TOKEN_IS(&tokens->tokens[call], "(")) {
                    int32 close = csane_tokens_match(view, call);
                    if (close < 0) {
                        return false;
                    }
                    range.end = close + 1;
                }
            }
        }
        other = csane_tokens_next(view, range.end - 1);
        if (other < view->range.end) {
            Token *token = &tokens->tokens[other];
            if (!TOKEN_IS(token, ")") && !TOKEN_IS(token, "]")
                && !TOKEN_IS(token, "}") && !TOKEN_IS(token, ",")
                && !TOKEN_IS(token, ";") && !TOKEN_IS(token, "&&")
                && !TOKEN_IS(token, "||") && !TOKEN_IS(token, "?")
                && !TOKEN_IS(token, ":") && !TOKEN_IS(token, "==")
                && !TOKEN_IS(token, "!=")) {
                return false;
            }
        }
    }
    range = csane_tokens_unwrap(view, range);
    if (!csane_tokens_subview(&local, view, range)) {
        return false;
    }
    i = csane_tokens_next(&local, range.first - 1);
    if (i >= range.end) {
        return false;
    }
    if (!count) {
        return TOKEN_IS(&tokens->tokens[i], "0")
               && tokens->tokens[i].kind == TOKEN_LITERAL
               && csane_tokens_next(&local, i) >= range.end;
    }
    if (tokens->tokens[i].kind != TOKEN_IDENT) {
        return false;
    }
    last = i;
    i = csane_tokens_next(&local, i);
    while (i < range.end
           && (TOKEN_IS(&tokens->tokens[i], ".")
               || TOKEN_IS(&tokens->tokens[i], "->"))) {
        i = csane_tokens_next(&local, i);
        if (i >= range.end || tokens->tokens[i].kind != TOKEN_IDENT) {
            return false;
        }
        last = i;
        i = csane_tokens_next(&local, i);
    }
    /* Only a call of the last identifier is allowed, not a calculation
     * involving a call or a chain of calls. */
    if (i < range.end && TOKEN_IS(&tokens->tokens[i], "(")) {
        int32 close = csane_tokens_match(&local, i);
        if (close < 0) {
            return false;
        }
        i = csane_tokens_next(&local, close);
    }
    if (i < range.end) {
        return false;
    }
    {
        int32 prefix = -1;
        Token *name = &tokens->tokens[last];

        if (ENDS_WITH(name->text, name->len, "_count")
            || ENDS_WITH(name->text, name->len, "_len")
            || ENDS_WITH(name->text, name->len, "_cap")
            || ENDS_WITH(name->text, name->len, "_capacity")
            || (last != range.first
                && (TOKEN_IS(name, "len") || TOKEN_IS(name, "count")
                    || TOKEN_IS(name, "cap")
                    || TOKEN_IS(name, "capacity")))) {
            return true;
        }
        if (name->len >= 3 && name->text[0] == 'n') {
            prefix = 0;
        } else {
            for (int32 j = 0; j < name->len - 3; j += 1) {
                if (name->text[j] == '_' && name->text[j + 1] == 'n') {
                    prefix = j + 1;
                    break;
                }
            }
        }
        if (prefix < 0 || name->len - prefix < 3
            || name->text[name->len - 1] != 's'
            || name->text[prefix + 1] < 'a'
            || name->text[prefix + 1] > 'z'
            || ENDS_WITH(name->text, name->len, "_status")
            || ENDS_WITH(name->text, name->len, "_success")
            || ENDS_WITH(name->text, name->len, "nstatus")
            || ENDS_WITH(name->text, name->len, "nsuccess")) {
            return false;
        }
        for (int32 j = prefix + 2; j < name->len - 1; j += 1) {
            char c = name->text[j];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                  || c == '_')) {
                return false;
            }
        }
    }
    return true;
}

/* The comparison macros already reject strings shorter than their pattern.
 * Recognize a directly preceding length guard on the same length argument. */
static bool
csane_style_redundant_string_guard(CsaneTokenView *view, int32 name,
                                   int32 open) {
    Tokenization *tokens = view->tokens;
    TokenRange args[4];
    TokenRange length;
    int32 and_token = csane_tokens_previous(view, name);
    int32 last;
    int32 first;
    int32 bound;
    int32 comparison;
    int32 left;
    int32 right;

    if (and_token < view->range.first
        || !TOKEN_IS(&tokens->tokens[and_token], "&&")
        || csane_tokens_arguments(view, open, args, LENGTH(args)) < 3) {
        return false;
    }
    length = csane_tokens_unwrap(view, args[1]);
    last = csane_tokens_previous(view, and_token);
    if (last < view->range.first) {
        return false;
    }
    if (TOKEN_IS(&tokens->tokens[last], ")")) {
        first = csane_tokens_match(view, last);
        if (first < 0 || !TOKEN_IS(&tokens->tokens[first], "(")) {
            return false;
        }
        right = csane_tokens_previous(view, last);
    } else {
        first = -1;
        right = last;
    }
    if (right < view->range.first
        || tokens->tokens[right].kind != TOKEN_LITERAL) {
        return false;
    }
    comparison = csane_tokens_previous(view, right);
    if (comparison < view->range.first
        || (!TOKEN_IS(&tokens->tokens[comparison], ">=")
            && !TOKEN_IS(&tokens->tokens[comparison], ">"))) {
        return false;
    }
    left = csane_tokens_previous(view, comparison);
    bound = csane_tokens_previous(view, length.end);
    while (bound >= length.first) {
        if (left < view->range.first
            || !STREQUAL(tokens->tokens[left].text, tokens->tokens[left].len,
                         tokens->tokens[bound].text,
                         tokens->tokens[bound].len)) {
            return false;
        }
        left = csane_tokens_previous(view, left);
        bound = csane_tokens_previous(view, bound);
    }
    if (first >= 0) {
        return left == first;
    }
    return left < view->range.first
           || TOKEN_IS(&tokens->tokens[left], "(")
           || TOKEN_IS(&tokens->tokens[left], "&&")
           || TOKEN_IS(&tokens->tokens[left], "||");
}

/* strcmp is a three-way comparator, not a string equality predicate.
 * Permit its value only when a simple statement directly returns it or
 * stores it in a variable. In particular, assignment inside an if condition
 * is not a standalone assignment statement. */
static bool
csane_style_strcmp_value(CsaneTokenView *view, TokenRange expression,
                         int32 start, int32 close) {
    CsaneTokenView local;
    int32 first;
    int32 after;

    expression = csane_tokens_unwrap(view, expression);
    if (!csane_tokens_subview(&local, view, expression)) {
        return false;
    }
    first = csane_tokens_next(&local, expression.first - 1);
    after = csane_tokens_next(&local, close);
    return first == start && after >= expression.end;
}

static bool
csane_style_strcmp_simple(CsaneFile *file, CsaneStatement *statement,
                          int32 start, int32 close) {
    CsaneTokenView view;
    CsaneTokenView segment;
    TokenRange ranges[64];
    TokenRange expression;
    TokenRange span = statement->tokens;
    int32 last;
    int32 first;
    int32 count;

    if (statement->kind != CSANE_STMT_SIMPLE
        || !csane_tokens_view(&view, file->tokens, span)) {
        return false;
    }
    last = csane_tokens_previous(&view, span.end);
    if (last < span.first
        || !TOKEN_IS(&file->tokens->tokens[last], ";")) {
        return false;
    }
    span.end = last;
    if (!csane_tokens_view(&view, file->tokens, span)) {
        return false;
    }
    first = csane_tokens_next(&view, span.first - 1);
    if (first < span.end
        && TOKEN_IS(&file->tokens->tokens[first], "return")) {
        expression = (TokenRange){first + 1, span.end};
        return csane_style_strcmp_value(&view, expression, start, close);
    }
    count = csane_tokens_split(&view, STRLIT(","), ranges, LENGTH(ranges));
    if (count < 0 || count > LENGTH(ranges)) {
        return false;
    }
    for (int32 i = 0; i < count; i += 1) {
        int32 equal;
        int32 before;
        if (start < ranges[i].first || close >= ranges[i].end
            || !csane_tokens_subview(&segment, &view, ranges[i])) {
            continue;
        }
        equal = csane_tokens_top_level(&segment, ranges[i].first - 1,
                                       STRLIT("="));
        if (equal < ranges[i].first || equal >= ranges[i].end) {
            continue;
        }
        before = csane_tokens_previous(&segment, equal);
        if (before < ranges[i].first) {
            continue;
        }
        expression = (TokenRange){equal + 1, ranges[i].end};
        if (csane_style_strcmp_value(&segment, expression, start, close)) {
            return true;
        }
    }
    return false;
}

static bool
csane_style_strcmp_allowed(CsaneFile *file, int32 name, int32 start,
                           int32 close) {
    int32 count;
    int32 index = -1;

    if (file->statements == NULL) {
        return false;
    }
    if (file->layout_statement_index != NULL
        && name < ARRAY_LEN(file->layout_statement_index)) {
        index = file->layout_statement_index[name];
    }
    if (index >= 0) {
        return csane_style_strcmp_simple(file,
                                         &file->statements->items[index],
                                         start, close);
    }
    /* An overlapping #if stream has no unique statement index. */
    if (index != -2) {
        return false;
    }
    count = ARRAY_LEN(file->statements->items);
    for (int32 i = 0; i < count; i += 1) {
        CsaneStatement *statement = &file->statements->items[i];
        if (name >= statement->tokens.first
            && close < statement->tokens.end
            && csane_style_strcmp_simple(file, statement, start, close)) {
            return true;
        }
    }
    return false;
}

void
csane_expression_style_check(CsaneFile *file) {
    CsaneTokenView view = csane_tokens_full(file->tokens);
    Tokenization *tokens = file->tokens;
    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        Token *token = &tokens->tokens[i];
        int32 next;
        int32 previous;
        int32 call_start = i;
        bool wrapped_strcmp = false;
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
            || TOKEN_IS(token, "if") || TOKEN_IS(token, "for")
            || TOKEN_IS(token, "while") || TOKEN_IS(token, "switch")
            || TOKEN_IS(token, "sizeof") || TOKEN_IS(token, "_Alignof")
            || TOKEN_IS(token, "_Generic")
            || TOKEN_IS(token, "_Static_assert")
            || TOKEN_IS(token, "typeof")
            || TOKEN_IS(token, "__typeof__")) {
            continue;
        }
        next = csane_tokens_next(&view, i);
        previous = csane_tokens_previous(&view, i);
        if (TOKEN_IS(token, "strcmp")) {
            while (previous >= 0 && next < ARRAY_LEN(tokens->tokens)
                   && TOKEN_IS(&tokens->tokens[previous], "(")
                   && TOKEN_IS(&tokens->tokens[next], ")")
                   && csane_tokens_match(&view, previous) == next) {
                call_start = previous;
                next = csane_tokens_next(&view, next);
                previous = csane_tokens_previous(&view, call_start);
                wrapped_strcmp = true;
            }
        }
        if (next >= ARRAY_LEN(tokens->tokens)
            || !TOKEN_IS(&tokens->tokens[next], "(")) {
            continue;
        }
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
                    || TOKEN_IS(token, "LENGTH")
                    || TOKEN_IS(token, "STREQUAL")
                    || TOKEN_IS(token, "BEGINS_WITH")
                    || TOKEN_IS(token, "ENDS_WITH")
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
        range = (TokenRange){0, ARRAY_LEN(tokens->tokens)};
        kind = wrapped_strcmp ? CSANE_LOCAL_CALL
                              : csane_classify_invocation(tokens, range, i);
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
            if ((TOKEN_IS(token, "BEGINS_WITH")
                 || TOKEN_IS(token, "ENDS_WITH"))
                && csane_style_redundant_string_guard(&local, i, next)) {
                csane_rule_issue(file, token->offset,
                                 STRLIT("STYLE-STRING-LENGTH-GUARD"),
                                 "omit the redundant length guard before "
                                 "BEGINS_WITH or ENDS_WITH");
            }
            if (TOKEN_IS(token, "strcmp")
                && !csane_style_strcmp_allowed(file, i, call_start, close)) {
                csane_rule_issue(file, token->offset,
                                 STRLIT("STYLE-STRCMP"),
                                 "use STREQUAL or strequal for equality; "
                                 "strcmp only allowed for sorting purposes.");
            }
            if (TOKEN_IS(token, "ASSERT")) {
                if (!csane_style_assert_tolerance(file, &local, i, next)) {
                    csane_style_assert_rel(file, &local, i, next);
                }
            }
            if (TOKEN_IS(token, "strlen32")) {
                bool indexed = false;
                if (csane_style_strlen32_member(file, &local, next,
                                                &indexed) && indexed) {
                    bool initializing =
                        csane_style_strlen32_length_assignment(file, &local,
                                                               i, next, close);
                    if (!initializing) {
                        csane_rule_issue(file, token->offset,
                                         STRLIT("STYLE-STRLEN32-REDUNDANT"),
                                         "strlen32 of a struct array item "
                                         "must use the stored item length");
                    }
                }
            }
            if (TOKEN_IS(token, "SIZEOF")) {
                csane_style_sizeof_length(file, &local, i, next, close);
                csane_style_sizeof_int64_cast(file, &local, i, close);
            }
            if (TOKEN_IS(token, "floor") || TOKEN_IS(token, "ceil")
                || TOKEN_IS(token, "round")) {
                csane_style_round64(file, &local, i);
            } else if (TOKEN_IS(token, "ASSERT")
                       || TOKEN_IS(token, "ASSERT_CLOSE")
                       || TOKEN_IS(token, "SIZEOF")
                       || TOKEN_IS(token, "STRLIT_LEN")
                       || TOKEN_IS(token, "LENGTH")
                       || TOKEN_IS(token, "STREQUAL")
                       || TOKEN_IS(token, "BEGINS_WITH")
                       || TOKEN_IS(token, "ENDS_WITH")
                       || TOKEN_IS(token, "str_printf")) {
                csane_style_one(file, &local, i, next, close);
            }
        }
    }
    /* Count operands have non-negative meaning: use > 0, not != 0.
     * Also match reverse comparisons and parenthesized members. */
    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        Token *token = &tokens->tokens[i];
        if (!TOKEN_IS(token, "!=")) {
            continue;
        }
        if (file->preprocessor->token_roles != NULL
            && file->preprocessor->token_roles[i] != C_PP_TOKEN_CODE
            && file->preprocessor->token_roles[i]
               != C_PP_TOKEN_MACRO_REPLACEMENT) {
            continue;
        }
        if ((csane_style_count_operand(&view, i, true, true)
             && csane_style_count_operand(&view, i, false, false))
            || (csane_style_count_operand(&view, i, true, false)
                && csane_style_count_operand(&view, i, false, true))) {
            csane_rule_issue(file, token->offset,
                             STRLIT("STYLE-COUNT-NONZERO"),
                             "use > 0 instead of != 0 for counts, lengths, "
                             "or capacities");
        }
        if ((csane_style_bitwise_operand(&view, i, true)
             && csane_style_count_operand(&view, i, false, false))
            || (csane_style_count_operand(&view, i, true, false)
                && csane_style_bitwise_operand(&view, i, false))) {
            csane_rule_issue(file, token->offset,
                             STRLIT("STYLE-BITWISE-NONZERO"),
                             "omit != 0 after a parenthesized bitwise AND");
        }
    }
    csane_matrix_index_check(file);
}

#endif /* CSANECHECK_EXPRESSION_STYLE_C */
