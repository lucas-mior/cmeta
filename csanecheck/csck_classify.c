// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CLASSIFY_C)
#define CSANECHECK_CLASSIFY_C

#include "cbase.h"
#include "csck_classify.h"

static StrFlex *csane_class_primitives[] = {
    SFLIT("void"), SFLIT("bool"), SFLIT("char"), SFLIT("short"),
    SFLIT("int"), SFLIT("long"), SFLIT("float"), SFLIT("double"),
    SFLIT("signed"), SFLIT("unsigned"), SFLIT("ldouble"), SFLIT("int8"),
    SFLIT("int16"), SFLIT("int32"), SFLIT("int64"), SFLIT("uint8"),
    SFLIT("uint16"), SFLIT("uint32"), SFLIT("uint64"), SFLIT("size_t"),
    SFLIT("ssize_t"), SFLIT("intptr_t"), SFLIT("uintptr_t"),
    SFLIT("ptrdiff_t"), SFLIT("wchar_t"), SFLIT("char8_t"),
    SFLIT("char16_t"), SFLIT("char32_t"), SFLIT("_Bool"),
    SFLIT("_Complex"), SFLIT("_Atomic"),
};

static StrFlex *csane_class_qualifiers[] = {
    SFLIT("const"), SFLIT("volatile"), SFLIT("static"), SFLIT("extern"),
    SFLIT("register"), SFLIT("inline"), SFLIT("restrict"),
    SFLIT("typedef"), SFLIT("struct"), SFLIT("union"), SFLIT("enum"),
    SFLIT("_Atomic"), SFLIT("_Thread_local"),
};

static bool
csane_class_word(Token *token, char *word, int32 word_len) {
    return token->kind == TOKEN_IDENT && TOKEN_IS(token, word, word_len);
}

static bool
csane_primitive(Token *token) {
    int32 count =
        SIZEOF(csane_class_primitives)/SIZEOF(csane_class_primitives[0]);
    for (int32 i = 0; i < count; i += 1) {
        StrFlex *word = csane_class_primitives[i];
        if (csane_class_word(token, word->data, word->len)) {
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
    if (csane_class_word(token, STRLIT("error"))) {
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
    int32 count =
        SIZEOF(csane_class_qualifiers)/SIZEOF(csane_class_qualifiers[0]);
    for (int32 i = 0; i < count; i += 1) {
        StrFlex *word = csane_class_qualifiers[i];
        if (csane_class_word(token, word->data, word->len)) {
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
csane_classify_invocation(Tokenization *tokens, TokenRange range, int32 name) {
    int32 next;
    enum CsaneNameKind kind;

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
    for (int32 i = name - 1; i >= range.first; i -= 1) {
        Token *previous = &tokens->tokens[i];

        if (token_is_trivia(previous) || previous->kind == TOKEN_PREPROC) {
            continue;
        }
        if (csane_type_name(previous)) {
            return CSANE_LOCAL_UNKNOWN;
        }
        break;
    }
    return CSANE_LOCAL_CALL;
}

#endif /* CSANECHECK_CLASSIFY_C */
