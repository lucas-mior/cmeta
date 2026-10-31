// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_DECLARATIONS_C)
#define CSANECHECK_DECLARATIONS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

static StrFlex *csane_decl_qualifiers[] = {
    SFLIT("static"), SFLIT("extern"), SFLIT("auto"), SFLIT("register"),
    SFLIT("const"), SFLIT("volatile"), SFLIT("restrict"), SFLIT("inline"),
    SFLIT("_Atomic"), SFLIT("_Thread_local"),
};

static StrFlex *csane_decl_statements[] = {
    SFLIT("return"), SFLIT("if"), SFLIT("for"), SFLIT("while"),
    SFLIT("switch"), SFLIT("do"), SFLIT("else"), SFLIT("case"),
    SFLIT("default"), SFLIT("goto"), SFLIT("break"), SFLIT("continue"),
};

static StrFlex *csane_decl_storage_words[] = {
    SFLIT("typedef"), SFLIT("static"), SFLIT("extern"), SFLIT("auto"),
    SFLIT("register"), SFLIT("inline"), SFLIT("_Thread_local"),
};

static StrFlex *csane_decl_stdint_words[] = {
    SFLIT("int8_t"), SFLIT("uint8_t"), SFLIT("int16_t"),
    SFLIT("uint16_t"), SFLIT("int32_t"), SFLIT("uint32_t"),
    SFLIT("int64_t"), SFLIT("uint64_t"),
};

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
csane_decl_is(CsaneFile *file, int32 i, char *name, int32 name_len) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], name, name_len);
}

