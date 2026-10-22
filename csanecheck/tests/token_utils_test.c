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
        fprintf(stderr, "token_utils_test.c:%d: failed\n", line);
        failures += 1;
    }
    return;
}

#define CHECK(CONDITION) test_check((CONDITION), __LINE__)

static bool
test_range_text(Tokenization *tokens, TokenRange range, char *expected) {
    range = token_range_trim_trivia(tokens, range);
    SourceRange source = token_range_source_range(tokens, range);
    int32 len = strlen32(expected);
    return source.end - source.start == len
           && !memcmp64(tokens->text + source.start, expected, len);
}


static int32
test_find_token(Tokenization *tokens, char *text, int32 occurrence) {
    int32 found = 0;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        if (!TOKEN_IS(&tokens->tokens[i], text)) {
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
test_view_first_is(CsaneTokenView *view, char *text) {
    int32 first = csane_tokens_next(view, view->range.first - 1);
    return first >= view->range.first && first < view->range.end
           && TOKEN_IS(&view->tokens->tokens[first], text);
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
    int32 first_hash = test_find_token(&tokens, "#", 0);
    int32 endif_hash = test_find_token(&tokens, "#", 2);
    int32 second = test_find_token(&tokens, "second", 0);
    int32 suffix = test_find_token(&tokens, "suffix", 0);
    int32 open = test_find_token(&tokens, "(", 0);
    int32 close = test_find_token(&tokens, ")", 0);

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
    CHECK(test_view_first_is(&argument, "prefix"));
    CHECK(csane_tokens_subview(&argument, &view, args[1]));
    CHECK(test_view_first_is(&argument, "second"));
    CHECK(csane_tokens_subview(&argument, &view, args[2]));
    CHECK(test_view_first_is(&argument, "suffix"));
    CHECK(test_find_token(&tokens, "first", 0) < second);
    CHECK(csane_tokens_next(&view, test_find_token(&tokens, ",", 0)) == second);

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
    int32 outer_hash = test_find_token(&tokens, "#", 0);
    int32 inner_hash = test_find_token(&tokens, "#", 1);
    int32 inner_endif_hash = test_find_token(&tokens, "#", 3);
    int32 outer = test_find_token(&tokens, "outer", 0);
    int32 inner_b = test_find_token(&tokens, "inner_b", 0);
    int32 suffix = test_find_token(&tokens, "suffix", 0);
    int32 open = test_find_token(&tokens, "(", 0);
    int32 close = test_find_token(&tokens, ")", 0);

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
    CHECK(test_view_first_is(&argument, "prefix"));
    CHECK(csane_tokens_subview(&argument, &view, args[1]));
    CHECK(test_view_first_is(&argument, "outer"));
    CHECK(csane_tokens_subview(&argument, &view, args[2]));
    CHECK(test_view_first_is(&argument, "inner_b"));
    CHECK(csane_tokens_subview(&argument, &view, args[3]));
    CHECK(test_view_first_is(&argument, "suffix"));

    csane_token_path_free(&path);
    free_tokenization(&tokens);
    return;
}

static void
test_nested_arguments(void) {
    char *text = "call(a, nested(x, y), (Type){1, 2}, arr[i, j]);";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    TokenRange ranges[4];
    TokenRange content;
    int32 close = -1;
    int32 open = csane_tokens_next(&view, 0);

    CHECK(csane_tokens_group(&view, open, &content, &close));
    CHECK(close > open);
    CHECK(csane_tokens_arguments(&view, open, ranges, 4) == 4);
    CHECK(test_range_text(&tokens, ranges[0], "a"));
    CHECK(test_range_text(&tokens, ranges[1], "nested(x, y)"));
    CHECK(test_range_text(&tokens, ranges[2], "(Type){1, 2}"));
    CHECK(test_range_text(&tokens, ranges[3], "arr[i, j]"));
    CHECK(csane_tokens_arguments(&view, open, ranges, 3) == -1);
    CHECK(csane_tokens_arguments(&view, open, NULL, 0) == -1);
    CHECK(csane_tokens_previous(&view, open) == 0);

    CsaneTokenView inner;
    CHECK(csane_tokens_subview(&inner, &view, content));
    CHECK(csane_tokens_balanced(&inner));
    CHECK(csane_tokens_split(&inner, ",", 1, ranges, 4) == 4);
    CHECK(csane_tokens_top_level(&inner, inner.range.first - 1, ",", 1)
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
    char *cases[] = {
        "f()",
        "f( /* comment */ )",
        "f(a,,b)",
        "f(a,)",
        "f(a, b",
        "f((a, b)",
        "f(a, b)",
    };
    int32 expected[] = {0, 0, -1, -1, -1, -1, 2,};
    for (int32 i = 0; i < SIZEOF(cases)/SIZEOF(cases[0]); i += 1) {
        Tokenization tokens = tokenize_copy(cases[i], strlen32(cases[i]),
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
    char *text = "  ((( value )))  ";
    Tokenization tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    CsaneTokenView view = csane_tokens_full(&tokens);
    TokenRange range = csane_tokens_unwrap(&view, view.range);
    CHECK(test_range_text(&tokens, range, "value"));
    CHECK(csane_tokens_unwrap(&view, range).first == range.first);
    CHECK(csane_tokens_unwrap(&view, range).end == range.end);
    free_tokenization(&tokens);
    return;
}

static void
test_assignments(void) {
    char *valid[] = {"x = f(a == b, y += 1)", "x <<= y + 1",
                     "answer += (p = 2)"};
    char *operators[] = {"=", "<<=", "+="};
    char *left[] = {"x", "x", "answer"};
    for (int32 i = 0; i < SIZEOF(valid)/SIZEOF(valid[0]); i += 1) {
        Tokenization tokens = tokenize_copy(valid[i], strlen32(valid[i]),
                                            TOKENIZE_DEFAULT);
        CsaneTokenView view = csane_tokens_full(&tokens);
        TokenRange lhs;
        TokenRange rhs;
        int32 op = -1;
        CHECK(csane_tokens_assignment(&view, &lhs, &rhs, &op));
        CHECK(test_range_text(&tokens, lhs, left[i]));
        CHECK(op >= 0 && TOKEN_IS(&tokens.tokens[op], operators[i]));
        CHECK(rhs.first == op + 1);
        free_tokenization(&tokens);
    }
    char *invalid[] = {
        "left == right",
        "f(x = 1, y)",
        "x = (y",
        "= x",
        "x = ",
    };
    for (int32 i = 0; i < SIZEOF(invalid)/SIZEOF(invalid[0]); i += 1) {
        Tokenization tokens = tokenize_copy(invalid[i], strlen32(invalid[i]),
                                            TOKENIZE_DEFAULT);
        CsaneTokenView view = csane_tokens_full(&tokens);
        CHECK(!csane_tokens_assignment(&view, NULL, NULL, NULL));
        free_tokenization(&tokens);
    }
    return;
}

int
main(void) {
    test_nested_arguments();
    test_empty_malformed_arguments();
    test_unwrap();
    test_assignments();
    test_conditional_path();
    test_nested_conditional_path();
    return failures > 0;
}
