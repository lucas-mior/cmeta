// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_DECLARATIONS_C)
#define CSANECHECK_DECLARATIONS_C

#include "cbase.h"
#include "csanecheck.h"
#include "csanecheck_tokens.h"

/* A deliberately local declaration view: no typedef table or AST is needed. */
static int32
csane_decl_next(CsaneFile *file, TokenRange range, int32 after) {
    return token_range_next_significant(file->tokens, range, after);
}

static bool
csane_decl_is(CsaneFile *file, int32 i, char *name) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], name);
}

static bool
csane_decl_type_start(CsaneFile *file, TokenRange range) {
    int32 first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end) {
        return false;
    }
    Token *token = &file->tokens->tokens[first];
    if (csane_type_name(token) || csane_decl_is(file, first, "typedef")
        || csane_decl_is(file, first, "struct")
        || csane_decl_is(file, first, "union")
        || csane_decl_is(file, first, "enum")
        || csane_decl_is(file, first, "const")
        || csane_decl_is(file, first, "static")
        || csane_decl_is(file, first, "extern")
        || csane_decl_is(file, first, "volatile")
        || csane_decl_is(file, first, "unsigned")
        || csane_decl_is(file, first, "signed")
        || csane_decl_is(file, first, "register")
        || csane_decl_is(file, first, "_Atomic")) {
        return true;
    }
    /* System typedefs are occasionally lowercase. Require a declarator
     * afterwards rather than interpreting an arbitrary expression as C. */
    int32 second = csane_decl_next(file, range, first);
    return token->kind == TOKEN_IDENT && second < range.end
           && file->tokens->tokens[second].kind == TOKEN_IDENT;
}

static int32
csane_decl_comma(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    if (!csane_tokens_view(&view, file->tokens, range)) {
        return -1;
    }
    return csane_tokens_top_level(&view, range.first, ",", 1);
}

static int32
csane_decl_function_open(CsaneFile *file, TokenRange range) {
    int32 i = csane_decl_next(file, range, range.first - 1);
    for (; i < range.end; i = csane_decl_next(file, range, i)) {
        if (!csane_decl_is(file, i, "(")) {
            continue;
        }
        TokenRange before = {range.first, i};
        int32 previous = token_range_last_significant(file->tokens, before);
        if (previous < range.first
            || file->tokens->tokens[previous].kind != TOKEN_IDENT) {
            continue;
        }
        CsaneTokenView view;
        if (!csane_tokens_view(&view, file->tokens, range)) {
            return -1;
        }
        int32 close = csane_tokens_match(&view, i);
        if (close < 0) {
            continue;
        }
        int32 next = csane_decl_next(file, range, close);
        if (next >= range.end || csane_decl_is(file, next, ";")
            || csane_decl_is(file, next, "{")) {
            return i;
        }
    }
    return -1;
}

static int32
csane_local_decl_line(CsaneFile *file, int32 token) {
    return tokenization_token_location(file->tokens, token).line;
}

static void
csane_decl_one(CsaneFile *file, TokenRange range, int32 *last_line) {
    int32 first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end || !csane_decl_type_start(file, range)) {
        *last_line = -1;
        return;
    }
    /* Function prototypes are not variable declarations. A function pointer
     * begins with '(*', so it is not mistaken for a prototype. */
    if (csane_decl_function_open(file, range) >= 0) {
        *last_line = -1;
        return;
    }
    int32 line = csane_local_decl_line(file, first);
    int32 comma = csane_decl_comma(file, range);
    if ((*last_line == line && line > 0)
        || (comma >= 0 && comma < range.end)) {
        csane_rule_issue(file, file->tokens->tokens[first].offset,
                         "AST-DECL-ONE",
                         "only one variable declaration per line");
    }
    *last_line = line;
}

/* Each brace-delimited lexical scope has a separate declaration sequence.
 * Nested initializers are skipped as balanced expressions rather than
 * interpreted as compound blocks. */
