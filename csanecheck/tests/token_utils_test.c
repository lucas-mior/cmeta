// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck/csck_tokens.c"

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
    for (int32 i = 0; i < tokens->token_count; i += 1) {
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
    int32 first_hash = test_find_token(&tokens, STRLIT("#"), 0);
    int32 endif_hash = test_find_token(&tokens, STRLIT("#"), 2);
    int32 second = test_find_token(&tokens, STRLIT("second"), 0);
    int32 suffix = test_find_token(&tokens, STRLIT("suffix"), 0);
    int32 open = test_find_token(&tokens, STRLIT("("), 0);
    int32 close = test_find_token(&tokens, STRLIT(")"), 0);

    CHECK(first_hash >= 0 && endif_hash >= 0 && second >= 0 && suffix >= 0);
    CHECK(csane_token_path_init(&path, &tokens,
                                (TokenRange){0, tokens.token_count}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, first_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){second, endif_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){suffix, tokens.token_count}));
    CHECK(csane_tokens_path_view(&view, &path));
    CHECK(csane_tokens_balanced(&view));
    CHECK(csane_tokens_match(&view, open) == close);
    CHECK(csane_tokens_arguments(&view, open, args, 3) == 3);

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
                                (TokenRange){0, tokens.token_count}));
    CHECK(csane_token_path_append(&path, (TokenRange){0, outer_hash}));
    CHECK(csane_token_path_append(&path, (TokenRange){outer, inner_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){inner_b, inner_endif_hash}));
    CHECK(csane_token_path_append(&path,
                                  (TokenRange){suffix, tokens.token_count}));
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
    for (int32 i = 0; i < view->path->count; i += 1) {
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
        int32 index = token_range_next_significant(view->tokens, part, cursor);
        if (index < part.end) {
            return index;
        }
    }
    return view->range.end;
}

static int32
test_path_previous_reference(CsaneTokenView *view, int32 cursor) {
    for (int32 i = view->path->count - 1; i >= 0; i -= 1) {
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
        int32 index =
            token_range_previous_significant(view->tokens, part, cursor);
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
    for (int32 i = 0; i < view->path->count; i += 1) {
        TokenRange part = view->path->parts[i];
        if (index >= part.first && index < part.end) {
            return true;
        }
    }
    return false;
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
                                    (TokenRange){0, tokens.token_count}));
        if (stride > 0) {
            int32 start = stride > 2 ? stride/2 : 0;
            for (int32 i = start; i < tokens.token_count; i += stride) {
                CHECK(csane_token_path_append(&path, (TokenRange){i, i + 1}));
            }
        }
        CHECK(csane_tokens_path_view(&view, &path));
        for (int32 first = 0; first <= tokens.token_count; first += 1) {
            for (int32 end = first; end <= tokens.token_count; end += 1) {
                CsaneTokenView subview;
                CHECK(csane_tokens_subview(&subview, &view,
                                           (TokenRange){first, end}));
                for (int32 cursor = first - 2; cursor <= end + 2;
                     cursor += 1) {
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

int
main(void) {
    test_bounded_expected_strings();
    test_nested_arguments();
    test_empty_malformed_arguments();
    test_unwrap();
    test_assignments();
    test_conditional_path();
    test_nested_conditional_path();
    test_path_navigation();
    return failures > 0;
}
