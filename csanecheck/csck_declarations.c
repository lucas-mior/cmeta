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
    static char *statements[] = {
        "return", "if", "for", "while", "switch", "do", "else",
        "case", "default", "goto", "break", "continue",
    };
    int32 first;
    int32 type;
    int32 second;
    int32 j;

    first = csane_decl_next(file, range, range.first - 1);
    if (first >= range.end || csane_decl_is(file, first, "typedef")) {
        return false;
    }
    for (j = 0; j < SIZEOF(statements)/SIZEOF(statements[0]); j += 1) {
        if (csane_decl_is(file, first, statements[j])) {
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
    if (csane_decl_is(file, type, "struct")
        || csane_decl_is(file, type, "union")
        || csane_decl_is(file, type, "enum")) {
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
        || csane_decl_is(file, second, "*")) {
        return true;
    }
    if (csane_decl_is(file, second, "(")) {
        int32 inside;

        inside = csane_decl_next(file, range, second);
        return inside < range.end && csane_decl_is(file, inside, "*");
    }
    return false;
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
    int32 i;

    i = csane_decl_next(file, range, range.first - 1);
    for (; i < range.end; i = csane_decl_next(file, range, i)) {
        TokenRange before;
        int32 previous;
        CsaneTokenView prefix;
        CsaneTokenView view;
        int32 close;
        int32 next;

        if (!csane_decl_is(file, i, "(")) {
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
                                      "=", 1) != before.end) {
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
        if (next >= range.end || csane_decl_is(file, next, ";")
            || csane_decl_is(file, next, "{")) {
            return i;
        }
    }
    return -1;
}

static bool
csane_decl_word(CsaneFile *file, int32 token, char *word) {
    return token >= 0 && token < file->tokens->token_count
           && file->tokens->tokens[token].kind == TOKEN_IDENT
           && TOKEN_IS(&file->tokens->tokens[token], word);
}

static bool
csane_decl_storage(CsaneFile *file, int32 token) {
    static char *words[] = {
        "typedef", "static", "extern", "auto", "register", "inline",
        "_Thread_local",
    };
    int32 i;

    for (i = 0; i < SIZEOF(words)/SIZEOF(words[0]); i += 1) {
        if (csane_decl_word(file, token, words[i])) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_cv_qualifier(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, "const")
           || csane_decl_word(file, token, "volatile")
           || csane_decl_word(file, token, "restrict")
           || csane_decl_word(file, token, "_Atomic");
}

static bool
csane_decl_aggregate(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, "struct")
           || csane_decl_word(file, token, "union")
           || csane_decl_word(file, token, "enum");
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
    if (csane_decl_word(file, token, "typedef")) {
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
    if (csane_decl_word(file, token, "signed")
        || csane_decl_word(file, token, "unsigned")
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
            || csane_decl_is(file, next, "*")) {
            return true;
        }
        if (csane_decl_is(file, next, "(")) {
            int32 inside;

            inside = csane_decl_next(file, range, next);
            return inside < range.end && csane_decl_is(file, inside, "*");
        }
    }
    return false;
}

static bool
csane_decl_size_type(CsaneFile *file, int32 token) {
    return csane_decl_word(file, token, "size_t")
           || csane_decl_word(file, token, "ptrdiff_t")
           || csane_decl_word(file, token, "ssize_t");
}

static bool
csane_decl_stdint_type(CsaneFile *file, int32 token) {
    static char *words[] = {
        "int8_t", "uint8_t", "int16_t", "uint16_t",
        "int32_t", "uint32_t", "int64_t", "uint64_t",
    };
    int32 i;

    for (i = 0; i < SIZEOF(words)/SIZEOF(words[0]); i += 1) {
        if (csane_decl_word(file, token, words[i])) {
            return true;
        }
    }
    return false;
}

static bool
csane_decl_long_double(CsaneFile *file, TokenRange range, int32 token) {
    int32 next;

    if (!csane_decl_word(file, token, "long")) {
        return false;
    }
    next = csane_decl_next(file, range, token);
    return next < range.end && csane_decl_word(file, next, "double");
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
            if ((csane_decl_word(file, token, "long")
                 || csane_decl_word(file, token, "int"))
                && modifier >= 0) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            break;
        }
        if (csane_decl_word(file, token, "signed")
            || csane_decl_word(file, token, "unsigned")) {
            if (modifier < 0) {
                modifier = token;
            }
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_word(file, token, "short")
            || csane_decl_word(file, token, "int")
            || csane_decl_word(file, token, "long")) {
            if (csane_decl_long_double(file, range, token)) {
                token = csane_decl_next(file, range, token);
                continue;
            }
            base = true;
            modifier = token;
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_size_type(file, token)) {
            csane_rule_issue(file, part->offset, "TYPE-SIZE-T",
                             "avoid size_t, ptrdiff_t, and ssize_t in "
                             "project declarations");
            base = true;
            token = csane_decl_next(file, range, token);
            continue;
        }
        if (csane_decl_stdint_type(file, token)) {
            csane_rule_issue(file, part->offset, "TYPE-STDINT-T",
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
csane_decl_dynamic_bound(CsaneFile *file, TokenRange range) {
    int32 token;

    token = csane_decl_next(file, range, range.first - 1);
    while (token < range.end) {
        Token *part = &file->tokens->tokens[token];
        int32 next;

        if (part->kind == TOKEN_IDENT
            && !csane_decl_word(file, token, "sizeof")
            && !csane_decl_word(file, token, "_Alignof")) {
            next = csane_decl_next(file, range, token);
            if (next < range.end && csane_decl_is(file, next, "(")) {
                enum CsaneLocalKind kind;

                kind = csane_classify_invocation(file->tokens, range, token);
                if (kind == CSANE_LOCAL_CALL) {
                    return true;
                }
            }
        }
        if (csane_decl_is(file, token, "->")
            || csane_decl_is(file, token, ".")
            || csane_decl_is(file, token, "++")
            || csane_decl_is(file, token, "--")) {
            return true;
        }
        token = csane_decl_next(file, range, token);
    }
    return false;
}

/* Only definitely dynamic bounds are diagnosed. A bare identifier such as
 * count may be a macro or enumerator, so it remains intentionally unknown. */
static void
csane_decl_vla_check(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 token;

    if (!csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < range.end) {
        int32 close;
        TokenRange bound;

        if (csane_decl_is(file, token, "=")) {
            return;
        }
        if (csane_decl_is(file, token, "{")) {
            return;
        }
        if (!csane_decl_is(file, token, "[")) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        close = csane_tokens_match(&view, token);
        if (close < 0 || close >= range.end) {
            return;
        }
        bound = (TokenRange){token + 1, close};
        if (csane_decl_dynamic_bound(file, bound)) {
            csane_rule_issue(file, file->tokens->tokens[token].offset,
                             "TYPE-VLA",
                             "variable-length arrays are forbidden");
        }
        token = csane_tokens_next(&view, close);
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

        if (!csane_decl_is(file, token, "*")) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        previous = csane_tokens_previous(&view, token);
        if (!csane_decl_is(file, previous, "(")) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        close = csane_tokens_match(&view, previous);
        if (close < 0) {
            return false;
        }
        after = csane_tokens_next(&view, close);
        if (csane_decl_is(file, after, "(")) {
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

        if (csane_decl_is(file, token, "{")
            || csane_decl_is(file, token, "[")) {
            int32 close;

            close = csane_tokens_match(&view, token);
            if (close < 0) {
                return false;
            }
            token = csane_tokens_next(&view, close);
            continue;
        }
        if (!csane_decl_is(file, token, "*")) {
            token = csane_tokens_next(&view, token);
            continue;
        }
        previous = csane_tokens_previous(&view, token);
        if (csane_decl_is(file, previous, "(")) {
            int32 close;
            int32 after;

            close = csane_tokens_match(&view, previous);
            after = -1;
            if (close >= 0) {
                after = csane_tokens_next(&view, close);
            }
            if (close >= 0 && !csane_decl_is(file, after, "(")) {
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
    int32 length;

    length = strlen32(file->path);
    return (length == 8 && !memcmp64(file->path, "xenums.c", 8))
           || (length > 8 && file->path[length - 9] == '/'
               && !memcmp64(file->path + length - 8, "xenums.c", 8));
}

static void
csane_decl_typedef_check(CsaneFile *file, TokenRange range) {
    int32 first;
    int32 type;

    first = csane_decl_next(file, range, range.first - 1);
    if (!csane_decl_word(file, first, "typedef")) {
        return;
    }
    type = csane_decl_next(file, range, first);
    while (type < range.end
           && (csane_decl_storage(file, type)
               || csane_decl_cv_qualifier(file, type))) {
        type = csane_decl_next(file, range, type);
    }
    if (csane_decl_word(file, type, "enum") && !csane_decl_xenums(file)) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         "TYPE-ENUM-TYPEDEF", "do not typedef enums");
    }
    if (csane_decl_word(file, type, "union")) {
        csane_rule_issue(file, file->tokens->tokens[type].offset,
                         "TYPE-UNION-TYPEDEF", "do not typedef unions");
    }
    if (csane_decl_pointer_typedef(file, range)) {
        csane_rule_issue(file, file->tokens->tokens[first].offset,
                         "TYPE-POINTER-TYPEDEF",
                         "do not typedef pointers except function pointers");
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

        comma = csane_tokens_top_level(&view, first - 1, ",", 1);
        parameter = (TokenRange){first, args.end};
        if (comma >= first && comma < args.end) {
            parameter.end = comma;
        }
        csane_decl_type_prefix_check(file, parameter);
        csane_decl_vla_check(file, parameter);
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
    if (!csane_decl_word(file, name, "main")) {
        csane_decl_type_prefix_check(file, result);
        csane_decl_parameter_checks(file, args);
    }
    token = csane_tokens_next(&view, range.first - 1);
    while (token >= range.first && token < close) {
        if (csane_decl_word(file, token, "const")) {
            csane_rule_issue(file, file->tokens->tokens[token].offset,
                             "TYPE-CONST-SIGNATURE",
                             "const qualifiers are forbidden in function "
                             "signatures");
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
    csane_decl_vla_check(file, range);
    csane_decl_typedef_check(file, range);
}

static void
csane_decl_struct_typedef_check(CsaneFile *file, TokenRange range) {
    CsaneTokenView view;
    int32 first;
    int32 token;

    if (!csane_tokens_view(&view, file->tokens, range)) {
        return;
    }
    first = csane_tokens_next(&view, range.first - 1);
    if (first < range.end && csane_decl_word(file, first, "typedef")) {
        return;
    }
    token = first;
    while (token >= range.first && token < range.end) {
        if (csane_decl_word(file, token, "struct")) {
            int32 cursor;

            cursor = csane_tokens_next(&view, token);
            if (cursor < range.end
                && file->tokens->tokens[cursor].kind == TOKEN_IDENT) {
                cursor = csane_tokens_next(&view, cursor);
            }
            if (csane_decl_is(file, cursor, "{")
                || csane_decl_is(file, cursor, ";")) {
                csane_rule_issue(file, file->tokens->tokens[token].offset,
                                 "TYPE-STRUCT-TYPEDEF",
                                 "top-level structs must be typedefed");
            }
            return;
        }
        if (csane_decl_is(file, token, "{")) {
            return;
        }
        token = csane_tokens_next(&view, token);
    }
}

static bool
csane_decl_statement(CsaneFile *file, CsaneStatement *statement) {
    return statement->kind == CSANE_STMT_SIMPLE
           && csane_decl_candidate(file, statement->tokens);
}

static bool
csane_decl_unknown_type_shape(CsaneFile *file, TokenRange range) {
    int32 type;
    int32 next;

    if (csane_classify_statement(file->tokens, range)
        == CSANE_LOCAL_DECLARATION) {
        return true;
    }
    type = csane_decl_next(file, range, range.first - 1);
    while (type < range.end
           && (csane_decl_storage(file, type)
               || csane_decl_cv_qualifier(file, type))) {
        type = csane_decl_next(file, range, type);
    }
    if (type >= range.end
        || file->tokens->tokens[type].kind != TOKEN_IDENT
        || csane_decl_known_type(file, type)) {
        return false;
    }
    next = csane_decl_next(file, range, type);
    if (next >= range.end) {
        return false;
    }
    if (file->tokens->tokens[next].kind == TOKEN_IDENT
        || csane_decl_is(file, next, "*")) {
        return true;
    }
    if (csane_decl_is(file, next, "(")) {
        int32 inside;

        inside = csane_decl_next(file, range, next);
        return inside < range.end && csane_decl_is(file, inside, "*");
    }
    return false;
}

static void
csane_decl_after_statement_check(CsaneFile *file) {
    uint8 *reported;
    int32 bytes;
    int32 i;

    bytes = file->tokens->token_count*SIZEOF(*reported);
    reported = malloc2(bytes);
    memset64(reported, 0, bytes);
    for (i = 0; i < file->statements->count; i += 1) {
        CsaneStatement *statement = &file->statements->items[i];
        int32 first;
        bool code = false;
        bool ambiguous = false;
        int32 j;

        if (!csane_decl_statement(file, statement)) {
            continue;
        }
        first = csane_decl_next(file, statement->tokens,
                                statement->tokens.first - 1);
        if (first < 0 || first >= file->tokens->token_count
            || reported[first]) {
            continue;
        }
        for (j = 0; j < i; j += 1) {
            CsaneStatement *previous = &file->statements->items[j];

            if (previous->function != statement->function
                || previous->owner_region != statement->owner_region
                || previous->parent != statement->parent
                || previous->source.start >= statement->source.start) {
                continue;
            }
            if (csane_decl_statement(file, previous)) {
                continue;
            }
            if (previous->kind == CSANE_STMT_OPAQUE
                || (previous->kind == CSANE_STMT_SIMPLE
                    && csane_decl_unknown_type_shape(file,
                                                     previous->tokens))) {
                ambiguous = true;
                continue;
            }
            if (previous->kind == CSANE_STMT_CASE
                || previous->kind == CSANE_STMT_DEFAULT
                || previous->kind == CSANE_STMT_LABEL) {
                continue;
            }
            code = true;
        }
        if (code && !ambiguous) {
            csane_rule_issue(file, file->tokens->tokens[first].offset,
                             "DECL-AFTER-STMT",
                             "declare variables before statements in the "
                             "same block");
            reported[first] = 1;
        }
    }
    free2(reported, bytes);
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
    int32 start;
    int32 i;

    start = csane_decl_next(file, range, range.first - 1);
    for (i = start; i < range.end; i = csane_decl_next(file, range, i)) {
        Token *token = &file->tokens->tokens[i];

        if (TOKEN_IS(token, "for")) {
            int32 open;

            open = csane_decl_next(file, range, i);
            if (csane_decl_is(file, open, "(")) {
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
                        semi = csane_tokens_top_level(&inner, begin, ";", 1);
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
                int32 close;

                close = csane_tokens_match(&view, i);
                if (close >= 0) {
                    i = close;
                }
            }
        } else if (csane_decl_is(file, i, "{")) {
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
    int32 open;
    CsaneTokenView view;
    int32 close;
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
    int32 line_count = 2;
    uint8 *lines;
    int32 i;

    if (file->is_ryu || csane_calls_generated(file->path)) {
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
        } else if (item->kind == CSANE_TOP_DECLARATION) {
            csane_decl_struct_typedef_check(file, item->tokens);
        } else if (item->kind == CSANE_TOP_TYPE_DECLARATION) {
            int32 open;

            csane_decl_struct_typedef_check(file, item->tokens);
            /* Type members form a separate declaration sequence. */
            open = item->tokens.first;
            while (open < item->tokens.end
                   && !csane_decl_is(file, open, "{")) {
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
    csane_decl_after_statement_check(file);
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
