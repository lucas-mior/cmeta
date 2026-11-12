// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck/csck_tokens.c"

#define TESTING_token_utils_test 1
#if TESTING_token_utils_test

static int32 failures = 0;

static void
test_check(bool condition, int32 line) {
    if (!condition) {
        error2("token_utils_test.c:%d: failed\n", line);
        failures += 1;
    }
    return;
}

#define CHECK(CONDITION) test_check((CONDITION), __LINE__)

static bool
test_range_text(Tokenization *tokens, TokenRange range,
                char *expected, int32 expected_len) {
    range = token_range_trim_trivia(tokens, range);
    SourceRange source = token_range_source_range(tokens, range);
    return STREQUAL(tokens->text + source.start, source.end - source.start,
                    expected, expected_len);
}

static int32
test_find_token(Tokenization *tokens,
                char *text, int32 text_len, int32 occurrence) {
    int32 found = 0;
    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        Token *token = &tokens->tokens[i];
        if (!STREQUAL(token->text, token->len, text, text_len)) {
            continue;
        }
        if (found == occurrence) {
            return i;
        }
        found += 1;
    }
    return -1;
}

/* Independent linear references, deliberately not using navigation helpers. */
static int32
test_range_first_reference(Tokenization *tokens, TokenRange range) {
    if (!token_range_is_valid(tokens, range)) {
        return -1;
    }
    for (int32 i = range.first; i < range.end; i += 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    return -1;
}

static int32
test_range_last_reference(Tokenization *tokens, TokenRange range) {
    if (!token_range_is_valid(tokens, range)) {
        return -1;
    }
    for (int32 i = range.end - 1; i >= range.first; i -= 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    return -1;
}

static int32
test_range_next_reference(Tokenization *tokens, TokenRange range,
                          int32 cursor) {
    if (!token_range_is_valid(tokens, range)) {
        return -1;
    }
    int32 first = range.first;
    if (cursor >= range.first) {
        if (cursor >= range.end - 1) {
            return range.end;
        }
        first = cursor + 1;
    }
    for (int32 i = first; i < range.end; i += 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    return range.end;
}

static int32
test_range_previous_reference(Tokenization *tokens, TokenRange range,
                              int32 cursor) {
    if (!token_range_is_valid(tokens, range)) {
        return -1;
    }
    if (cursor <= range.first) {
        return range.first - 1;
    }
    int32 last = range.end - 1;
    if (cursor <= range.end) {
        last = cursor - 1;
    }
    for (int32 i = last; i >= range.first; i -= 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    return range.first - 1;
}

static int32
test_global_at_or_after_reference(Tokenization *tokens, int32 cursor) {
    int32 end = ARRAY_LEN(tokens->tokens);
    for (int32 i = cursor; i < end; i += 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    if (cursor > end) {
        return cursor;
    }
    return end;
}

static int32
test_global_previous_reference(Tokenization *tokens, int32 cursor) {
    for (int32 i = cursor - 1; i >= 0; i -= 1) {
        if (!token_is_trivia(&tokens->tokens[i])) {
            return i;
        }
    }
    return -1;
}

static void
test_significant_index(Tokenization *tokens) {
    int32 count = ARRAY_LEN(tokens->tokens);

    CHECK(ARRAY_LEN(tokens->next_significant) == count + 1);
    CHECK(ARRAY_LEN(tokens->previous_significant) == count + 1);
    if ((ARRAY_LEN(tokens->next_significant) != count + 1)
        || (ARRAY_LEN(tokens->previous_significant) != count + 1)) {
        return;
    }

    for (int32 i = 0; i <= count; i += 1) {
        CHECK(tokens->next_significant[i]
              == test_global_at_or_after_reference(tokens, i));
        CHECK(tokens->previous_significant[i]
              == test_global_previous_reference(tokens, i));
    }
    return;
}

static void
test_significant_index_modes(void) {
    char *source = "  #define VALUE 2\nx /* comment */ y\n";
    int32 flags[] = {
        TOKENIZE_DEFAULT,
        TOKENIZE_PREPROCESSOR_LINES,
        TOKENIZE_SKIP_WHITESPACE,
        TOKENIZE_PREPROCESSOR_LINES | TOKENIZE_SKIP_WHITESPACE,
    };

    for (int32 mode = 0; mode < LENGTH(flags); mode += 1) {
        Tokenization tokens =
            tokenize_with_flags(source, strlen32(source), flags[mode]);
        test_significant_index(&tokens);
        if (flags[mode] & TOKENIZE_PREPROCESSOR_LINES) {
            CHECK(ARRAY_LEN(tokens.tokens) == 1);
        }
        free_tokenization(&tokens);
        CHECK(tokens.next_significant == NULL);
        CHECK(tokens.previous_significant == NULL);
    }

    Tokenization empty = tokenize("", 0);
    test_significant_index(&empty);
    free_tokenization(&empty);
    CHECK(empty.next_significant == NULL);
    CHECK(empty.previous_significant == NULL);
    return;
}

static void
test_significant_navigation(void) {
    StrFlex *sources[] = {
        SFLIT(""),
        SFLIT(" \t\r\n"),
        SFLIT("/* block */ // line\n"),
        SFLIT("a"),
        SFLIT("a+b;c"),
        SFLIT(" \ta /* middle */ + b // end\n c \t"),
        SFLIT("/* first */ x /* second */ y /* last */"),
        SFLIT("#if X\nleft\n#else\nright\n#endif\nend\n"),
    };
    int32 flags[] = {TOKENIZE_DEFAULT, TOKENIZE_PREPROCESSOR_LINES};

    for (int32 mode = 0; mode < LENGTH(flags); mode += 1) {
        for (int32 sample = 0; sample < LENGTH(sources); sample += 1) {
            Tokenization tokens =
                tokenize_copy(sources[sample]->data, sources[sample]->len,
                              flags[mode]);
            int32 end = ARRAY_LEN(tokens.tokens);
            CsaneTokenView full = csane_tokens_full(&tokens);

            test_significant_index(&tokens);
            for (int32 cursor = 0; cursor <= end + 2; cursor += 1) {
                CHECK(tokenization_significant_at_or_after(&tokens, cursor)
                      == test_global_at_or_after_reference(&tokens, cursor));
            }
            for (int32 cursor = -1; cursor <= end + 2; cursor += 1) {
                CHECK(tokenization_next_significant(&tokens, cursor)
                      == test_global_at_or_after_reference(&tokens,
                                                           cursor + 1));
            }
            for (int32 cursor = 0; cursor <= end; cursor += 1) {
                CHECK(tokenization_previous_significant(&tokens, cursor)
                      == test_global_previous_reference(&tokens, cursor));
            }
            for (int32 first = 0; first <= end; first += 1) {
                for (int32 last = first; last <= end; last += 1) {
                    TokenRange range = {first, last};
                    CsaneTokenView view;
                    CHECK(csane_tokens_subview(&view, &full, range));
                    CHECK(token_range_first_significant(&tokens, range)
                          == test_range_first_reference(&tokens, range));
                    CHECK(token_range_last_significant(&tokens, range)
                          == test_range_last_reference(&tokens, range));
                    for (int32 cursor = first - 2; cursor <= last + 2;
                         cursor += 1) {
                        int32 next =
                            test_range_next_reference(&tokens, range, cursor);
                        int32 previous =
                            test_range_previous_reference(&tokens, range,
                                                          cursor);
                        CHECK(token_range_next_significant(&tokens, range,
                                                           cursor) == next);
                        CHECK(token_range_previous_significant(&tokens, range,
                                                               cursor)
                              == previous);
                        CHECK(csane_tokens_next(&view, cursor) == next);
                        CHECK(csane_tokens_previous(&view, cursor) == previous);
                    }
                }
            }
            TokenRange invalid[] = {
                {-1, 0}, {0, end + 1}, {end, end - 1},
                {end + 1, end + 1},
            };
            CHECK(token_range_first_significant(NULL, (TokenRange){0, 0})
                  == -1);
            CHECK(token_range_last_significant(NULL, (TokenRange){0, 0})
                  == -1);
            CHECK(token_range_next_significant(NULL, (TokenRange){0, 0}, 0)
                  == -1);
            CHECK(token_range_previous_significant(NULL,
                                                   (TokenRange){0, 0}, 0)
                  == -1);
            for (int32 i = 0; i < LENGTH(invalid); i += 1) {
                CHECK(token_range_first_significant(&tokens, invalid[i]) == -1);
                CHECK(token_range_last_significant(&tokens, invalid[i]) == -1);
                CHECK(token_range_next_significant(&tokens, invalid[i], 0)
                      == -1);
                CHECK(token_range_previous_significant(&tokens, invalid[i], 0)
                      == -1);
            }
            free_tokenization(&tokens);
        }
    }
    return;
}

static bool
test_view_first_is(CsaneTokenView *view, char *text, int32 text_len) {
    int32 first = csane_tokens_next(view, view->range.first - 1);
    return first >= view->range.first && first < view->range.end
           && STREQUAL(view->tokens->tokens[first].text,
                       view->tokens->tokens[first].len, text, text_len);
}

static void
test_conditional_path(void) {
    char *text =
        "call(prefix,\n"
        "#if FIRST\n"
        "    first,\n"
        "#else\n"
        "    second,\n"
        "#endif\n"
        "    suffix);\n";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CsaneTokenPath path;
    CsaneTokenView view;
    CsaneTokenView argument;
    TokenRange args[3];
    TokenRange split[3];
    CsaneTokenView inner;
    CsaneTokenView segment;
    int32 first_hash = test_find_token(&tokens, STRLIT("#"), 0);
    int32 endif_hash = test_find_token(&tokens, STRLIT("#"), 2);
    int32 second = test_find_token(&tokens, STRLIT("second"), 0);
    int32 suffix = test_find_token(&tokens, STRLIT("suffix"), 0);
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 close = test_find_token(&tokens, STRLIT(")"), 0);

    CHECK(first_hash >= 0 && endif_hash >= 0 && second >= 0 && suffix >= 0);
    CHECK(csane_token_path_init(&path, &tokens,
                                (TokenRange){0, ARRAY_LEN(tokens.tokens)}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, first_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){second, endif_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){suffix,
                                               ARRAY_LEN(tokens.tokens)}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_balanced(&view));
    CHECK(csane_tokens_match(&view, open) == close);
    CHECK(csane_tokens_arguments(&view, open, args, 3) == 3);
    CHECK(csane_tokens_subview(&inner, &view, (TokenRange){open + 1, close}));
    CHECK(csane_tokens_top_level(&inner, open, STRLIT(","))
          == test_find_token(&tokens, STRLIT(","), 0));
    CHECK(csane_tokens_split(&inner, STRLIT(","), split, 3) == 3);
    CHECK(csane_tokens_subview(&segment, &inner, split[1]));
    CHECK(test_view_first_is(&segment, STRLIT("second")));

    CHECK(csane_tokens_subview(&argument, &view, args[0]));
    CHECK(test_view_first_is(&argument, STRLIT("prefix")));
    CHECK(csane_tokens_subview(&argument, &view, args[1]));
    CHECK(test_view_first_is(&argument, STRLIT("second")));
    CHECK(csane_tokens_subview(&argument, &view, args[2]));
    CHECK(test_view_first_is(&argument, STRLIT("suffix")));
    CHECK(test_find_token(&tokens, STRLIT("first"), 0) < second);
    int32 comma = test_find_token(&tokens, STRLIT(","), 0);
    CHECK(csane_tokens_next(&view, comma) == second);

    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

static void
test_nested_conditional_path(void) {
    char *text =
        "call(prefix,\n"
        "#if OUTER\n"
        "    outer,\n"
        "#if INNER\n"
        "    inner_a,\n"
        "#else\n"
        "    inner_b,\n"
        "#endif\n"
        "#else\n"
        "    fallback,\n"
        "#endif\n"
        "    suffix);\n";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CsaneTokenPath path;
    CsaneTokenView view;
    CsaneTokenView argument;
    TokenRange args[4];
    int32 outer_hash = test_find_token(&tokens, STRLIT("#"), 0);
    int32 inner_hash = test_find_token(&tokens, STRLIT("#"), 1);
    int32 inner_endif_hash = test_find_token(&tokens, STRLIT("#"), 3);
    int32 outer = test_find_token(&tokens, STRLIT("outer"), 0);
    int32 inner_b = test_find_token(&tokens, STRLIT("inner_b"), 0);
    int32 suffix = test_find_token(&tokens, STRLIT("suffix"), 0);
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 close = test_find_token(&tokens, STRLIT(")"), 0);

    CHECK(outer_hash >= 0 && inner_hash >= 0 && inner_endif_hash >= 0);
    CHECK(csane_token_path_init(&path, &tokens,
                                (TokenRange){0, ARRAY_LEN(tokens.tokens)}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, outer_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){outer, inner_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){inner_b, inner_endif_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){suffix,
                                               ARRAY_LEN(tokens.tokens)}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_balanced(&view));
    CHECK(csane_tokens_match(&view, open) == close);
    CHECK(csane_tokens_arguments(&view, open, args, 4) == 4);

    CHECK(csane_tokens_subview(&argument, &view, args[0]));
    CHECK(test_view_first_is(&argument, STRLIT("prefix")));
    CHECK(csane_tokens_subview(&argument, &view, args[1]));
    CHECK(test_view_first_is(&argument, STRLIT("outer")));
    CHECK(csane_tokens_subview(&argument, &view, args[2]));
    CHECK(test_view_first_is(&argument, STRLIT("inner_b")));
    CHECK(csane_tokens_subview(&argument, &view, args[3]));
    CHECK(test_view_first_is(&argument, STRLIT("suffix")));

    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

static void
test_nested_arguments(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("call(a, nested(x, y), (Type){1, 2}, arr[i, j]);"),
                      TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    TokenRange ranges[4];
    TokenRange content;
    int32 close = -1;
    int32 open = csane_tokens_next(&view, 0);

    CHECK(csane_tokens_group(&view, open, &content, &close));
    CHECK(close > open);
    CHECK(csane_tokens_arguments(&view, open, ranges, 4) == 4);
    CHECK(test_range_text(&tokens, ranges[0], STRLIT("a")));
    CHECK(test_range_text(&tokens, ranges[1], STRLIT("nested(x, y)")));
    CHECK(test_range_text(&tokens, ranges[2], STRLIT("(Type){1, 2}")));
    CHECK(test_range_text(&tokens, ranges[3], STRLIT("arr[i, j]")));
    CHECK(csane_tokens_arguments(&view, open, ranges, 3) == -1);
    CHECK(csane_tokens_arguments(&view, open, NULL, 0) == -1);
    CHECK(csane_tokens_previous(&view, open) == 0);

    CsaneTokenView inner;
    CHECK(csane_tokens_subview(&inner, &view, content));
    CHECK(csane_tokens_balanced(&inner));
    CHECK(csane_tokens_split(&inner, STRLIT(","), ranges, 4) == 4);
    CHECK(csane_tokens_top_level(&inner, inner.range.first - 1,
                                 STRLIT(","))
          < inner.range.end);
    CHECK(csane_tokens_source(&inner).start
          == tokens.tokens[open].offset + tokens.tokens[open].len);
    CHECK(csane_tokens_match(&inner, open) == -1);

    CsaneTokenView clipped;
    CHECK(csane_tokens_subview(&clipped, &view,
                               (TokenRange){open, close}));
    CHECK(csane_tokens_arguments(&clipped, open, ranges, 4) == -1);
    CHECK(!csane_tokens_subview(&inner, &clipped,
                                (TokenRange){open, close + 1}));
    CHECK(inner.tokens == NULL);
    CHECK(!csane_tokens_subview(&inner, &view,
                                (TokenRange){-1, open}));
    free_tokenization(&tokens);
    return;
}

static void
test_empty_malformed_arguments(void) {
    StrFlex *cases[] = {
        SFLIT("f()"),
        SFLIT("f( /* comment */ )"),
        SFLIT("f(a,,b)"),
        SFLIT("f(a,)"),
        SFLIT("f(a, b"),
        SFLIT("f((a, b)"),
        SFLIT("f(a, b)"),
    };
    int32 expected[] = {0, 0, -1, -1, -1, -1, 2,};
    for (int32 i = 0; i < LENGTH(cases); i += 1) {
        Tokenization tokens = tokenize_copy(cases[i]->data, cases[i]->len,
                                            TOKENIZE_DEFAULT);
        CsaneTokenView view = csane_tokens_full(&tokens);
        TokenRange args[2];
        int32 open = csane_tokens_next(&view, 0);
        CHECK(csane_tokens_arguments(&view, open, args, 2) == expected[i]);
        free_tokenization(&tokens);
    }
    return;
}

static void
test_unwrap(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("  ((( value )))  "), TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    TokenRange range = csane_tokens_unwrap(&view, view.range);
    CHECK(test_range_text(&tokens, range, STRLIT("value")));
    CHECK(csane_tokens_unwrap(&view, range).first == range.first);
    CHECK(csane_tokens_unwrap(&view, range).end == range.end);
    free_tokenization(&tokens);
    return;
}

static void
test_assignments(void) {
    StrFlex *valid[] = {SFLIT("x = f(a == b, y += 1)"),
                        SFLIT("x <<= y + 1"), SFLIT("answer += (p = 2)")};
    StrFlex *operators[] = {SFLIT("="), SFLIT("<<="), SFLIT("+=")};
    StrFlex *left[] = {SFLIT("x"), SFLIT("x"), SFLIT("answer")};
    for (int32 i = 0; i < LENGTH(valid); i += 1) {
        Tokenization tokens = tokenize_copy(valid[i]->data, valid[i]->len,
                                            TOKENIZE_DEFAULT);
        CsaneTokenView view = csane_tokens_full(&tokens);
        TokenRange lhs;
        TokenRange rhs;
        int32 op = -1;
        CHECK(csane_tokens_assignment(&view, &lhs, &rhs, &op));
        CHECK(test_range_text(&tokens, lhs, left[i]->data, left[i]->len));
        CHECK(op >= 0 && STREQUAL(tokens.tokens[op].text,
                                  tokens.tokens[op].len,
                                  operators[i]->data, operators[i]->len));
        CHECK(rhs.first == op + 1);
        free_tokenization(&tokens);
    }
    StrFlex *invalid[] = {
        SFLIT("left == right"),
        SFLIT("f(x = 1, y)"),
        SFLIT("x = (y"),
        SFLIT("= x"),
        SFLIT("x = "),
    };
    for (int32 i = 0; i < LENGTH(invalid); i += 1) {
        Tokenization tokens = tokenize_copy(invalid[i]->data, invalid[i]->len,
                                            TOKENIZE_DEFAULT);
        CsaneTokenView view = csane_tokens_full(&tokens);
        CHECK(!csane_tokens_assignment(&view, NULL, NULL, NULL));
        free_tokenization(&tokens);
    }
    return;
}

static void
test_single_pass_scans(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("first, nested(a, b), last = value"),
                      TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    TokenRange parts[3];
    TokenRange lhs;
    TokenRange rhs;
    int32 comma = test_find_token(&tokens, STRLIT(","), 0);
    int32 second = test_find_token(&tokens, STRLIT(","), 2);
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 op = -1;

    CHECK(csane_tokens_top_level(&view, -1, STRLIT(",")) == comma);
    CHECK(csane_tokens_top_level(&view, comma, STRLIT(",")) == second);
    CHECK(csane_tokens_top_level(&view, -1, STRLIT("(")) == open);
    CHECK(csane_tokens_top_level(&view, -1, STRLIT(";")) == view.range.end);
    CHECK(csane_tokens_split(&view, STRLIT(","), parts, 3) == 3);
    CHECK(test_range_text(&tokens, parts[0], STRLIT("first")));
    CHECK(test_range_text(&tokens, parts[1], STRLIT("nested(a, b)")));
    CHECK(test_range_text(&tokens, parts[2], STRLIT("last = value")));
    CHECK(csane_tokens_assignment(&view, &lhs, &rhs, &op));
    CHECK(op == test_find_token(&tokens, STRLIT("="), 0));
    CHECK(test_range_text(&tokens, rhs, STRLIT("value")));
    free_tokenization(&tokens);

    StrFlex *malformed[] = {
        SFLIT("first, second, (bad]"),
        SFLIT("first, second, (missing"),
        SFLIT("left = right)"),
        SFLIT("left = right, (bad]"),
        SFLIT("left = right, (unclosed"),
        SFLIT("left = right, [bad)"),
    };
    for (int32 i = 0; i < LENGTH(malformed); i += 1) {
        Tokenization bad =
            tokenize_copy(malformed[i]->data, malformed[i]->len,
                          TOKENIZE_DEFAULT);
        CsaneTokenView invalid = csane_tokens_full(&bad);
        TokenRange output[3];
        CHECK(!csane_tokens_balanced(&invalid));
        CHECK(csane_tokens_top_level(&invalid, -1, STRLIT(",")) == -1);
        CHECK(csane_tokens_split(&invalid, STRLIT(","), output, 3) == -1);
        CHECK(!csane_tokens_assignment(&invalid, NULL, NULL, NULL));
        free_tokenization(&bad);
    }

    Tokenization empty =
        tokenize_copy(STRLIT(" /* only trivia */ "), TOKENIZE_DEFAULT);
    CsaneTokenView blank = csane_tokens_full(&empty);
    CHECK(csane_tokens_split(&blank, STRLIT(","), NULL, 0) == 0);
    CHECK(csane_tokens_top_level(&blank, -1, STRLIT(",")) == blank.range.end);
    free_tokenization(&empty);
    return;
}

static void
test_single_pass_path(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("start = value, (broken], other = data, "
                             "call(x, y)"), TOKENIZE_DEFAULT);
    CsaneTokenPath path;
    CsaneTokenView raw = csane_tokens_full(&tokens);
    CsaneTokenView view;
    CsaneTokenView segment;
    TokenRange parts[3];
    int32 broken = test_find_token(&tokens, STRLIT("("), 0);
    int32 other = test_find_token(&tokens, STRLIT("other"), 0);
    int32 first_comma = test_find_token(&tokens, STRLIT(","), 0);
    int32 first_assign = test_find_token(&tokens, STRLIT("="), 0);
    int32 op = -1;
    int32 end = ARRAY_LEN(tokens.tokens);

    CHECK(!csane_tokens_balanced(&raw));
    CHECK(csane_tokens_top_level(&raw, -1, STRLIT(",")) == -1);
    CHECK(csane_tokens_split(&raw, STRLIT(","), parts, 3) == -1);
    CHECK(!csane_tokens_assignment(&raw, NULL, NULL, NULL));
    CHECK(csane_token_path_init(&path, &tokens,
                                (TokenRange){0, ARRAY_LEN(tokens.tokens)}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, broken}));
    CHECK(csane_token_path_append(&path, (TokenRange){other, end}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_balanced(&view));
    CHECK(csane_tokens_top_level(&view, -1, STRLIT(",")) == first_comma);
    CHECK(csane_tokens_split(&view, STRLIT(","), parts, 3) == 3);
    CHECK(csane_tokens_subview(&segment, &view, parts[1]));
    CHECK(test_view_first_is(&segment, STRLIT("other")));
    CHECK(csane_tokens_assignment(&view, NULL, NULL, &op));
    CHECK(op == first_assign);
    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

static void
test_bounded_expected_strings(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("alpha beta alpha"), TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    char expected[] = {'a', 'l', 'p', 'h', 'a', 'X',};
    CHECK(test_find_token(&tokens, expected, 5, 0) == 0);
    CHECK(test_find_token(&tokens, expected, 5, 1) >= 0);
    CHECK(test_find_token(&tokens, expected, 3, 0) == -1);
    CHECK(test_find_token(&tokens, expected, 6, 0) == -1);
    CHECK(test_view_first_is(&view, expected, 5));
    CHECK(!test_view_first_is(&view, expected, 6));
    CHECK(test_range_text(&tokens, (TokenRange){0, 1}, expected, 5));
    CHECK(!test_range_text(&tokens, (TokenRange){0, 1}, expected, 6));
    free_tokenization(&tokens);

    char source[] = {'o', 'n', 'e', ' ', 't', 'w', 'o', 'X',};
    Tokenization bounded = tokenize_copy(source, 7, TOKENIZE_DEFAULT);
    CHECK(test_find_token(&bounded, STRLIT("one"), 0) >= 0);
    CHECK(test_find_token(&bounded, STRLIT("two"), 0) >= 0);
    CHECK(test_find_token(&bounded, STRLIT("X"), 0) == -1);
    free_tokenization(&bounded);
    return;
}

/* Reference navigation intentionally scans every segment, including segments
 * outside the subview. Compare cursors at and beyond all boundaries. */
static int32
test_path_next_reference(CsaneTokenView *view, int32 cursor) {
    for (int32 i = 0; i < ARRAY_LEN(view->path->parts); i += 1) {
        TokenRange part = view->path->parts[i];
        if (part.first < view->range.first) {
            part.first = view->range.first;
        }
        if (part.end > view->range.end) {
            part.end = view->range.end;
        }
        if (part.first >= part.end || cursor >= part.end) {
            continue;
        }
        int32 index = test_range_next_reference(view->tokens, part, cursor);
        if (index < part.end) {
            return index;
        }
    }
    return view->range.end;
}

static int32
test_path_previous_reference(CsaneTokenView *view, int32 cursor) {
    for (int32 i = ARRAY_LEN(view->path->parts) - 1; i >= 0; i -= 1) {
        TokenRange part = view->path->parts[i];
        if (part.first < view->range.first) {
            part.first = view->range.first;
        }
        if (part.end > view->range.end) {
            part.end = view->range.end;
        }
        if (part.first >= part.end || cursor <= part.first) {
            continue;
        }
        int32 index = test_range_previous_reference(view->tokens, part, cursor);
        if (index >= part.first) {
            return index;
        }
    }
    return view->range.first - 1;
}

static bool
test_path_contains_reference(CsaneTokenView *view, int32 index) {
    if (index < view->range.first || index >= view->range.end) {
        return false;
    }
    for (int32 i = 0; i < ARRAY_LEN(view->path->parts); i += 1) {
        TokenRange part = view->path->parts[i];
        if (index >= part.first && index < part.end) {
            return true;
        }
    }
    return false;
}

/* Delimiter oracle scans tokens directly and does not call matching or
 * significant-token navigation helpers. Positive kinds open, negative close. */
static int32
test_delimiter_kind_reference(Token *token) {
    if (token->len != 1) {
        return 0;
    }
    switch (token->text[0]) {
    case '(':
        return 1;
    case '[':
        return 2;
    case '{':
        return 3;
    case ')':
        return -1;
    case ']':
        return -2;
    case '}':
        return -3;
    default:
        return 0;
    }
}

static bool
test_delimiter_visible_reference(CsaneTokenView *view, int32 index) {
    if (view == NULL || view->tokens == NULL
        || index < view->range.first || index >= view->range.end) {
        return false;
    }
    if (view->path == NULL) {
        return true;
    }
    return test_path_contains_reference(view, index);
}

static int32
test_delimiter_match_reference(CsaneTokenView *view, int32 index) {
    int32 *stack = NULL;
    int32 depth = 0;
    int32 result = -1;
    int32 first_kind;
    int32 step;

    if (!test_delimiter_visible_reference(view, index)) {
        return -1;
    }
    first_kind = test_delimiter_kind_reference(&view->tokens->tokens[index]);
    if (first_kind == 0) {
        return -1;
    }
    step = 1;
    if (first_kind < 0) {
        step = -1;
    }
    ARRAY_INIT_COUNT(stack, view->range.end - view->range.first);
    for (int32 i = index; (i >= view->range.first)
                        && (i < view->range.end); i += step) {
        int32 kind;

        if (!test_delimiter_visible_reference(view, i)) {
            continue;
        }
        kind = test_delimiter_kind_reference(&view->tokens->tokens[i]);
        if (kind == 0) {
            continue;
        }
        if ((kind > 0) == (step > 0)) {
            stack[depth] = kind*step;
            depth += 1;
            continue;
        }
        if (depth == 0 || stack[depth - 1] != -kind*step) {
            break;
        }
        depth -= 1;
        if (depth == 0) {
            result = i;
            break;
        }
    }
    ARRAY_FREE(stack);
    return result;
}

/* The index is conservative on malformed input: recorded pairs must be
 * valid in both directions, even if some recoverable pairs remain unindexed. */
static void
test_delimiter_index(Tokenization *tokens) {
    int32 count = ARRAY_LEN(tokens->tokens);
    CsaneTokenView full = csane_tokens_full(tokens);
    bool balanced = token_range_is_balanced(tokens, full.range);

    CHECK(ARRAY_LEN(tokens->matching_delimiter) == count);
    if (ARRAY_LEN(tokens->matching_delimiter) != count) {
        return;
    }
    for (int32 i = 0; i < count; i += 1) {
        int32 match = tokens->matching_delimiter[i];
        int32 kind = test_delimiter_kind_reference(&tokens->tokens[i]);
        int32 expected = test_delimiter_match_reference(&full, i);

        if (kind == 0) {
            CHECK(match == -1);
        }
        CHECK(match >= -1 && match < count);
        if (match >= 0 && match < count) {
            int32 other = test_delimiter_kind_reference(&tokens->tokens[match]);

            CHECK(tokens->matching_delimiter[match] == i);
            CHECK(other == -kind);
            CHECK(expected == match);
        }
        if (balanced) {
            CHECK(match == expected);
        }
    }
    return;
}

static void
test_delimiter_index_modes(void) {
    StrFlex *sources[] = {
        SFLIT(""),
        SFLIT("  /* just trivia */  "),
        SFLIT("(x[2] + {y})"),
        SFLIT("([)] {}"),
        SFLIT("'(' \"[)\" /* { ] */ (x)"),
        SFLIT("  #define X (broken]\n"),
    };
    int32 flags[] = {
        TOKENIZE_DEFAULT,
        TOKENIZE_SKIP_WHITESPACE,
        TOKENIZE_PREPROCESSOR_LINES,
        TOKENIZE_PREPROCESSOR_LINES | TOKENIZE_SKIP_WHITESPACE,
    };
    Tokenization malformed;

    for (int32 mode = 0; mode < LENGTH(flags); mode += 1) {
        for (int32 sample = 0; sample < LENGTH(sources); sample += 1) {
            Tokenization tokens =
                tokenize_copy(sources[sample]->data, sources[sample]->len,
                              flags[mode]);
            test_delimiter_index(&tokens);
            free_tokenization(&tokens);
            CHECK(tokens.matching_delimiter == NULL);
        }
    }

    malformed = tokenize_copy(STRLIT("([)] {}"), TOKENIZE_DEFAULT);
    CHECK(malformed.matching_delimiter[0] == -1);
    CHECK(malformed.matching_delimiter[1] == -1);
    CHECK(malformed.matching_delimiter[2] == -1);
    CHECK(malformed.matching_delimiter[3] == -1);
    CHECK(malformed.matching_delimiter[5] == 6);
    CHECK(malformed.matching_delimiter[6] == 5);
    free_tokenization(&malformed);
    return;
}

static void
test_delimiter_match_view(CsaneTokenView *view) {
    for (int32 i = view->range.first - 1; i <= view->range.end; i += 1) {
        int32 expected = test_delimiter_match_reference(view, i);
        CHECK(csane_tokens_match(view, i) == expected);
        if (view->path == NULL) {
            int32 forward = -1;
            int32 reverse = -1;

            if (test_delimiter_visible_reference(view, i)) {
                Token *token = &view->tokens->tokens[i];
                int32 kind = test_delimiter_kind_reference(token);
                if (kind > 0) {
                    forward = expected;
                } else if (kind < 0) {
                    reverse = expected;
                }
            }
            CHECK(token_range_matching_delimiter_forward(view->tokens,
                                                         view->range, i)
                  == forward);
            CHECK(token_range_matching_delimiter_reverse(view->tokens,
                                                         view->range, i)
                  == reverse);
        }
    }
    return;
}

static void
test_delimiter_match_ranges(void) {
    StrFlex *sources[] = {
        SFLIT(""),
        SFLIT("  /* no delimiters */  "),
        SFLIT("() [] {}"),
        SFLIT("call(a[0], (b + (C){.x = 1}));"),
        SFLIT("(a) (unclosed"),
        SFLIT("(a) ]"),
        SFLIT(") (a)"),
        SFLIT("(] (b)"),
        SFLIT("[)"),
        SFLIT("([)]"),
        SFLIT("({[a]})"),
        SFLIT("((a) [b {c}]) )"),
        SFLIT("'(' \"( [ { ) ] }\" /* [({ })] */ (ok)"),
        SFLIT("#if X\n(a)\n#else\n[b]\n#endif\n"),
    };
    int32 flags[] = {
        TOKENIZE_DEFAULT,
        TOKENIZE_SKIP_WHITESPACE,
        TOKENIZE_PREPROCESSOR_LINES,
    };

    for (int32 mode = 0; mode < LENGTH(flags); mode += 1) {
        for (int32 sample = 0; sample < LENGTH(sources); sample += 1) {
            Tokenization tokens =
                tokenize_copy(sources[sample]->data, sources[sample]->len,
                              flags[mode]);
            int32 count = ARRAY_LEN(tokens.tokens);

            test_delimiter_index(&tokens);
            for (int32 first = 0; first <= count; first += 1) {
                for (int32 end = first; end <= count; end += 1) {
                    CsaneTokenView view;
                    CHECK(csane_tokens_view(&view, &tokens,
                                            (TokenRange){first, end}));
                    test_delimiter_match_view(&view);
                }
            }
            free_tokenization(&tokens);
        }
    }
    return;
}

/* Indexed lookup, clipped ranges, and unavailable/unpaired index fallback. */
static void
test_delimiter_match_index_fallback(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("before (a[b]{c}) after"), TOKENIZE_DEFAULT);
    TokenRange all = {0, ARRAY_LEN(tokens.tokens)};
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 close = test_find_token(&tokens, STRLIT(")"), 0);
    int32 bracket = test_find_token(&tokens, STRLIT("["), 0);
    int32 bracket_close = test_find_token(&tokens, STRLIT("]"), 0);
    TokenRange bracket_range = {bracket, bracket_close + 1};
    int32 saved_open = tokens.matching_delimiter[open];
    int32 saved_close = tokens.matching_delimiter[close];
    int32 *index = tokens.matching_delimiter;

    CHECK(saved_open == close);
    CHECK(saved_close == open);
    CHECK(token_range_matching_delimiter_forward(&tokens, all, open) == close);
    CHECK(token_range_matching_delimiter_reverse(&tokens, all, close) == open);
    CHECK(token_range_matching_delimiter_forward(&tokens,
                                                 (TokenRange){open, close},
                                                 open) == -1);
    CHECK(token_range_matching_delimiter_reverse(&tokens,
                                                 (TokenRange){open + 1,
                                                              close + 1},
                                                 close) == -1);
    CHECK(token_range_matching_delimiter_forward(&tokens, bracket_range,
                                                 bracket) == bracket_close);

    tokens.matching_delimiter[open] = -1;
    tokens.matching_delimiter[close] = -1;
    CHECK(token_range_matching_delimiter_forward(&tokens, all, open) == close);
    CHECK(token_range_matching_delimiter_reverse(&tokens, all, close) == open);
    tokens.matching_delimiter[open] = saved_open;
    tokens.matching_delimiter[close] = saved_close;

    tokens.matching_delimiter = NULL;
    CHECK(token_range_matching_delimiter_forward(&tokens, all, open) == close);
    CHECK(token_range_matching_delimiter_reverse(&tokens, all, close) == open);
    tokens.matching_delimiter = index;

    free_tokenization(&tokens);
    return;
}

static void
test_delimiter_match_invalid_ranges(void) {
    Tokenization tokens = tokenize_copy(STRLIT("(x)"), TOKENIZE_DEFAULT);
    TokenRange invalid[] = {
        {-1, 1},
        {0, ARRAY_LEN(tokens.tokens) + 1},
        {ARRAY_LEN(tokens.tokens), 0},
    };

    for (int32 i = 0; i < LENGTH(invalid); i += 1) {
        CHECK(token_range_matching_delimiter_forward(&tokens, invalid[i], 0)
              == -1);
        CHECK(token_range_matching_delimiter_reverse(&tokens, invalid[i], 0)
              == -1);
    }
    CHECK(token_range_matching_delimiter_forward(NULL, (TokenRange){0, 0}, 0)
          == -1);
    CHECK(token_range_matching_delimiter_reverse(NULL, (TokenRange){0, 0}, 0)
          == -1);
    free_tokenization(&tokens);
    return;
}

static void
test_delimiter_match_combinations(void) {
    char alphabet[] = "()[]{}";
    int32 combinations = LENGTH(alphabet) - 1;

    /* Every delimiter-only string of length 1 through 4, including errors. */
    for (int32 length = 1; length <= 4; length += 1) {
        for (int32 sample = 0; sample < combinations; sample += 1) {
            char source[5] = {0};
            int32 value = sample;
            Tokenization tokens;
            int32 count;

            for (int32 i = 0; i < length; i += 1) {
                source[i] = alphabet[value % 6];
                value /= 6;
            }
            tokens = tokenize_copy(source, length, TOKENIZE_DEFAULT);
            count = ARRAY_LEN(tokens.tokens);

            test_delimiter_index(&tokens);
            for (int32 first = 0; first <= count; first += 1) {
                for (int32 end = first; end <= count; end += 1) {
                    CsaneTokenView view;
                    CHECK(csane_tokens_view(&view, &tokens,
                                            (TokenRange){first, end}));
                    test_delimiter_match_view(&view);
                }
            }
            free_tokenization(&tokens);
        }
        combinations *= 6;
    }
    return;
}

static void
test_delimiter_match_paths(void) {
    char *source =
        "call(prefix,\n"
        "#if FLAG\n"
        "    (bad],\n"
        "#else\n"
        "    nested([kept], {x}),\n"
        "#endif\n"
        "    suffix);\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source),
                                        TOKENIZE_DEFAULT);
    int32 count = ARRAY_LEN(tokens.tokens);
    int32 first_hash = test_find_token(&tokens, STRLIT("#"), 0);
    int32 endif_hash = test_find_token(&tokens, STRLIT("#"), 2);
    int32 kept = test_find_token(&tokens, STRLIT("nested"), 0);
    int32 suffix = test_find_token(&tokens, STRLIT("suffix"), 0);
    int32 outer_open = test_find_token(&tokens, STRLIT("("), 0);
    int32 outer_close = test_find_token(&tokens, STRLIT(")"), 1);
    CsaneTokenPath path;
    CsaneTokenView view;
    CsaneTokenView full = csane_tokens_full(&tokens);

    CHECK(first_hash >= 0 && endif_hash >= 0 && kept >= 0 && suffix >= 0);
    CHECK(outer_open >= 0 && outer_close >= 0);
    CHECK(csane_tokens_match(&full, outer_open) == -1);
    CHECK(csane_token_path_init(&path, &tokens, (TokenRange){0, count}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, first_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){kept, endif_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){suffix, count}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_match(&view, outer_open) == outer_close);
    CHECK(csane_tokens_match(&view, outer_close) == outer_open);
    test_delimiter_match_view(&view);
    for (int32 first = 0; first <= count; first += 1) {
        for (int32 end = first; end <= count; end += 1) {
            CsaneTokenView subview;

            CHECK(csane_tokens_subview(&subview, &view,
                                       (TokenRange){first, end}));
            test_delimiter_match_view(&subview);
        }
    }
    csane_token_path_free(&path);
    free_tokenization(&tokens);

    return;
}

static void
test_delimiter_match_filtered_path(void) {
    Tokenization malformed =
        tokenize_copy(STRLIT("([)]"), TOKENIZE_DEFAULT);
    CsaneTokenView raw = csane_tokens_full(&malformed);
    CsaneTokenPath filtered;
    int32 paren_open = test_find_token(&malformed, STRLIT("("), 0);
    int32 bracket_open = test_find_token(&malformed, STRLIT("["), 0);
    int32 paren_close = test_find_token(&malformed, STRLIT(")"), 0);
    int32 bracket_close = test_find_token(&malformed, STRLIT("]"), 0);

    CHECK(csane_tokens_match(&raw, paren_open) == -1);
    CHECK(csane_token_path_init(&filtered, &malformed,
                                (TokenRange){0, ARRAY_LEN(malformed.tokens)}));
    CHECK(csane_token_path_append(&filtered,
                                  (TokenRange){paren_open, bracket_open}));
    CHECK(csane_token_path_append(&filtered,
                                  (TokenRange){paren_close, bracket_close}));
    CHECK(csane_tokens_path_view(&raw, &filtered));
    CHECK(csane_tokens_match(&raw, paren_open) == paren_close);
    CHECK(csane_tokens_match(&raw, paren_close) == paren_open);
    test_delimiter_match_view(&raw);
    csane_token_path_free(&filtered);
    free_tokenization(&malformed);
    return;
}

/* A one-part token path must behave exactly like its clipped physical range. */
static void
test_delimiter_match_single_path_part(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("x(a[b]+{c})y(d)"), TOKENIZE_DEFAULT);
    int32 count = ARRAY_LEN(tokens.tokens);
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 close = test_find_token(&tokens, STRLIT(")"), 0);
    int32 other_open = test_find_token(&tokens, STRLIT("("), 1);
    CsaneTokenPath path;
    CsaneTokenView view;
    CsaneTokenView clipped;
    int32 *index = tokens.matching_delimiter;

    CHECK(open >= 0 && close > open && other_open > close);
    CHECK(csane_token_path_init(&path, &tokens, (TokenRange){0, count}));
    CHECK(csane_token_path_append(&path, (TokenRange){open, close + 1}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_match(&view, open) == close);
    CHECK(csane_tokens_match(&view, close) == open);
    CHECK(csane_tokens_match(&view, other_open) == -1);
    test_delimiter_match_view(&view);

    CHECK(csane_tokens_subview(&clipped, &view,
                               (TokenRange){open, close}));
    CHECK(csane_tokens_match(&clipped, open) == -1);
    CHECK(csane_tokens_subview(&clipped, &view,
                               (TokenRange){open + 1, close + 1}));
    CHECK(csane_tokens_match(&clipped, close) == -1);

    /* A missing index must still use the bounded matching scanner. */
    tokens.matching_delimiter = NULL;
    CHECK(csane_tokens_match(&view, open) == close);
    CHECK(csane_tokens_match(&view, close) == open);
    tokens.matching_delimiter = index;

    for (int32 first = 0; first <= count; first += 1) {
        for (int32 end = first; end <= count; end += 1) {
            CHECK(csane_tokens_subview(&clipped, &view,
                                       (TokenRange){first, end}));
            test_delimiter_match_view(&clipped);
        }
    }
    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

/* Paths with gaps must not trust physical pairs across excluded tokens. */
static void
test_delimiter_match_path_masks(void) {
    StrFlex *sources[] = {
        SFLIT("([])"),
        SFLIT("([)]"),
        SFLIT("{[()]}"),
    };

    for (int32 sample = 0; sample < LENGTH(sources); sample += 1) {
        Tokenization tokens =
            tokenize_copy(sources[sample]->data, sources[sample]->len,
                          TOKENIZE_DEFAULT);
        int32 count = ARRAY_LEN(tokens.tokens);
        CHECK(count <= 6);
        for (int32 mask = 0; mask < (1 << count); mask += 1) {
            CsaneTokenPath path;
            CsaneTokenView view;

            CHECK(csane_token_path_init(&path, &tokens,
                                        (TokenRange){0, count}));
            for (int32 first = 0; first < count;) {
                int32 end;
                if ((mask & (1 << first)) == 0) {
                    first += 1;
                    continue;
                }
                end = first + 1;
                while (end < count && (mask & (1 << end)) != 0) {
                    end += 1;
                }
                CHECK(csane_token_path_append(&path,
                                              (TokenRange){first, end}));
                first = end;
            }
            CHECK(csane_tokens_path_view(&view, &path));
            for (int32 first = 0; first <= count; first += 1) {
                for (int32 end = first; end <= count; end += 1) {
                    CsaneTokenView clipped;
                    CHECK(csane_tokens_subview(&clipped, &view,
                                               (TokenRange){first, end}));
                    test_delimiter_match_view(&clipped);
                }
            }
            csane_token_path_free(&path);
        }
        free_tokenization(&tokens);
    }
    return;
}

static void
test_path_navigation(void) {
    Tokenization tokens =
        tokenize_copy(STRLIT("a /* comment */ b + c // ignore\n"
                             "d (e, f) + g /* block */ h + i;"),
                      TOKENIZE_DEFAULT);
    for (int32 stride = 0; stride <= 8; stride += 1) {
        CsaneTokenPath path;
        CsaneTokenView view;
        CHECK(csane_token_path_init(&path, &tokens,
                                    (TokenRange){0, ARRAY_LEN(tokens.tokens)}));
        if (stride > 0) {
            int32 start = stride > 2 ? stride/2 : 0;
            for (int32 i = start; i < ARRAY_LEN(tokens.tokens); i += stride) {
                CHECK(csane_token_path_append(&path, (TokenRange){i, i + 1}));
            }
        }
        CHECK(csane_tokens_path_view(&view, &path));
        for (int32 first = 0; first <= ARRAY_LEN(tokens.tokens); first += 1) {
            for (int32 end = first; end <= ARRAY_LEN(tokens.tokens); end += 1) {
                CsaneTokenView subview;
                CHECK(csane_tokens_subview(&subview, &view,
                                           (TokenRange){first, end}));
                for (int32 cursor = first - 2; cursor <= end + 2; cursor += 1) {
                    CHECK(csane_tokens_next(&subview, cursor)
                          == test_path_next_reference(&subview, cursor));
                    CHECK(csane_tokens_previous(&subview, cursor)
                          == test_path_previous_reference(&subview, cursor));
                    CHECK(csane_tokens_contains(&subview, cursor)
                          == test_path_contains_reference(&subview, cursor));
                }
            }
        }
        csane_token_path_free(&path);
    }
    free_tokenization(&tokens);
    return;
}

static void
test_preprocessor_path_navigation(void) {
    char *source =
        "start;\n"
        "#if FLAG\n"
        "discarded_a + discarded_b;\n"
        "#else\n"
        "kept_a /* comment */ + kept_b;\n"
        "#endif\n"
        "finish;\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source),
                                        TOKENIZE_DEFAULT);
    int32 count = ARRAY_LEN(tokens.tokens);
    int32 first_hash = test_find_token(&tokens, STRLIT("#"), 0);
    int32 endif_hash = test_find_token(&tokens, STRLIT("#"), 2);
    int32 discarded = test_find_token(&tokens, STRLIT("discarded_a"), 0);
    int32 kept = test_find_token(&tokens, STRLIT("kept_a"), 0);
    int32 finish = test_find_token(&tokens, STRLIT("finish"), 0);
    CsaneTokenPath path;
    CsaneTokenView view;

    CHECK(first_hash >= 0 && endif_hash >= 0 && discarded >= 0);
    CHECK(kept >= 0 && finish >= 0);
    CHECK(csane_token_path_init(&path, &tokens, (TokenRange){0, count}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, first_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){first_hash, first_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){kept, endif_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){endif_hash, endif_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){finish, count}));
    CHECK(ARRAY_LEN(path.parts) == 3);
    CHECK(csane_tokens_path_view(&view, &path));

    /* Global indices include discarded tokens, but path navigation must not. */
    CHECK(tokenization_next_significant(&tokens, first_hash - 1) == first_hash);
    CHECK(csane_tokens_next(&view, first_hash - 1) == kept);
    CHECK(!csane_tokens_contains(&view, discarded));
    CHECK(csane_tokens_contains(&view, kept));
    CHECK(csane_tokens_contains(&view, finish));

    for (int32 first = 0; first <= count; first += 1) {
        for (int32 end = first; end <= count; end += 1) {
            CsaneTokenView subview;
            CHECK(csane_tokens_subview(&subview, &view,
                                       (TokenRange){first, end}));
            for (int32 cursor = first - 2; cursor <= end + 2; cursor += 1) {
                CHECK(csane_tokens_next(&subview, cursor)
                      == test_path_next_reference(&subview, cursor));
                CHECK(csane_tokens_previous(&subview, cursor)
                      == test_path_previous_reference(&subview, cursor));
                CHECK(csane_tokens_contains(&subview, cursor)
                      == test_path_contains_reference(&subview, cursor));
            }
        }
    }

    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

int
main(void) {
    test_bounded_expected_strings();
    test_significant_navigation();
    test_significant_index_modes();
    test_nested_arguments();
    test_empty_malformed_arguments();
    test_unwrap();
    test_assignments();
    test_single_pass_scans();
    test_single_pass_path();
    test_conditional_path();
    test_nested_conditional_path();
    test_path_navigation();
    test_preprocessor_path_navigation();
    test_delimiter_index_modes();
    test_delimiter_match_ranges();
    test_delimiter_match_index_fallback();
    test_delimiter_match_invalid_ranges();
    test_delimiter_match_combinations();
    test_delimiter_match_paths();
    test_delimiter_match_filtered_path();
    test_delimiter_match_single_path_part();
    test_delimiter_match_path_masks();
    return failures > 0;
}

#endif /* TESTING_token_utils_test */
