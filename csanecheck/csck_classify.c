// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CLASSIFY_C)
#define CSANECHECK_CLASSIFY_C

#include "cbase.h"
#include "csck_classify.h"

static bool
csane_class_word(Token *token, char *word) {
    return token->kind == TOKEN_IDENT && TOKEN_IS(token, word);
}

static bool
csane_primitive(Token *token) {
    static char *names[] = {
        "void", "bool", "char", "short", "int", "long", "float", "double",
        "signed", "unsigned", "ldouble", "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64", "size_t", "ssize_t",
        "intptr_t", "uintptr_t", "ptrdiff_t", "wchar_t", "char8_t",
        "char16_t", "char32_t", "_Bool", "_Complex", "_Atomic",
    };
    for (int32 i = 0; i < SIZEOF(names)/SIZEOF(names[0]); i += 1) {
        if (csane_class_word(token, names[i])) {
            return true;
        }
    }
    return false;
}

enum CsaneNameKind
csane_name_kind(Token *token) {
    bool upper = false;
    bool lower = false;

    if (!token || token->kind != TOKEN_IDENT || token->len <= 0) {
        return CSANE_NAME_OTHER;
    }
    if (csane_primitive(token)) {
        return CSANE_NAME_PRIMITIVE;
    }
    /* The documented lowercase macro exception is not a type. */
    if (csane_class_word(token, "error")) {
        return CSANE_NAME_MACRO;
    }
    for (int32 i = 0; i < token->len; i += 1) {
        char c = token->text[i];
        upper |= c >= 'A' && c <= 'Z';
        lower |= c >= 'a' && c <= 'z';
    }
    if (upper && !lower) {
        return CSANE_NAME_MACRO;
    }
    if (upper && token->text[0] >= 'A' && token->text[0] <= 'Z') {
        return CSANE_NAME_TYPE;
    }
    return CSANE_NAME_IDENTIFIER;
}

bool
csane_type_name(Token *token) {
    enum CsaneNameKind kind = csane_name_kind(token);
    return kind == CSANE_NAME_TYPE || kind == CSANE_NAME_PRIMITIVE;
}

static int32
csane_significant(Tokenization *tokens, TokenRange range, int32 from) {
    if (from < range.first) {
        from = range.first;
    }
    for (int32 i = from; i < range.end; i += 1) {
        if (!token_is_trivia(&tokens->tokens[i])
            && tokens->tokens[i].kind != TOKEN_PREPROC) {
            return i;
        }
    }
    return range.end;
}

static bool
csane_qualifier(Token *token) {
    static char *names[] = {
        "const", "volatile", "static", "extern", "register", "inline",
        "restrict", "typedef", "struct", "union", "enum", "_Atomic",
        "_Thread_local",
    };
    for (int32 i = 0; i < SIZEOF(names)/SIZEOF(names[0]); i += 1) {
        if (csane_class_word(token, names[i])) {
            return true;
        }
    }
    return false;
}

enum CsaneLocalKind
csane_classify_statement(Tokenization *tokens, TokenRange range) {
    int32 i;

    if (!tokens || range.first < 0 || range.end > tokens->token_count
        || range.first >= range.end) {
        return CSANE_LOCAL_UNKNOWN;
    }
    i = csane_significant(tokens, range, range.first);
    while (i < range.end && csane_qualifier(&tokens->tokens[i])) {
        i = csane_significant(tokens, range, i + 1);
    }
    if (i < range.end
        && (csane_type_name(&tokens->tokens[i])
            || csane_name_kind(&tokens->tokens[i]) == CSANE_NAME_MACRO)) {
        int32 next = csane_significant(tokens, range, i + 1);
        if (next < range.end
            && (tokens->tokens[next].kind == TOKEN_IDENT
                || TOKEN_IS(&tokens->tokens[next], "*"))) {
            return CSANE_LOCAL_DECLARATION;
        }
    }
    return CSANE_LOCAL_UNKNOWN;
}

enum CsaneLocalKind
csane_classify_invocation(Tokenization *tokens, TokenRange range,
                          int32 name) {
    int32 next;
    enum CsaneNameKind kind;
    int32 first;

    if (!tokens || range.first < 0 || range.end > tokens->token_count
        || name < range.first || name >= range.end
        || tokens->tokens[name].kind != TOKEN_IDENT) {
        return CSANE_LOCAL_UNKNOWN;
    }
    next = csane_significant(tokens, range, name + 1);
    if (next >= range.end || !TOKEN_IS(&tokens->tokens[next], "(")) {
        return CSANE_LOCAL_UNKNOWN;
    }
    kind = csane_name_kind(&tokens->tokens[name]);
    if (kind == CSANE_NAME_MACRO) {
        return CSANE_LOCAL_MACRO_INVOCATION;
    }
    if (kind == CSANE_NAME_PRIMITIVE || kind == CSANE_NAME_TYPE) {
        return CSANE_LOCAL_UNKNOWN;
    }
    /* A declaration header is not a call expression. */
    first = csane_significant(tokens, range, range.first);
    if (first < name) {
        int32 preceding = first;
        while (preceding < name) {
            int32 after = csane_significant(tokens, range, preceding + 1);
            if (after == name && csane_type_name(&tokens->tokens[preceding])) {
                return CSANE_LOCAL_UNKNOWN;
            }
            if (after >= name) {
                break;
            }
            preceding = after;
        }
    }
    return CSANE_LOCAL_CALL;
}

#endif /* CSANECHECK_CLASSIFY_C */
