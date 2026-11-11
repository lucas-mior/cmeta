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
csane_decl_is(CsaneFile *file, int32 token_index, char *name, int32 name_len) {
    return token_index >= 0 && token_index < ARRAY_LEN(file->tokens->tokens)
           && TOKEN_IS(&file->tokens->tokens[token_index], name, name_len);
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
    while (true) {
        bool _i25_predicate;
        _i25_predicate = type < range.end;
        if (_i25_predicate) {
            bool _i25_result;
            do {
                int32 _i25_token_index = type;

                int32 nested_count = LENGTH(csane_decl_qualifiers);
                for (int32 nested_j =
                    0; nested_j < nested_count; nested_j += 1) {
                    StrFlex *word = csane_decl_qualifiers[nested_j];
                    if (csane_decl_is(file, _i25_token_index, word->data,
                                      word->len)) {
                        _i25_result = true;
                        goto _i25_done;
                    }
                }
                _i25_result = false;
                goto _i25_done;
                _i25_done: ;
            } while (false);
            _i25_predicate = _i25_result;
        }
        if (!_i25_predicate) {
            break;
        }
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
    return token >= 0 && token < ARRAY_LEN(file->tokens->tokens)
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
    if (token < 0 || token >= ARRAY_LEN(file->tokens->tokens)) {
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

static void
csane_decl_typedef_check(CsaneFile *file, TokenRange range) {
    bool _i28_result;
    bool _i28_predicate;
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
        && !(STREQUAL(file->path, file->path_len, "xenums.c")
             || ENDS_WITH(file->path, file->path_len, "/xenums.c"))) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         STRLIT("TYPE-ENUM-TYPEDEF"), "do not typedef enums");
    }
    if (csane_decl_word(file, type, STRLIT("union"))) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         STRLIT("TYPE-UNION-TYPEDEF"), "do not typedef unions");
    }
    do {
        CsaneTokenView view;
        int32 token;

        bool _i27_predicate;
        _i27_predicate = csane_decl_function_open(file, range) >= 0;
        if (!_i27_predicate) {
            bool _i27_result;
            do {
                CsaneTokenView nested_view;
                int32 nested_token;

                if (!csane_tokens_view(&nested_view, file->tokens, range)) {
                    _i27_result = false;
                    goto _i27_done;
                }
                nested_token = csane_tokens_next(&nested_view, range.first - 1);
                while (nested_token >= range.first
                    && nested_token < range.end) {
                    int32 previous;
                    int32 close;
                    int32 after;

                    if (!csane_decl_is(file, nested_token, STRLIT("*"))) {
                        nested_token = csane_tokens_next(&nested_view,
                                                         nested_token);
                        continue;
                    }
                    previous = csane_tokens_previous(&nested_view,
                                                     nested_token);
                    if (!csane_decl_is(file, previous, STRLIT("("))) {
                        nested_token = csane_tokens_next(&nested_view,
                                                         nested_token);
                        continue;
                    }
                    close = csane_tokens_match(&nested_view, previous);
                    if (close < 0) {
                        _i27_result = false;
                        goto _i27_done;
                    }
                    after = csane_tokens_next(&nested_view, close);
                    if (csane_decl_is(file, after, STRLIT("("))) {
                        _i27_result = true;
                        goto _i27_done;
                    }
                    nested_token = csane_tokens_next(&nested_view,
                                                     nested_token);
                }
                _i27_result = false;
                goto _i27_done;
                _i27_done: ;
            } while (false);
            _i27_predicate = _i27_result;
        }
        if (!_i27_predicate) {
            _i27_predicate = !csane_tokens_view(&view, file->tokens, range);
        }
        if (_i27_predicate) {
            _i28_result = false;
            goto _i28_done;
        }
        token = csane_tokens_next(&view, range.first - 1);
        while (token >= range.first && token < range.end) {
            int32 previous;

            if (csane_decl_is(file, token, STRLIT("{"))
                || csane_decl_is(file, token, STRLIT("["))) {
                int32 close = csane_tokens_match(&view, token);

                if (close < 0) {
                    _i28_result = false;
                    goto _i28_done;
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
                    _i28_result = true;
                    goto _i28_done;
                }
            }
            if (csane_layout_classify(file, token) == CSANE_LAYOUT_DECLARATOR) {
                _i28_result = true;
                goto _i28_done;
            }
            token = csane_tokens_next(&view, token);
        }
        _i28_result = false;
        goto _i28_done;
        _i28_done: ;
    } while (false);
    _i28_predicate = _i28_result;
    if (_i28_predicate) {
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

/* Inspect declaration heads, never initializers or nested argument lists.
 * A parenthesized pointer declarator is a variable, while error(...) is
 * normally the project's logging macro (or a function declarator). */
static void
csane_decl_error_names(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 first;

    if (!csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    first = csane_tokens_next(&view, range.first - 1);
    while (first >= range.first && first < range.end) {
        int32 comma = csane_tokens_top_level(&view, first - 1, STRLIT(","));
        TokenRange part = {first, range.end};
        CsaneTokenView declarator;
        int32 equal;
        int32 token;
        int32 parentheses = 0;
        int32 brackets = 0;

        if (comma < 0) {
            break;
        }
        if (comma < range.end) {
            part.end = comma;
        }
        if (!csane_tokens_view(&declarator, file->tokens, part)) {
            break;
        }
        equal = csane_tokens_top_level(&declarator, part.first - 1,
                                       STRLIT("="));
        if (equal < 0) {
            break;
        }
        if (equal < part.end) {
            part.end = equal;
            (void)csane_tokens_view(&declarator, file->tokens, part);
        }
        token = csane_tokens_next(&declarator, part.first - 1);
        while (token >= part.first && token < part.end) {
            int32 previous = csane_tokens_previous(&declarator, token);
            int32 next = csane_tokens_next(&declarator, token);

            if (csane_decl_is(file, token, STRLIT("{"))) {
                int32 close = csane_tokens_match(&declarator, token);

                if (close >= 0) {
                    token = csane_tokens_next(&declarator, close);
                    continue;
                }
            }
            if (csane_decl_is(file, token, STRLIT("("))) {
                parentheses += 1;
            } else if (csane_decl_is(file, token, STRLIT(")"))) {
                parentheses -= parentheses > 0;
            } else if (csane_decl_is(file, token, STRLIT("["))) {
                brackets += 1;
            } else if (csane_decl_is(file, token, STRLIT("]"))) {
                brackets -= brackets > 0;
            }
            if (csane_decl_word(file, token, STRLIT("error"))
                && brackets == 0
                && (parentheses == 0
                    || csane_decl_is(file, previous, STRLIT("*")))
                && !csane_decl_is(file, previous, STRLIT("struct"))
                && !csane_decl_is(file, previous, STRLIT("union"))
                && !csane_decl_is(file, previous, STRLIT("enum"))
                && !csane_decl_is(file, previous, STRLIT("."))
                && !csane_decl_is(file, previous, STRLIT("->"))
                && !csane_decl_is(file, previous, STRLIT(":"))
                && !csane_decl_is(file, next, STRLIT("("))
                && !csane_decl_is(file, next, STRLIT("*"))
                && (next >= part.end
                    || file->tokens->tokens[next].kind != TOKEN_IDENT)) {
                csane_rule_issue(file, file->tokens->tokens[token].offset,
                                 STRLIT("STYLE-ERROR-NAME"),
                                 "do not name variables or parameters error");
            }
            token = next;
        }
        if (comma >= range.end) {
            break;
        }
        first = csane_tokens_next(&view, comma);
    }
}

/* Parameter declarators may contain pointer and callback syntax. Only a
 * declarator identifier, never a type tag or array bound, is an argument
 * name. Inspect the original tokens instead of matching raw source text. */

static void
csane_decl_parameter_checks(CsaneFile *file, TokenRange args,
                            bool check_types) {
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
        if (check_types) {
            csane_decl_type_prefix_check(file, parameter);
        }
        csane_decl_error_names(file, parameter);
        /* A function-pointer parameter can declare its own parameters. */
        do {
            CsaneTokenView nested;
            int32 cursor;

            if (!csane_tokens_view(&nested, file->tokens, parameter)) {
                break;
            }
            cursor = csane_tokens_next(&nested, parameter.first - 1);
            while (cursor >= parameter.first && cursor < parameter.end) {
                int32 pointer;
                int32 close;
                int32 open;
                int32 args_close;

                if (!csane_decl_is(file, cursor, STRLIT("("))) {
                    cursor = csane_tokens_next(&nested, cursor);
                    continue;
                }
                pointer = csane_tokens_next(&nested, cursor);
                close = csane_tokens_match(&nested, cursor);
                if (!csane_decl_is(file, pointer, STRLIT("*"))
                    || close < 0) {
                    cursor = csane_tokens_next(&nested, cursor);
                    continue;
                }
                open = csane_tokens_next(&nested, close);
                args_close = csane_tokens_match(&nested, open);
                if (csane_decl_is(file, open, STRLIT("("))
                    && args_close > open) {
                    csane_decl_parameter_checks(file,
                                                (TokenRange){open + 1,
                                                             args_close},
                                                false);
                }
                cursor = csane_tokens_next(&nested, args_close > open
                                                   ? args_close : close);
            }
        } while (false);
        do {
            CsaneFile *in_file = file;

            CsaneTokenView nested_view_2;
            int32 brackets = 0;
            int32 token;

            if (!csane_tokens_view(&nested_view_2, in_file->tokens,
                                   parameter)) {
                break;
            }
            token = csane_tokens_next(&nested_view_2, parameter.first - 1);
            while (token >= parameter.first && token < parameter.end) {
                Token *word = &in_file->tokens->tokens[token];
                int32 previous;
                int32 next;

                if (csane_decl_is(in_file, token, STRLIT("["))) {
                    brackets += 1;
                } else if (csane_decl_is(in_file, token, STRLIT("]"))) {
                    brackets -= brackets > 0;
                }
                if (brackets != 0 || word->kind != TOKEN_IDENT || word->len != 1
                    || (word->text[0] != 'i' && word->text[0] != 'j')) {
                    token = csane_tokens_next(&nested_view_2, token);
                    continue;
                }
                previous = csane_tokens_previous(&nested_view_2, token);
                next = csane_tokens_next(&nested_view_2, token);
                if (previous < parameter.first
                    || (in_file->tokens->tokens[previous].kind != TOKEN_IDENT
                        && !csane_decl_is(in_file, previous, STRLIT("*")))
                    || csane_decl_is(in_file, previous, STRLIT("struct"))
                    || csane_decl_is(in_file, previous, STRLIT("union"))
                    || csane_decl_is(in_file, previous, STRLIT("enum"))
                    || (next < parameter.end
                        && !csane_decl_is(in_file, next, STRLIT(")"))
                        && !csane_decl_is(in_file, next, STRLIT("["))
                        && !csane_decl_is(in_file, next, STRLIT("(")))) {
                    token = csane_tokens_next(&nested_view_2, token);
                    continue;
                }
                csane_rule_issue(in_file, word->offset,
                                 STRLIT("STYLE-PARAM-IJK"),
                                 "i, j and k are forbidden "
                                 "as function parameter "
                                 "names");
                token = csane_tokens_next(&nested_view_2, token);
            }
        } while (false);
        if (comma < first || comma >= args.end) {
            break;
        }
        first = csane_tokens_next(&view, comma);
    }
}

/* Function-pointer declarations also carry formal parameter names, even
 * though their declarator has no direct function-name/parameter pair. */

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
    }
    csane_decl_parameter_checks(file, args,
                                !csane_decl_word(file, name, STRLIT("main")));
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
    do {
        int32 open;

        bool _i26_predicate;
        bool _i26_result;
        do {
            int32 token;

            token = csane_decl_next(file, range, range.first - 1);
            if (token >= range.end) {
                _i26_result = false;
                goto _i26_done;
            }
            if (csane_decl_word(file, token, STRLIT("typedef"))) {
                _i26_result = true;
                goto _i26_done;
            }
            while (token < range.end
                   && (csane_decl_storage(file, token)
                       || csane_decl_cv_qualifier(file, token))) {
                token = csane_decl_next(file, range, token);
            }
            if (token >= range.end) {
                _i26_result = false;
                goto _i26_done;
            }
            if (csane_decl_aggregate(file, token)) {
                _i26_result = true;
                goto _i26_done;
            }
            if (csane_decl_word(file, token, STRLIT("signed"))
                || csane_decl_word(file, token, STRLIT("unsigned"))
                || csane_decl_stdint_type(file, token)) {
                _i26_result = true;
                goto _i26_done;
            }
            if (csane_decl_known_type(file, token)) {
                int32 next;

                next = csane_decl_next(file, range, token);
                if (next >= range.end) {
                    _i26_result = false;
                    goto _i26_done;
                }
                if (file->tokens->tokens[next].kind == TOKEN_IDENT
                    || csane_decl_is(file, next, STRLIT("*"))) {
                    _i26_result = true;
                    goto _i26_done;
                }
                if (csane_decl_is(file, next, STRLIT("("))) {
                    int32 inside;

                    inside = csane_decl_next(file, range, next);
                    _i26_result = inside < range.end
                           && csane_decl_is(file, inside, STRLIT("*"));
                    goto _i26_done;
                }
            }
            _i26_result = false;
            goto _i26_done;
            _i26_done: ;
        } while (false);
        _i26_predicate = !_i26_result;
        if (_i26_predicate) {
            break;
        }
        open = csane_decl_function_open(file, range);
        if (open >= 0) {
            csane_decl_signature_check(file, range);
            csane_decl_typedef_check(file, range);
            break;
        }
        csane_decl_type_prefix_check(file, range);
        csane_decl_typedef_check(file, range);
        do {
            CsaneTokenView view;
            int32 token;

            if (!csane_tokens_view(&view, file->tokens, range)) {
                break;
            }
            token = csane_tokens_next(&view, range.first - 1);
            while (token >= range.first && token < range.end) {
                if (csane_decl_is(file, token, STRLIT("{"))) {
                    int32 close = csane_tokens_match(&view, token);

                    if (close > token) {
                        token = csane_tokens_next(&view, close);
                        continue;
                    }
                }
                if (csane_decl_is(file, token, STRLIT("("))) {
                    int32 pointer = csane_tokens_next(&view, token);
                    int32 close = csane_tokens_match(&view, token);

                    if (csane_decl_is(file, pointer, STRLIT("*"))
                        && close > pointer) {
                        int32 args = csane_tokens_next(&view, close);
                        int32 args_close = csane_tokens_match(&view, args);

                        if (csane_decl_is(file, args, STRLIT("("))
                            && args_close > args) {
                            TokenRange callback_args = {args + 1, args_close};

                            csane_decl_parameter_checks(file, callback_args,
                                                        false);
                        }
                    }
                }
                token = csane_tokens_next(&view, token);
            }
        } while (false);
    } while (false);
    if (!csane_decl_type_start(file, range)) {
        return;
    }
    comma = csane_decl_comma(file, range);
    /* Only a direct function declarator with no other declarator is a
     * prototype. A function-pointer variable starts with '(*'. */
    if (comma >= range.end && csane_decl_function_open(file, range) >= 0) {
        return;
    }
    if (!csane_decl_word(file, first, STRLIT("typedef"))) {
        csane_decl_error_names(file, range);
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

/* Wrapping depends on the width of the entire physical header line,
 * including qualifiers and return types preceding an inline function name. */

static void
csane_decl_header(CsaneFile *file, TokenRange range) {
    int32 open;
    CsaneTokenView view;
    int32 close;
    int32 name;
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
    do {
        CsaneFile *in_file = file;

        CsaneTokenView full;
        int32 nested_first;
        int32 suffix;
        int32 start;
        int32 end;
        int32 width;

        if (name < range.first
            || csane_local_decl_line(in_file, name)
               >= csane_local_decl_line(in_file, close)) {
            break;
        }
        nested_first = name;
        while (nested_first > range.first
               && csane_local_decl_line(in_file, nested_first - 1)
                  == csane_local_decl_line(in_file, name)) {
            nested_first -= 1;
        }
        if (!csane_wrap_safe(in_file, nested_first, close, false)) {
            break;
        }
        full = csane_tokens_full(in_file->tokens);
        suffix = csane_tokens_next(&full, close);
        if (suffix >= ARRAY_LEN(in_file->tokens->tokens)
            || (!csane_decl_is(in_file, suffix, STRLIT(";"))
                && !csane_decl_is(in_file, suffix, STRLIT("{")))) {
            break;
        }
        start = in_file->tokens->tokens[nested_first].offset;
        while (start > 0 && in_file->tokens->text[start - 1] != '\n') {
            start -= 1;
        }
        end = in_file->tokens->tokens[suffix].offset
              + in_file->tokens->tokens[suffix].len;
        width = csane_wrap_flat_length(in_file->tokens->text, start, end,
                                       in_file->column_limit);
        if (width >= 0 && (in_file->column_limit <= 0
                           || width <= in_file->column_limit)) {
            csane_rule_issue(in_file, in_file->tokens->tokens[name].offset,
                             STRLIT("FMT-FUNC-WRAP"),
                             "function header fits on one line but its "
                             "parameters are split across multiple lines");
        }
    } while (false);
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
    for (i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];

        if (item->kind == CSANE_TOP_DECLARATION
            || item->kind == CSANE_TOP_TYPE_DECLARATION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            csane_decl_one(file, item->tokens, lines, line_count);
        }
    }
    for (i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
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
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION
            || item->kind == CSANE_TOP_FUNCTION_DECLARATION) {
            int32 name = item->name_token;
            if (name < 0 || !csane_calls_code(file, name)) {
                continue;
            }
            csane_decl_header(file, item->header);
        }
    }
}

#endif /* CSANECHECK_DECLARATIONS_C */