static bool
csane_decl_qualifier(CsaneFile *file, int32 i) {
    int32 count = LENGTH(csane_decl_qualifiers);
    for (int32 j = 0; j < count; j += 1) {
        StrFlex *word = csane_decl_qualifiers[j];
        if (csane_decl_is(file, i, word->data, word->len)) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_type_start(CsaneFile *file, TokenRange range) {
    int32 first;
    int32 type;
    int32 second;
    int32 j;
    int32 count = LENGTH(csane_decl_statements);

    first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end || csane_decl_is(file, first, STRLIT("typedef"))) {
        return false;
    }
    for (j = 0; j < count; j += 1) {
        StrFlex *word = csane_decl_statements[j];
        if (csane_decl_is(file, first, word->data, word->len)) {
            return false;
        }
    }
    type = first;
    while (type < range.end && csane_decl_qualifier(file, type)) {
        type = csane_decl_next(file, range, type);
    }
    if (type >= range.end) {
        return false;
    }
    if (csane_decl_is(file, type, STRLIT("struct"))
        || csane_decl_is(file, type, STRLIT("union"))
        || csane_decl_is(file, type, STRLIT("enum"))) {
        type = csane_decl_next(file, range, type);
        if (type >= range.end) {
            return false;
        }
    }
    if (file->tokens->tokens[type].kind != TOKEN_IDENT) {
        return false;
    }
    second = csane_decl_next(file, range, type);
    /* A declaration must have at least a plausible declarator. The
     * second name also admits lowercase system typedefs locally. */
    if (second >= range.end) {
        return false;
    }
    if (file->tokens->tokens[second].kind == TOKEN_IDENT
        || csane_decl_is(file, second, STRLIT("*"))) {
        return true;
    }
    if (csane_decl_is(file, second, STRLIT("("))) {
        int32 inside;

        inside = csane_decl_next(file, range, second);
        return inside < range.end
                   && csane_decl_is(file, inside, STRLIT("*"));
    }
    return false;
}

static int32
csane_decl_comma(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    if (!csane_tokens_view(&view, file->tokens, range)) {
        return -1;
    }
    return csane_tokens_top_level(&view, range.first, STRLIT(","));
}

static int32
csane_decl_function_open(CsaneFile *file, TokenRange range) {
    int32 i;

    i = csane_decl_next(file, range, range.first - 1);
    for (; i < range.end; i = csane_decl_next(file, range, i)) {
        TokenRange before;
        int32 previous;
        CsaneTokenView prefix;
        CsaneTokenView view;
        int32 close;
        int32 next;

        if (!csane_decl_is(file, i, STRLIT("("))) {
            continue;
        }
        before = (TokenRange){range.first, i};
        previous = token_range_last_significant(file->tokens, before);
        if (previous < range.first
            || file->tokens->tokens[previous].kind != TOKEN_IDENT) {
            continue;
        }
        /* An initializer's call is not a function declarator. */
        if (!csane_tokens_view(&prefix, file->tokens, before)
            || csane_tokens_top_level(&prefix, before.first - 1,
                                      STRLIT("=")) != before.end) {
            continue;
        }
        if (!csane_tokens_view(&view, file->tokens, range)) {
            return -1;
        }
        close = csane_tokens_match(&view, i);
        if (close < 0) {
            continue;
        }
        next = csane_decl_next(file, range, close);
        if (next >= range.end || csane_decl_is(file, next, STRLIT(";"))
            || csane_decl_is(file, next, STRLIT("{"))) {
            return i;
        }
    }
    return -1;
}

static bool
csane_decl_word(CsaneFile *file, int32 token, char *word, int32 word_len) {
    return token >= 0 && token < file->tokens->token_count
           && file->tokens->tokens[token].kind == TOKEN_IDENT
           && TOKEN_IS(&file->tokens->tokens[token], word, word_len);
}

static bool
csane_decl_storage(CsaneFile *file, int32 token) {
    int32 i;
    int32 count = LENGTH(csane_decl_storage_words);

    for (i = 0; i < count; i += 1) {
        StrFlex *word = csane_decl_storage_words[i];
        if (csane_decl_word(file, token, word->data, word->len)) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_cv_qualifier(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, STRLIT("const"))
           || csane_decl_word(file, token, STRLIT("volatile"))
           || csane_decl_word(file, token, STRLIT("restrict"))
           || csane_decl_word(file, token, STRLIT("_Atomic"));
}

static bool
csane_decl_aggregate(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, STRLIT("struct"))
           || csane_decl_word(file, token, STRLIT("union"))
           || csane_decl_word(file, token, STRLIT("enum"));
}

static bool
csane_decl_known_type(CsaneFile *file, int32 token) {
    if (token < 0 || token >= file->tokens->token_count) {
        return false;
    }
    return csane_type_name(&file->tokens->tokens[token])
           || csane_type_hints_typedef(file->type_hints,
                                       &file->tokens->tokens[token]);
}

static bool csane_decl_stdint_type(CsaneFile *, int32);

/* This is intentionally a shallow declaration classifier. Unknown names are
 * not promoted to types merely because the surrounding text resembles a
 * declaration. */
static bool
csane_decl_candidate(CsaneFile *file, TokenRange range) {
    int32 token;

    token = csane_decl_next(file, range, range.first - 1);
    if (token >= range.end) {
        return false;
    }
    if (csane_decl_word(file, token, STRLIT("typedef"))) {
        return true;
    }
    while (token < range.end
           && (csane_decl_storage(file, token)
               || csane_decl_cv_qualifier(file, token))) {
        token = csane_decl_next(file, range, token);
    }
    if (token >= range.end) {
        return false;
    }
    if (csane_decl_aggregate(file, token)) {
        return true;
    }
    if (csane_decl_word(file, token, STRLIT("signed"))
        || csane_decl_word(file, token, STRLIT("unsigned"))
        || csane_decl_stdint_type(file, token)) {
        return true;
    }
    if (csane_decl_known_type(file, token)) {
        int32 next;

        next = csane_decl_next(file, range, token);
        if (next >= range.end) {
            return false;
        }
        if (file->tokens->tokens[next].kind == TOKEN_IDENT
            || csane_decl_is(file, next, STRLIT("*"))) {
            return true;
        }
        if (csane_decl_is(file, next, STRLIT("("))) {
            int32 inside;

            inside = csane_decl_next(file, range, next);
            return inside < range.end
                   && csane_decl_is(file, inside, STRLIT("*"));
        }
    }
    return false;
}

static bool
csane_decl_stdint_type(CsaneFile *file, int32 token) {
    int32 i;
    int32 count = LENGTH(csane_decl_stdint_words);

    for (i = 0; i < count; i += 1) {
        StrFlex *word = csane_decl_stdint_words[i];
        if (csane_decl_word(file, token, word->data, word->len)) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_long_double(CsaneFile *file, TokenRange range, int32 token) {
    int32 next;

    if (!csane_decl_word(file, token, STRLIT("long"))) {
        return false;
    }
    next = csane_decl_next(file, range, token);
    return next < range.end && csane_decl_word(file, next, STRLIT("double"));
}

/* Check only the leading type-specifier sequence. Initializers, declarator
 * names, casts in initializers, and unknown type spellings are deliberately
 * outside this local decision. */
static void
csane_decl_type_prefix_check(CsaneFile *file, TokenRange range) {
    int32 token;
    int32 modifier = -1;
    bool base = false;

    token = csane_decl_next(file, range, range.first - 1);
    while (token < range.end) {
        Token *part = &file->tokens->tokens[token];
        int32 next;

        if (csane_decl_storage(file, token)
            || csane_decl_cv_qualifier(file, token)) {
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_aggregate(file, token)) {
            next = csane_decl_next(file, range, token);
            if (next < range.end
                && file->tokens->tokens[next].kind == TOKEN_IDENT) {
                token = csane_decl_next(file, range, next);
            } else {
                token = next;
            }
            base = true;
            continue;
        }
        if (base) {
            if (csane_decl_cv_qualifier(file, token)) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            if ((csane_decl_word(file, token, STRLIT("long"))
                 || csane_decl_word(file, token, STRLIT("int")))
                && modifier >= 0) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            break;
        }
        if (csane_decl_word(file, token, STRLIT("signed"))
            || csane_decl_word(file, token, STRLIT("unsigned"))) {
            if (modifier < 0) {
                modifier = token;
            }
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_word(file, token, STRLIT("short"))
            || csane_decl_word(file, token, STRLIT("int"))
            || csane_decl_word(file, token, STRLIT("long"))) {
            if (csane_decl_long_double(file, range, token)) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            base = true;
            modifier = token;
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_stdint_type(file, token)) {
            csane_rule_issue(file, part->offset, STRLIT("TYPE-STDINT-T"),
                             "use cbase fixed-width integer aliases");
            base = true;
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_known_type(file, token)) {
            base = true;
            token = csane_decl_next(file, range, token);
            continue;
        }
        break;
    }
}

static bool
csane_decl_function_pointer_typedef(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 token;

    if (!csane_tokens_view(&view, file->tokens, range)) {
        return false;
    }
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < range.end) {
        int32 previous;
        int32 close;
        int32 after;

        if (!csane_decl_is(file, token, STRLIT("*"))) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        previous = csane_tokens_previous(&view, token);
        if (!csane_decl_is(file, previous, STRLIT("("))) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        close = csane_tokens_match(&view, previous);
        if (close < 0) {
            return false;
        }
        after = csane_tokens_next(&view, close);
        if (csane_decl_is(file, after, STRLIT("("))) {
            return true;
        }
        token = csane_tokens_next(&view, token);
    }
    return false;
}

static bool
csane_decl_pointer_typedef(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 token;

    if (csane_decl_function_open(file, range) >= 0
        || csane_decl_function_pointer_typedef(file, range)
        || !csane_tokens_view(&view, file->tokens, range)) {
        return false;
    }
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < range.end) {
        int32 previous;

        if (csane_decl_is(file, token, STRLIT("{"))
            || csane_decl_is(file, token, STRLIT("["))) {
            int32 close = csane_tokens_match(&view, token);

            if (close < 0) {
                return false;
            }
            token = csane_tokens_next(&view, close);
            continue;
        }
        if (!csane_decl_is(file, token, STRLIT("*"))) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        previous = csane_tokens_previous(&view, token);
        if (csane_decl_is(file, previous, STRLIT("("))) {
            int32 close = csane_tokens_match(&view, previous);
            int32 after = -1;

            if (close >= 0) {
                after = csane_tokens_next(&view, close);
            }
            if (close >= 0 && !csane_decl_is(file, after, STRLIT("("))) {
                return true;
            }
        }
        if (csane_layout_classify(file, token) == CSANE_LAYOUT_DECLARATOR) {
            return true;
        }
        token = csane_tokens_next(&view, token);
    }
    return false;
}

static bool
csane_decl_xenums(CsaneFile *file) {
    return STREQUAL(file->path, file->path_len, "xenums.c")
           || ENDS_WITH(file->path, file->path_len, "/xenums.c");
}

static void
csane_decl_typedef_check(CsaneFile *file, TokenRange range) {
    int32 first;
    int32 type;

    first = csane_decl_next(file, range, range.first - 1);
    if (!csane_decl_word(file, first, STRLIT("typedef"))) {
        return;
    }
    type = csane_decl_next(file, range, first);
    while (type < range.end
           && (csane_decl_storage(file, type)
               || csane_decl_cv_qualifier(file, type))) {
        type = csane_decl_next(file, range, type);
    }
    if (csane_decl_word(file, type, STRLIT("enum"))
        && !csane_decl_xenums(file)) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         STRLIT("TYPE-ENUM-TYPEDEF"), "do not typedef enums");
    }
    if (csane_decl_word(file, type, STRLIT("union"))) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         STRLIT("TYPE-UNION-TYPEDEF"), "do not typedef unions");
    }
    if (csane_decl_pointer_typedef(file, range)) {
        CsaneTokenView view;
        int32 end = range.end;
        int32 alias = -1;

        if (csane_tokens_view(&view, file->tokens, range)) {
            end = csane_tokens_previous(&view, range.end);
            alias = csane_tokens_previous(&view, end);
        }
        if (alias < range.first
            || !csane_policy_typedef_allowed(file->policy, file->path,
                                             file->path_len,
                                             STRLIT("TYPE-POINTER-TYPEDEF"),
                                             &file->tokens->tokens[alias])) {
            csane_rule_issue(file, file->tokens->tokens[first].offset,
                             STRLIT("TYPE-POINTER-TYPEDEF"),
                             "do not typedef pointers except "
                             "function pointers");
        }
    }
}

