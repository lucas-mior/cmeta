// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck_tokens.c"

static int32
find_token(CsaneTokenView *view, char *text) {
    for (int32 i = csane_tokens_next(view, -1); i < view->range.end;
         i = csane_tokens_next(view, i)) {
        if (TOKEN_IS(&view->tokens->tokens[i], text)) {
            return i;
        }
    }
    return -1;
}

int
main(void) {
    char source[] = "call(a, nested(b, c), arr[1], (x + y));\n"
                    "// parentheses (] inside a comment\n"
                    "char *s = \"(])\";\n";
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    CsaneTokenView view = csane_tokens_full(&tokens);
    int32 open = find_token(&view, "(");
    int32 close = csane_tokens_match(&view, open);
    CsaneTokenView args = {0};
    TokenRange pieces[4] = {0};
    SourceRange extent;
    int32 count;
    int32 comma;
    int result = 0;

    if ((open < 0) || (close <= open)
        || (csane_tokens_match(&view, close) != open)
        || !csane_tokens_balanced(&view)) {
        result = 1;
        goto done;
    }
    if (!csane_tokens_view(&args, &tokens,
                           (TokenRange){open + 1, close})) {
        result = 2;
        goto done;
    }
    count = csane_tokens_split(&args, ",", 1, pieces, 4);
    comma = csane_tokens_top_level(&args, args.range.first - 1, ",", 1);
    if ((count != 4) || (comma != pieces[0].end)
        || (csane_tokens_next(&args, args.range.first - 1)
            != pieces[0].first)
        || (csane_tokens_previous(&args, args.range.end)
            >= args.range.end)
        || (csane_tokens_top_level(&args, comma, ",", 1)
            != pieces[1].end)) {
        result = 3;
        goto done;
    }
    extent = csane_tokens_source(&args);
    if ((extent.start >= extent.end)
        || (source[extent.start] != 'a')) {
        result = 4;
        goto done;
    }
    if ((csane_tokens_match(&args, open) != -1)
        || (csane_tokens_top_level(&args, args.range.first - 1,
                                   ";", 1) != args.range.end)) {
        result = 5;
        goto done;
    }
    {
        char bad_source[] = "f(a, (b, c]";
        Tokenization bad = tokenize_copy(bad_source, strlen32(bad_source), 0);
        CsaneTokenView bad_view = csane_tokens_full(&bad);
        if (csane_tokens_balanced(&bad_view)
            || (csane_tokens_top_level(&bad_view, -1, ",", 1) != -1)
            || (csane_tokens_split(&bad_view, ",", 1, NULL, 0) != -1)) {
            result = 6;
        }
        free_tokenization(&bad);
    }
    if (csane_tokens_view(&args, &tokens,
                          (TokenRange){0, tokens.token_count + 1})) {
        result = 7;
    }
done:
    free_tokenization(&tokens);
    return result;
}
