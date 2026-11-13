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

static StrFlex *csane_decl_cbase_types[] = {
    SFLIT("schar"), SFLIT("uchar"), SFLIT("ushort"), SFLIT("uint"),
    SFLIT("ulong"), SFLIT("ullong"), SFLIT("llong"), SFLIT("bool32"),
    SFLIT("intptr"), SFLIT("uintptr"),
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
    if (first >= range.end || csane_decl_is(file, first, SL("typedef"))) {
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
        bool _i25_predicate = type < range.end;
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
    if (csane_decl_is(file, type, SL("struct"))
        || csane_decl_is(file, type, SL("union"))
        || csane_decl_is(file, type, SL("enum"))) {
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
        || csane_decl_is(file, second, SL("*"))) {
        return true;
    }
    if (csane_decl_is(file, second, SL("("))) {
        int32 inside = csane_decl_next(file, range, second);
        return inside < range.end
                   && csane_decl_is(file, inside, SL("*"));
    }
    return false;
}

static int32
csane_decl_comma(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    if (!csane_tokens_view(&view, file->tokens, range)) {
        return -1;
    }
    return csane_tokens_top_level(&view, range.first, SL(","));
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

        if (!csane_decl_is(file, i, SL("("))) {
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
                                      SL("=")) != before.end) {
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
        if (next >= range.end || csane_decl_is(file, next, SL(";"))
            || csane_decl_is(file, next, SL("{"))) {
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
    return csane_decl_word(file, token, SL("const"))
           || csane_decl_word(file, token, SL("volatile"))
           || csane_decl_word(file, token, SL("restrict"))
           || csane_decl_word(file, token, SL("_Atomic"));
}

static bool
csane_decl_aggregate(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, SL("struct"))
           || csane_decl_word(file, token, SL("union"))
           || csane_decl_word(file, token, SL("enum"));
}

static bool csane_decl_stdint_type(CsaneFile *, int32);

static bool
csane_decl_known_type(CsaneFile *file, int32 token) {
    if (token < 0 || token >= ARRAY_LEN(file->tokens->tokens)) {
        return false;
    }

    for (int32 i = 0; i < LENGTH(csane_decl_cbase_types); i += 1) {
        StrFlex *type = csane_decl_cbase_types[i];
        if (csane_decl_word(file, token, type->data, type->len)) {
            return true;
        }
    }
    /* FILE is declared by stdio.h, outside this file's typedef hints.
     * Its uppercase spelling otherwise looks like a macro name, so a
     * declaration such as FILE *save; would be mistaken for arithmetic. */
    return csane_decl_word(file, token, SL("FILE"))
           || csane_decl_stdint_type(file, token)
           || csane_type_name(&file->tokens->tokens[token])
           || csane_type_hints_typedef(file->type_hints,
                                       &file->tokens->tokens[token]);
}

/* A declaration preserves the surrounding block only if it introduces
 * an object in that scope. Typedefs and function prototypes do not count.
 * Do not interpret a multiplication expression as a pointer declaration
 * unless its leading name is a recognized type. */
bool
csane_declarations_variable_statement(CsaneFile *file, CsaneStatement *stmt) {
    TokenRange range = stmt->tokens;
    int32 first = csane_decl_next(file, range, range.first - 1);
    int32 type = first;

    if (first >= range.end || csane_decl_is(file, first, SL("typedef"))) {
        return false;
    }
    while (type < range.end) {
        bool qualifier = false;

        for (int32 i = 0; i < LENGTH(csane_decl_qualifiers); i += 1) {
            StrFlex *word = csane_decl_qualifiers[i];
            if (csane_decl_is(file, type, word->data, word->len)) {
                qualifier = true;
                break;
            }
        }
        if (!qualifier) {
            break;
        }
        type = csane_decl_next(file, range, type);
    }
    if (type >= range.end) {
        return false;
    }
    if (!csane_decl_known_type(file, type)
        && !csane_decl_aggregate(file, type)) {
        int32 next = csane_decl_next(file, range, type);

        /* Two adjacent names are a declaration even for project typedefs
         * absent from this file's hints. An unknown name followed by '*'
         * could instead be multiplication and must not count. */
        if (file->tokens->tokens[type].kind != TOKEN_IDENT
            || next >= range.end
            || file->tokens->tokens[next].kind != TOKEN_IDENT) {
            return false;
        }
    }
    if (csane_decl_type_start(file, range)) {
        int32 comma = csane_decl_comma(file, range);

        return comma < range.end
               || csane_decl_function_open(file, range) < 0;
    }
    /* Inline structure/union/enum definitions can declare an object after
     * their closing brace (including anonymous aggregate types). */
    if (csane_decl_aggregate(file, type)) {
        CsaneTokenView view;

        if (!csane_tokens_view(&view, file->tokens, range)) {
            return false;
        }
        for (int32 i = csane_decl_next(file, range, type);
             i < range.end; i = csane_decl_next(file, range, i)) {
            if (csane_decl_is(file, i, SL("{"))) {
                int32 close = csane_tokens_match(&view, i);
                int32 next = csane_decl_next(file, range, close);

                return close > i && next < range.end
                       && (file->tokens->tokens[next].kind == TOKEN_IDENT
                           || csane_decl_is(file, next, SL("*")));
            }
        }
    }
    return false;
}

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

    if (!csane_decl_word(file, token, SL("long"))) {
        return false;
    }
    next = csane_decl_next(file, range, token);
    return next < range.end && csane_decl_word(file, next, SL("double"));
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
            if ((csane_decl_word(file, token, SL("long"))
                 || csane_decl_word(file, token, SL("int")))
                && modifier >= 0) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            break;
        }
        if (csane_decl_word(file, token, SL("signed"))
            || csane_decl_word(file, token, SL("unsigned"))) {
            if (modifier < 0) {
                modifier = token;
            }
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_word(file, token, SL("short"))
            || csane_decl_word(file, token, SL("int"))
            || csane_decl_word(file, token, SL("long"))) {
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
            csane_rule_issue(file, part->offset, SL("TYPE-STDINT-T"),
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
    if (!csane_decl_word(file, first, SL("typedef"))) {
        return;
    }
    type = csane_decl_next(file, range, first);
    while (type < range.end
           && (csane_decl_storage(file, type)
               || csane_decl_cv_qualifier(file, type))) {
        type = csane_decl_next(file, range, type);
    }
    if (csane_decl_word(file, type, SL("enum"))
        && !(STREQUAL(file->path, file->path_len, "xenums.c")
             || ENDS_WITH(file->path, file->path_len, "/xenums.c"))) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         SL("TYPE-ENUM-TYPEDEF"), "do not typedef enums");
    }
    if (csane_decl_word(file, type, SL("union"))) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         SL("TYPE-UNION-TYPEDEF"), "do not typedef unions");
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

                    if (!csane_decl_is(file, nested_token, SL("*"))) {
                        nested_token = csane_tokens_next(&nested_view,
                                                         nested_token);
                        continue;
                    }
                    previous = csane_tokens_previous(&nested_view,
                                                     nested_token);
                    if (!csane_decl_is(file, previous, SL("("))) {
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
                    if (csane_decl_is(file, after, SL("("))) {
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

            if (csane_decl_is(file, token, SL("{"))
                || csane_decl_is(file, token, SL("["))) {
                int32 close = csane_tokens_match(&view, token);

                if (close < 0) {
                    _i28_result = false;
                    goto _i28_done;
                }
                token = csane_tokens_next(&view, close);
                continue;
            }
            if (!csane_decl_is(file, token, SL("*"))) {
                token = csane_tokens_next(&view, token);
                continue;
            }
            previous = csane_tokens_previous(&view, token);
            if (csane_decl_is(file, previous, SL("("))) {
                int32 close = csane_tokens_match(&view, previous);
                int32 after = -1;

                if (close >= 0) {
                    after = csane_tokens_next(&view, close);
                }
                if (close >= 0 && !csane_decl_is(file, after, SL("("))) {
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
                                             SL("TYPE-POINTER-TYPEDEF"),
                                             &file->tokens->tokens[alias])) {
            csane_rule_issue(file, file->tokens->tokens[first].offset,
                             SL("TYPE-POINTER-TYPEDEF"),
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
        int32 comma = csane_tokens_top_level(&view, first - 1, SL(","));
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
        equal = csane_tokens_top_level(&declarator, part.first - 1, SL("="));
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

            if (csane_decl_is(file, token, SL("{"))) {
                int32 close = csane_tokens_match(&declarator, token);

                if (close >= 0) {
                    token = csane_tokens_next(&declarator, close);
                    continue;
                }
            }
            if (csane_decl_is(file, token, SL("("))) {
                parentheses += 1;
            } else if (csane_decl_is(file, token, SL(")"))) {
                parentheses -= parentheses > 0;
            } else if (csane_decl_is(file, token, SL("["))) {
                brackets += 1;
            } else if (csane_decl_is(file, token, SL("]"))) {
                brackets -= brackets > 0;
            }
            if (csane_decl_word(file, token, SL("error"))
                && brackets == 0
                && (parentheses == 0
                    || csane_decl_is(file, previous, SL("*")))
                && !csane_decl_is(file, previous, SL("struct"))
                && !csane_decl_is(file, previous, SL("union"))
                && !csane_decl_is(file, previous, SL("enum"))
                && !csane_decl_is(file, previous, SL("."))
                && !csane_decl_is(file, previous, SL("->"))
                && !csane_decl_is(file, previous, SL(":"))
                && !csane_decl_is(file, next, SL("("))
                && !csane_decl_is(file, next, SL("*"))
                && (next >= part.end
                    || file->tokens->tokens[next].kind != TOKEN_IDENT)) {
                csane_rule_issue(file, file->tokens->tokens[token].offset,
                                 SL("STYLE-ERROR-NAME"),
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
        int32 comma = csane_tokens_top_level(&view, first - 1, SL(","));
        TokenRange parameter = {first, args.end};
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

                if (!csane_decl_is(file, cursor, SL("("))) {
                    cursor = csane_tokens_next(&nested, cursor);
                    continue;
                }
                pointer = csane_tokens_next(&nested, cursor);
                close = csane_tokens_match(&nested, cursor);
                if (!csane_decl_is(file, pointer, SL("*"))
                    || close < 0) {
                    cursor = csane_tokens_next(&nested, cursor);
                    continue;
                }
                open = csane_tokens_next(&nested, close);
                args_close = csane_tokens_match(&nested, open);
                if (csane_decl_is(file, open, SL("("))
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

                if (csane_decl_is(in_file, token, SL("["))) {
                    brackets += 1;
                } else if (csane_decl_is(in_file, token, SL("]"))) {
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
                        && !csane_decl_is(in_file, previous, SL("*")))
                    || csane_decl_is(in_file, previous, SL("struct"))
                    || csane_decl_is(in_file, previous, SL("union"))
                    || csane_decl_is(in_file, previous, SL("enum"))
                    || (next < parameter.end
                        && !csane_decl_is(in_file, next, SL(")"))
                        && !csane_decl_is(in_file, next, SL("["))
                        && !csane_decl_is(in_file, next, SL("(")))) {
                    token = csane_tokens_next(&nested_view_2, token);
                    continue;
                }
                csane_rule_issue(in_file, word->offset,
                                 SL("STYLE-PARAM-IJK"),
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
    if (!csane_decl_word(file, name, SL("main"))) {
        csane_decl_type_prefix_check(file, result);
    }
    csane_decl_parameter_checks(file, args,
                                !csane_decl_word(file, name, SL("main")));
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < close) {
        if (csane_decl_word(file, token, SL("const"))) {
            if (!csane_policy_rule_allowed(file->policy, file->path,
                                           file->path_len,
                                           SL("TYPE-CONST-SIGNATURE"),
                                           &file->tokens->tokens[name])) {
                csane_rule_issue(file, file->tokens->tokens[token].offset,
                                 SL("TYPE-CONST-SIGNATURE"),
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
            int32 token = csane_decl_next(file, range, range.first - 1);
            if (token >= range.end) {
                _i26_result = false;
                goto _i26_done;
            }
            if (csane_decl_word(file, token, SL("typedef"))) {
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
            if (csane_decl_word(file, token, SL("signed"))
                || csane_decl_word(file, token, SL("unsigned"))
                || csane_decl_stdint_type(file, token)) {
                _i26_result = true;
                goto _i26_done;
            }
            if (csane_decl_known_type(file, token)) {
                int32 next = csane_decl_next(file, range, token);
                if (next >= range.end) {
                    _i26_result = false;
                    goto _i26_done;
                }
                if (file->tokens->tokens[next].kind == TOKEN_IDENT
                    || csane_decl_is(file, next, SL("*"))) {
                    _i26_result = true;
                    goto _i26_done;
                }
                if (csane_decl_is(file, next, SL("("))) {
                    int32 inside = csane_decl_next(file, range, next);
                    _i26_result = inside < range.end
                           && csane_decl_is(file, inside, SL("*"));
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
                if (csane_decl_is(file, token, SL("{"))) {
                    int32 close = csane_tokens_match(&view, token);

                    if (close > token) {
                        token = csane_tokens_next(&view, close);
                        continue;
                    }
                }
                if (csane_decl_is(file, token, SL("("))) {
                    int32 pointer = csane_tokens_next(&view, token);
                    int32 close = csane_tokens_match(&view, token);

                    if (csane_decl_is(file, pointer, SL("*"))
                        && close > pointer) {
                        int32 args = csane_tokens_next(&view, close);
                        int32 args_close = csane_tokens_match(&view, args);

                        if (csane_decl_is(file, args, SL("("))
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
    if (!csane_decl_word(file, first, SL("typedef"))) {
        csane_decl_error_names(file, range);
    }
    line = csane_local_decl_line(file, first);
    if ((line > 0 && line < line_count && lines[line])
        || (comma >= 0 && comma < range.end)) {
        csane_rule_issue(file, file->tokens->tokens[first].offset,
                         SL("AST-DECL-ONE"),
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
            int32 open = csane_decl_next(file, range, i);
            if (csane_decl_is(file, open, SL("("))) {
                CsaneTokenView view;

                if (csane_tokens_view(&view, file->tokens, range)) {
                    int32 close = csane_tokens_match(&view, open);
                    if (close >= 0) {
                        TokenRange header;
                        CsaneTokenView inner;
                        int32 begin;
                        int32 semi;

                        header = (TokenRange){open + 1, close};
                        (void)csane_tokens_view(&inner, file->tokens, header);
                        begin = open + 1;
                        semi = csane_tokens_top_level(&inner, begin, SL(";"));
                        if (semi >= 0 && semi < close) {
                            header.end = semi;
                            csane_decl_one(file, header, lines, line_count);
                        }
                        i = close;
                    }
                }
            }
        } else if (csane_decl_is(file, i, SL("("))
                   || csane_decl_is(file, i, SL("["))) {
            CsaneTokenView view;

            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, SL("{"))) {
            CsaneTokenView view;

            if (csane_tokens_view(&view, file->tokens, range)) {
                int32 close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    TokenRange prefix;
                    bool type_prefix;
                    bool aggregate_type = false;

                    prefix = (TokenRange){start, i};
                    type_prefix = csane_decl_type_start(file, prefix);
                    if (type_prefix) {
                        int32 cursor = start;
                        while (cursor < i) {
                            if (csane_decl_is(file, cursor, SL("struct"))
                                || csane_decl_is(file, cursor,
                                                 SL("union"))) {
                                aggregate_type = true;
                            }
                            if (csane_decl_is(file, cursor, SL("="))) {
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
        } else if (csane_decl_is(file, i, SL(";"))) {
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
            || (!csane_decl_is(in_file, suffix, SL(";"))
                && !csane_decl_is(in_file, suffix, SL("{")))) {
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
                             SL("FMT-FUNC-WRAP"),
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
        int32 current = csane_decl_next(file, args, comma);
        if (current >= args.end) {
            break;
        }
        if (csane_local_decl_line(file, current)
            > csane_local_decl_line(file, previous)
            && tokenization_token_location(file->tokens, current).column
               != first_column) {
            csane_rule_issue(file, file->tokens->tokens[current].offset,
                             SL("FMT-FUNC-ALIGN"),
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
            /* Type members form a separate declaration sequence. */
            int32 open = item->tokens.first;
            while (open < item->tokens.end
                   && !csane_decl_is(file, open, SL("{"))) {
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

/* Only an uninitialized object declaration can start a candidate group.
 * Keep unsupported declarators in the group so that an incomplete or
 * complicated group can never be reduced to a misleading suffix match. */
static bool
csane_decl_postponed_uninitialized(CsaneFile *file, int32 index) {
    CsaneStatement *stmt = &file->statements->items[index];
    CsaneTokenView view;
    TokenRange range = stmt->tokens;

    if (stmt->kind != CSANE_STMT_SIMPLE
        || !csane_declarations_variable_statement(file, stmt)
        || !csane_tokens_view(&view, file->tokens, range)) {
        return false;
    }
    return csane_tokens_top_level(&view, range.first - 1, SL("="))
           == range.end;
}

/* Leave complex declarators, storage-duration modifiers and volatile objects
 * alone. In particular, an assignment is not equivalent to initializing a
 * static object on every execution of its containing block. */
static int32
csane_decl_postponed_name(CsaneFile *file, int32 index) {
    CsaneStatement *stmt = &file->statements->items[index];
    TokenRange range = stmt->tokens;
    CsaneTokenView view;
    int32 last;
    int32 name;
    int32 token;

    if (!csane_decl_postponed_uninitialized(file, index)
        || !csane_tokens_view(&view, file->tokens, range)) {
        return -1;
    }
    last = csane_tokens_previous(&view, range.end);
    name = csane_tokens_previous(&view, last);
    if (!csane_decl_is(file, last, SL(";")) || name < range.first
        || file->tokens->tokens[name].kind != TOKEN_IDENT) {
        return -1;
    }
    token = csane_tokens_next(&view, range.first - 1);
    for (; token < name; token = csane_tokens_next(&view, token)) {
        Token *word = &file->tokens->tokens[token];

        if (csane_decl_is(file, token, SL("static"))
            || csane_decl_is(file, token, SL("extern"))
            || csane_decl_is(file, token, SL("typedef"))
            || csane_decl_is(file, token, SL("_Thread_local"))
            || csane_decl_is(file, token, SL("volatile"))
            || csane_decl_is(file, token, SL("_Atomic"))
            || csane_decl_is(file, token, SL("const"))) {
            return -1;
        }
        if (word->kind != TOKEN_IDENT
            && !csane_decl_is(file, token, SL("*"))) {
            return -1;
        }
    }
    return name;
}

/* Require an ordinary statement of the form NAME = expression; . */
static int32
csane_decl_postponed_assignment(CsaneFile *file, int32 index) {
    CsaneStatement *stmt = &file->statements->items[index];
    TokenRange range = stmt->tokens;
    CsaneTokenView view;
    int32 name;
    int32 equal;
    int32 last;

    if (stmt->kind != CSANE_STMT_SIMPLE
        || !csane_tokens_view(&view, file->tokens, range)) {
        return -1;
    }
    name = csane_tokens_next(&view, range.first - 1);
    equal = csane_tokens_next(&view, name);
    last = csane_tokens_previous(&view, range.end);
    if (name >= range.end
        || file->tokens->tokens[name].kind != TOKEN_IDENT
        || !csane_decl_is(file, equal, SL("="))
        || !csane_decl_is(file, last, SL(";"))
        || csane_tokens_next(&view, equal) >= last
        || csane_tokens_top_level(&view, equal, SL(",")) < last) {
        return -1;
    }
    return name;
}

/* Only suggest moving an assignment when both statements occupy one
 * physical line and the resulting initializer also fits on one line. */
static bool
csane_decl_postponed_inline(CsaneFile *file, int32 declaration,
                            int32 assignment, int32 name) {
    CsaneStatement *decl = &file->statements->items[declaration];
    CsaneStatement *assign = &file->statements->items[assignment];
    CsaneTokenView decl_view;
    CsaneTokenView assign_view;
    int32 decl_first;
    int32 decl_last;
    int32 assign_first;
    int32 assign_last;
    int32 equal;
    int32 expression;
    int32 length;

    if (!csane_tokens_view(&decl_view, file->tokens, decl->tokens)
        || !csane_tokens_view(&assign_view, file->tokens, assign->tokens)) {
        return false;
    }
    decl_first = csane_tokens_next(&decl_view, decl->tokens.first - 1);
    decl_last = csane_tokens_previous(&decl_view, decl->tokens.end);
    assign_first = csane_tokens_next(&assign_view, assign->tokens.first - 1);
    assign_last = csane_tokens_previous(&assign_view, assign->tokens.end);
    if (csane_local_decl_line(file, decl_first)
        != csane_local_decl_line(file, decl_last)
        || csane_local_decl_line(file, assign_first)
           != csane_local_decl_line(file, assign_last)) {
        return false;
    }
    equal = csane_tokens_next(&assign_view, assign_first);
    expression = csane_tokens_next(&assign_view, equal);
    if (expression >= assign_last) {
        return false;
    }
    length = tokenization_token_location(file->tokens, decl_first).column
             + file->tokens->tokens[name].offset
             + file->tokens->tokens[name].len
             - file->tokens->tokens[decl_first].offset + 3
             + file->tokens->tokens[assign_last].offset
             + file->tokens->tokens[assign_last].len
             - file->tokens->tokens[expression].offset;
    return length <= file->column_limit;
}

static bool
csane_post_adjacent(CsaneFile *file, int32 left, int32 right) {
    CsaneStatement *a = &file->statements->items[left];
    CsaneStatement *b = &file->statements->items[right];

    if (a->parent != b->parent || a->function != b->function
        || a->owner_region != b->owner_region
        || a->source.end > b->source.start) {
        return false;
    }
    for (int32 i = a->tokens.end; i < b->tokens.first; i += 1) {
        if (!token_is_trivia(&file->tokens->tokens[i])) {
            return false;
        }
    }
    return true;
}

/* Function-body statements may be stored as root statements with no parent
 * or sibling links. Their original indices still preserve source order. */
static int32
csane_decl_postponed_next(CsaneStatements *tree, int32 index) {
    CsaneStatement *stmt = &tree->items[index];

    if (stmt->parent >= 0) {
        return stmt->next_sibling;
    }
    for (int32 i = index + 1; i < ARRAY_LEN(tree->items); i += 1) {
        CsaneStatement *next = &tree->items[i];

        if (next->parent == -1 && next->function == stmt->function
            && next->owner_region == stmt->owner_region) {
            return i;
        }
    }
    return -1;
}

/* A maximal run of uninitialized declarations must be followed by exactly
 * the matching number of plain assignments, in declaration order. This
 * preserves evaluation order and forbids partial-group diagnostics. */
void
csane_declarations_postponed_check(CsaneFile *file) {
    CsaneStatements *tree = file->statements;

    for (int32 parent = -1; parent < ARRAY_LEN(tree->items); parent += 1) {
        int32 current = parent < 0 ? -1 : tree->items[parent].first_child;

        if (parent < 0) {
            for (int32 i = 0; i < ARRAY_LEN(tree->items); i += 1) {
                if (tree->items[i].parent == -1) {
                    current = i;
                    break;
                }
            }
        }

        while (current >= 0) {
            int32 *names = NULL;
            int32 end = current;
            int32 last = -1;
            int32 next;
            bool supported = true;
            bool complete = true;

            if (!csane_decl_postponed_uninitialized(file, current)) {
                current = csane_decl_postponed_next(tree, current);
                continue;
            }
            while (end >= 0
                   && csane_decl_postponed_uninitialized(file, end)) {
                int32 name = csane_decl_postponed_name(file, end);

                last = end;

                if (name < 0) {
                    supported = false;
                }
                ARRAY_PUSH(names, name);
                next = csane_decl_postponed_next(tree, end);
                if (next < 0
                    || !csane_post_adjacent(file, end, next)) {
                    end = next;
                    break;
                }
                end = next;
            }
            if (supported && end >= 0
                && csane_post_adjacent(file, last, end)) {
                int32 declaration = current;

                next = end;
                for (int32 i = 0; i < ARRAY_LEN(names); i += 1) {
                    int32 assignment;
                    Token *declared = &file->tokens->tokens[names[i]];

                    if (next < 0 || !complete) {
                        complete = false;
                        break;
                    }
                    assignment = csane_decl_postponed_assignment(file, next);
                    if (assignment < 0) {
                        complete = false;
                        break;
                    }
                    if (declared->len
                        != file->tokens->tokens[assignment].len
                        || memcmp64(declared->text,
                                    file->tokens->tokens[assignment].text,
                                    declared->len) != 0) {
                        complete = false;
                        break;
                    }
                    if (!csane_decl_postponed_inline(file, declaration, next,
                                                     names[i])) {
                        complete = false;
                        break;
                    }
                    if (i + 1 < ARRAY_LEN(names)) {
                        int32 following = csane_decl_postponed_next(tree, next);

                        if (following < 0
                            || !csane_post_adjacent(file, next, following)) {
                            complete = false;
                            break;
                        }
                        next = following;
                        declaration = csane_decl_postponed_next(tree,
                                                                declaration);
                    }
                }
                if (complete) {
                    int32 offset = file->tokens->tokens[names[0]].offset;

                    csane_rule_issue(file, offset, SL("STYLE-POSTPONED-INIT"),
                                     "initialize the complete declaration "
                                     "group at its declarations");
                }
            }
            ARRAY_FREE(names);
            current = end;
        }
    }
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