static void
csane_decl_parameter_checks(CsaneFile *file, TokenRange args) {
    CsaneTokenView view;
    int32 first;

    if (!csane_tokens_view(&view, file->tokens, args)) {
        return;
    }
    first = csane_tokens_next(&view, args.first - 1);
    while (first >= args.first && first < args.end) {
        int32 comma;
        TokenRange parameter;

        comma = csane_tokens_top_level(&view, first - 1, STRLIT(","));
        parameter = (TokenRange){first, args.end};
        if (comma >= first && comma < args.end) {
            parameter.end = comma;
        }
        csane_decl_type_prefix_check(file, parameter);
        if (comma < first || comma >= args.end) {
            break;
        }
        first = csane_tokens_next(&view, comma);
    }
}

static void
csane_decl_signature_check(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 open;
    int32 close;
    int32 name;
    int32 token;
    TokenRange result;
    TokenRange args;

    open = csane_decl_function_open(file, range);
    if (open < 0 || !csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    close = csane_tokens_match(&view, open);
    name = csane_tokens_previous(&view, open);
    if (close < 0 || name < range.first) {
        return;
    }
    result = (TokenRange){range.first, name};
    args = (TokenRange){open + 1, close};
    if (!csane_decl_word(file, name, STRLIT("main"))) {
        csane_decl_type_prefix_check(file, result);
        csane_decl_parameter_checks(file, args);
    }
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < close) {
        if (csane_decl_word(file, token, STRLIT("const"))) {
            if (!csane_policy_rule_allowed(file->policy, file->path,
                                           file->path_len,
                                           STRLIT("TYPE-CONST-SIGNATURE"),
                                           &file->tokens->tokens[name])) {
                csane_rule_issue(file, file->tokens->tokens[token].offset,
                                 STRLIT("TYPE-CONST-SIGNATURE"),
                                 "const qualifiers are forbidden in function "
                                 "signatures");
            }
        }
        token = csane_tokens_next(&view, token);
    }
}

