// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_DECLARATIONS_C)
#define CSANECHECK_DECLARATIONS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

/* A deliberately local declaration view: no typedef table or AST is needed. */
static int32
csane_decl_next(CsaneFile *file, TokenRange range, int32 after) {
    int32 i = token_range_next_significant(file->tokens, range, after);
    while (i >= 0 && i < range.end) {
        int32 region = csane_regions_at_token(file->regions, i);
        if (region >= 0
            && file->regions->items[region].kind == CSANE_REGION_CODE) {
            break;
        }
        i = token_range_next_significant(file->tokens, range, i);
    }
    return i;
}

static bool
csane_decl_is(CsaneFile *file, int32 i, char *name) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], name);
}

static bool
csane_decl_qualifier(CsaneFile *file, int32 i) {
    static char *words[] = {
        "static", "extern", "auto", "register", "const", "volatile",
        "restrict", "inline", "_Atomic", "_Thread_local",
    };
    for (int32 j = 0; j < SIZEOF(words)/SIZEOF(words[0]); j += 1) {
        if (csane_decl_is(file, i, words[j])) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_type_start(CsaneFile *file, TokenRange range) {
    int32 first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end || csane_decl_is(file, first, "typedef")) {
        return false;
    }
    static char *statements[] = {
        "return", "if", "for", "while", "switch", "do", "else",
        "case", "default", "goto", "break", "continue",
    };
    for (int32 j = 0; j < SIZEOF(statements)/SIZEOF(statements[0]); j += 1) {
        if (csane_decl_is(file, first, statements[j])) {
            return false;
        }
    }
    int32 type = first;
    while (type < range.end && csane_decl_qualifier(file, type)) {
        type = csane_decl_next(file, range, type);
    }
    if (type >= range.end) {
        return false;
    }
    if (csane_decl_is(file, type, "struct")
        || csane_decl_is(file, type, "union")
        || csane_decl_is(file, type, "enum")) {
        type = csane_decl_next(file, range, type);
        if (type >= range.end) {
            return false;
        }
    }
    Token *token = &file->tokens->tokens[type];
    if (token->kind != TOKEN_IDENT) {
        return false;
    }
    int32 second = csane_decl_next(file, range, type);
    /* A declaration must have at least a plausible declarator. The
     * second name also admits lowercase system typedefs locally. */
    return second < range.end
           && (file->tokens->tokens[second].kind == TOKEN_IDENT
               || csane_decl_is(file, second, "*")
               || csane_decl_is(file, second, "("));
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
        /* An initializer's call is not a function declarator. */
        CsaneTokenView prefix;
        if (!csane_tokens_view(&prefix, file->tokens, before)
            || csane_tokens_top_level(&prefix, before.first - 1,
                                      "=", 1) != before.end) {
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
csane_decl_one(CsaneFile *file, TokenRange range, uint8 *lines,
               int32 line_count) {
    int32 first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end || !csane_decl_type_start(file, range)) {
        return;
    }
    int32 comma = csane_decl_comma(file, range);
    /* Only a direct function declarator with no other declarator is a
     * prototype. A function-pointer variable starts with '(*'. */
    if (comma >= range.end && csane_decl_function_open(file, range) >= 0) {
        return;
    }
    int32 line = csane_local_decl_line(file, first);
    if ((line > 0 && line < line_count && lines[line])
        || (comma >= 0 && comma < range.end)) {
        csane_rule_issue(file, file->tokens->tokens[first].offset,
                         "AST-DECL-ONE",
                         "only one variable declaration per line");
    }
    if (line > 0 && line < line_count) {
        lines[line] = 1;
    }
}

/* Inspect each brace-delimited scope without resetting the file-wide
 * declaration-line map. Nested initializers are skipped as balanced
 * expressions rather than interpreted as compound blocks. */
static void
csane_decl_scope(CsaneFile *file, TokenRange range, uint8 *lines,
                 int32 line_count) {
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
                            csane_decl_one(file, header, lines, line_count);
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
                    bool type_prefix = csane_decl_type_start(file, prefix);
                    bool aggregate_type = false;
                    if (type_prefix) {
                        int32 cursor = start;
                        while (cursor < i) {
                            if (csane_decl_is(file, cursor, "struct")
                                || csane_decl_is(file, cursor, "union")) {
                                aggregate_type = true;
                            }
                            if (csane_decl_is(file, cursor, "=")) {
                                aggregate_type = false;
                                break;
                            }
                            cursor = csane_decl_next(file, prefix, cursor);
                        }
                    }
                    if (start == i || !type_prefix || aggregate_type) {
                        csane_decl_scope(file, (TokenRange){i + 1, close},
                                         lines, line_count);
                        if (!aggregate_type) {
                            start = csane_decl_next(file, range, close);
                        }
                    }
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, ";")) {
            csane_decl_one(file, (TokenRange){start, i + 1}, lines,
                           line_count);
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
    if (file->is_ryu || csane_calls_generated(file->path)) {
        return;
    }
    int32 line_count = 2;
    for (int32 i = 0; i < file->tokens->text_len; i += 1) {
        if (file->tokens->text[i] == '\n') {
            line_count += 1;
        }
    }
    uint8 *lines = malloc2(line_count*SIZEOF(*lines));
    memset64(lines, 0, line_count*SIZEOF(*lines));
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            csane_decl_one(file, item->tokens, lines, line_count);
        }
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            csane_decl_scope(file, item->body, lines, line_count);
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
                        csane_decl_scope(file, (TokenRange){open + 1, close},
                                         lines, line_count);
                    }
                }
            }
        }
    }
    free2(lines, line_count*SIZEOF(*lines));
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