static void
csane_decl_scope(CsaneFile *file, TokenRange range) {
    int32 previous = -1;
    int32 start = csane_decl_next(file, range, range.first - 1);
    for (int32 i = start; i < range.end;
         i = csane_decl_next(file, range, i)) {
        Token *token = &file->tokens->tokens[i];
        if (TOKEN_IS(token, "for")) {
            int32 open = csane_decl_next(file, range, i);
            if (csane_decl_is(file, open, "(")) {
                CsaneTokenView view;
                if (csane_tokens_view(&view, file->tokens, range)) {
                    int32 close = csane_tokens_match(&view, open);
                    if (close >= 0) {
                        TokenRange header = {open + 1, close};
                        CsaneTokenView inner;
                        (void)csane_tokens_view(&inner, file->tokens,
                                                header);
                        int32 begin = open + 1;
                        int32 semi = csane_tokens_top_level(&inner, begin,
                                                            ";", 1);
                        if (semi >= 0 && semi < close) {
                            header.end = semi;
                            int32 isolated = -1;
                            csane_decl_one(file, header, &isolated);
                        }
                        i = close;
                    }
                }
            }
        } else if (csane_decl_is(file, i, "(")
                   || csane_decl_is(file, i, "[")) {
            CsaneTokenView view;
            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, "{")) {
            CsaneTokenView view;
            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    TokenRange prefix = {start, i};
                    if (start == i
                        || !csane_decl_type_start(file, prefix)) {
                        csane_decl_scope(file, (TokenRange){i + 1, close});
                        previous = -1;
                        start = csane_decl_next(file, range, close);
                    }
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, ";")) {
            csane_decl_one(file, (TokenRange){start, i + 1}, &previous);
            start = csane_decl_next(file, range, i);
        }
    }
}

static void
csane_decl_header(CsaneFile *file, TokenRange range) {
    int32 open = csane_decl_function_open(file, range);
    if (open < 0) {
        return;
    }
    CsaneTokenView view;
    if (!csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    int32 close = csane_tokens_match(&view, open);
    if (close < 0) {
        return;
    }
    TokenRange args = {open + 1, close};
    int32 first = csane_decl_next(file, args, args.first - 1);
    int32 previous = first;
    if (first >= args.end) {
        return;
    }
    int32 first_column = tokenization_token_location(file->tokens,
                                                     first).column;
    int32 comma = csane_decl_comma(file, args);
    while (comma >= 0 && comma < args.end) {
        int32 current = csane_decl_next(file, args, comma);
        if (current >= args.end) {
            break;
        }
        if (csane_local_decl_line(file, current)
            > csane_local_decl_line(file, previous)
            && tokenization_token_location(file->tokens, current).column
               != first_column) {
            csane_rule_issue(file, file->tokens->tokens[current].offset,
                             "FMT-FUNC-ALIGN",
                             "function header parameter is not aligned "
                             "with first parameter");
            break;
        }
        previous = current;
        args.first = current;
        comma = csane_decl_comma(file, args);
    }
}

void
csane_declarations_check(CsaneFile *file) {
    int32 previous = -1;
    if (file->is_ryu || csane_calls_generated(file->path)) {
        return;
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION) {
            csane_decl_one(file, item->tokens, &previous);
        } else {
            previous = -1;
        }
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            csane_decl_scope(file, item->body);
        } else if (item->kind == CSANE_TOP_TYPE_DECLARATION) {
            /* Type members form a separate declaration sequence. */
            int32 open = item->tokens.first;
            while (open < item->tokens.end
                   && !csane_decl_is(file, open, "{")) {
                open += 1;
            }
            if (open < item->tokens.end) {
                CsaneTokenView view;
                if (csane_tokens_view(&view, file->tokens, item->tokens)) {
                    int32 close = csane_tokens_match(&view, open);
                    if (close >= 0) {
                        csane_decl_scope(file, (TokenRange){open + 1, close});
                    }
                }
            }
        }
    }
}

void
csane_declarations_headers(CsaneFile *file) {
    if (csane_calls_generated(file->path)) {
        return;
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            int32 name = item->name_token;
            if (name < 0 || !csane_calls_code(file, name)
                || csane_calls_location(file, name).column != 0) {
                continue;
            }
            csane_decl_header(file, item->header);
        }
    }
}

#endif /* CSANECHECK_DECLARATIONS_C */