static void
csane_decl_style_check(CsaneFile *file, TokenRange range) {
    int32 open;

    if (!csane_decl_candidate(file, range)) {
        return;
    }
    open = csane_decl_function_open(file, range);
    if (open >= 0) {
        csane_decl_signature_check(file, range);
        csane_decl_typedef_check(file, range);
        return;
    }
    csane_decl_type_prefix_check(file, range);
    csane_decl_typedef_check(file, range);
}

static int32
csane_local_decl_line(CsaneFile *file, int32 token) {
    return tokenization_token_location(file->tokens, token).line;
}

static void
csane_decl_one(CsaneFile *file, TokenRange range, uint8 *lines,
               int32 line_count) {
    int32 first;
    int32 comma;
    int32 line;

    first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end) {
        return;
    }
    csane_decl_style_check(file, range);
    if (!csane_decl_type_start(file, range)) {
        return;
    }
    comma = csane_decl_comma(file, range);
    /* Only a direct function declarator with no other declarator is a
     * prototype. A function-pointer variable starts with '(*'. */
    if (comma >= range.end && csane_decl_function_open(file, range) >= 0) {
        return;
    }
    line = csane_local_decl_line(file, first);
    if ((line > 0 && line < line_count && lines[line])
        || (comma >= 0 && comma < range.end)) {
        csane_rule_issue(file, file->tokens->tokens[first].offset,
                         STRLIT("AST-DECL-ONE"),
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
    int32 start;
    int32 i;

    start = csane_decl_next(file, range, range.first - 1);
    for (i = start; i < range.end; i = csane_decl_next(file, range, i)) {
        Token *token = &file->tokens->tokens[i];

        if (TOKEN_IS(token, "for")) {
            int32 open;

            open = csane_decl_next(file, range, i);
            if (csane_decl_is(file, open, STRLIT("("))) {
                CsaneTokenView view;

                if (csane_tokens_view(&view, file->tokens, range)) {
                    int32 close;

                    close = csane_tokens_match(&view, open);
                    if (close >= 0) {
                        TokenRange header;
                        CsaneTokenView inner;
                        int32 begin;
                        int32 semi;

                        header = (TokenRange){open + 1, close};
                        (void)csane_tokens_view(&inner, file->tokens, header);
                        begin = open + 1;
                        semi = csane_tokens_top_level(&inner, begin,
                                                      STRLIT(";"));
                        if (semi >= 0 && semi < close) {
                            header.end = semi;
                            csane_decl_one(file, header, lines, line_count);
                        }
                        i = close;
                    }
                }
            }
        } else if (csane_decl_is(file, i, STRLIT("("))
                   || csane_decl_is(file, i, STRLIT("["))) {
            CsaneTokenView view;

            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close;

                close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, STRLIT("{"))) {
            CsaneTokenView view;

            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close;

                close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    TokenRange prefix;
                    bool type_prefix;
                    bool aggregate_type = false;

                    prefix = (TokenRange){start, i};
                    type_prefix = csane_decl_type_start(file, prefix);
                    if (type_prefix) {
                        int32 cursor;

                        cursor = start;
                        while (cursor < i) {
                            if (csane_decl_is(file, cursor, STRLIT("struct"))
                                || csane_decl_is(file, cursor,
                                                 STRLIT("union"))) {
                                aggregate_type = true;
                            }
                            if (csane_decl_is(file, cursor, STRLIT("="))) {
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
        } else if (csane_decl_is(file, i, STRLIT(";"))) {
            csane_decl_one(file, (TokenRange){start, i + 1}, lines,
                           line_count);
            start = csane_decl_next(file, range, i);
        }
    }
}

static void
csane_decl_header(CsaneFile *file, TokenRange range) {
    int32 open;
    CsaneTokenView view;
    int32 close;
    int32 name;
    int32 suffix;
    TokenRange args;
    int32 first;
    int32 previous;
    int32 first_column;
    int32 comma;

    open = csane_decl_function_open(file, range);
    if (open < 0) {
        return;
    }
    if (!csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0) {
        return;
    }
    name = csane_tokens_previous(&view, open);
    if (name >= range.first
        && csane_local_decl_line(file, name)
           < csane_local_decl_line(file, close)
        && csane_wrap_safe(file, name, close, false)) {
        CsaneTokenView full = csane_tokens_full(file->tokens);
        suffix = csane_tokens_next(&full, close);
        if (suffix < file->tokens->token_count
            && (csane_decl_is(file, suffix, STRLIT(";"))
                || csane_decl_is(file, suffix, STRLIT("{")))) {
            int32 start = file->tokens->tokens[name].offset;
            int32 end = file->tokens->tokens[suffix].offset
                        + file->tokens->tokens[suffix].len;
            int32 width = csane_wrap_flat_length(file->tokens->text,
                                                 start, end,
                                                 file->column_limit);
            if (width >= 0 && (file->column_limit <= 0
                               || width <= file->column_limit)) {
                csane_rule_issue(file, start, STRLIT("FMT-FUNC-WRAP"),
                                 "function header fits on one line but its "
                                 "parameters are split across multiple lines");
            }
        }
    }
    args = (TokenRange){open + 1, close};
    first = csane_decl_next(file, args, args.first - 1);
    previous = first;
    if (first >= args.end) {
        return;
    }
    first_column = tokenization_token_location(file->tokens, first).column;
    comma = csane_decl_comma(file, args);
    while (comma >= 0 && comma < args.end) {
        int32 current;

        current = csane_decl_next(file, args, comma);
        if (current >= args.end) {
            break;
        }
        if (csane_local_decl_line(file, current)
            > csane_local_decl_line(file, previous)
            && tokenization_token_location(file->tokens, current).column
               != first_column) {
            csane_rule_issue(file, file->tokens->tokens[current].offset,
                             STRLIT("FMT-FUNC-ALIGN"),
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
    int32 line_count = 2;
    uint8 *lines;
    int32 i;

    if (file->is_ryu || csane_calls_generated(file->path, file->path_len)) {
        return;
    }
    for (i = 0; i < file->tokens->text_len; i += 1) {
        if (file->tokens->text[i] == '\n') {
            line_count += 1;
        }
    }
    lines = malloc2(line_count*SIZEOF(*lines));
    memset64(lines, 0, line_count*SIZEOF(*lines));
    for (i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];

        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            csane_decl_one(file, item->tokens, lines, line_count);
        }
    }
    for (i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];

        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            csane_decl_signature_check(file, item->header);
            csane_decl_scope(file, item->body, lines, line_count);
        } else if (item->kind == CSANE_TOP_TYPE_DECLARATION) {
            int32 open;

            /* Type members form a separate declaration sequence. */
            open = item->tokens.first;
            while (open < item->tokens.end
                   && !csane_decl_is(file, open, STRLIT("{"))) {
                open += 1;
            }
            if (open < item->tokens.end) {
                CsaneTokenView view;

                if (csane_tokens_view(&view, file->tokens, item->tokens)) {
                    int32 close;

                    close = csane_tokens_match(&view, open);
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
    if (csane_calls_generated(file->path, file->path_len)) {
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
