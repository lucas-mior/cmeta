// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STRING_LITERALS_C)
#define CSANECHECK_STRING_LITERALS_C

#include "cbase.h"
#include "csck.h"

/* A concatenated literal must have a compatible encoding prefix, and its
 * delimiters must be complete. Character and numeric literals are excluded. */
static int32
csane_string_prefix(Token *token) {
    int32 prefix = 0;

    if (token->kind != TOKEN_LITERAL || token->len < 2) {
        return -1;
    }
    if (token->text[0] == 'L' || token->text[0] == 'U'
        || token->text[0] == 'u') {
        prefix = 1;
        if (token->text[0] == 'u' && token->len > 2
            && token->text[1] == '8') {
            prefix = 2;
        }
    }
    if (token->len < prefix + 2 || token->text[prefix] != '"'
        || token->text[token->len - 1] != '"') {
        return -1;
    }
    for (int32 i = 0; i < token->len; i += 1) {
        if (token->text[i] == '\n' || token->text[i] == '\r') {
            return -1;
        }
    }
    return prefix;
}

static bool
csane_string_whitespace(char *text, int32 first, int32 end) {
    for (int32 i = first; i < end; i += 1) {
        if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r'
            && text[i] != '\n') {
            return false;
        }
    }
    return true;
}

/* Joining can extend a trailing octal or hexadecimal escape. Refuse such
 * pairs instead of issuing a suggestion that changes the literal's bytes. */
static bool
csane_string_safe_boundary(Token *left, Token *right, int32 prefix) {
    char *text = left->text;
    int32 end = left->len - 1;
    char first = right->text[prefix + 1];
    int32 digits = 0;
    int32 start;

    if (end > prefix + 1 && text[end - 1] == '?' && first == '?') {
        return false;
    }
    if (end > prefix + 2 && text[end - 1] == '?'
        && text[end - 2] == '?'
        && (first == '=' || first == '/' || first == '\''
            || first == '(' || first == ')' || first == '!'
            || first == '<' || first == '>' || first == '-')) {
        return false;
    }
    if (first >= '0' && first <= '7') {
        start = end;
        while (start > prefix + 1 && end - start < 3
               && text[start - 1] >= '0' && text[start - 1] <= '7') {
            start -= 1;
            digits += 1;
        }
        if (digits > 0 && digits < 3 && start > prefix + 1
            && text[start - 1] == '\\') {
            int32 slashes = 0;
            for (int32 i = start - 1; i > prefix && text[i] == '\\'; i -= 1) {
                slashes += 1;
            }
            if (slashes % 2 == 1) {
                return false;
            }
        }
    }
    if ((first >= '0' && first <= '9')
        || (first >= 'a' && first <= 'f')
        || (first >= 'A' && first <= 'F')) {
        start = end;
        while (start > prefix + 1
               && ((text[start - 1] >= '0' && text[start - 1] <= '9')
                   || (text[start - 1] >= 'a' && text[start - 1] <= 'f')
                   || (text[start - 1] >= 'A' && text[start - 1] <= 'F'))) {
            start -= 1;
        }
        if (start > prefix + 2 && text[start - 1] == 'x'
            && text[start - 2] == '\\') {
            int32 slashes = 0;
            for (int32 i = start - 2; i > prefix && text[i] == '\\'; i -= 1) {
                slashes += 1;
            }
            if (slashes % 2 == 1) {
                return false;
            }
        }
    }
    return true;
}

bool
csane_string_literal_token(Token *token) {
    return csane_string_prefix(token) >= 0;
}

static int32
csane_string_columns(char *text, int32 first, int32 end) {
    int32 columns = 0;

    for (int32 i = first; i < end; i += 1) {
        if (((uint8)text[i] & 0xc0) != 0x80) {
            columns += 1;
        }
    }
    return columns;
}

static bool
csane_string_code(CsaneFile *file, int32 token) {
    return file->preprocessor->token_roles == NULL
           || file->preprocessor->token_roles[token] == C_PP_TOKEN_CODE;
}

/* Report only when the *whole* adjacent group fits on the first literal's
 * physical line, including preceding code and the last literal's suffix.
 * This also handles strings in SL("..." "...") without special casing SL. */
void
csane_string_literals_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    int32 count = ARRAY_LEN(tokens->tokens);
    char *source = tokens->text;

    if (file->column_limit <= 0) {
        return;
    }
    for (int32 i = 0; i < count;) {
        Token *first = &tokens->tokens[i];
        int32 prefix = csane_string_prefix(first);
        int32 last = i;
        int32 next;
        int32 columns;
        int32 line_start;
        int32 line_end;
        bool multiline = false;

        if (prefix < 0 || !csane_string_code(file, i)) {
            i += 1;
            continue;
        }
        next = csane_tokens_next(&view, last);
        while (next < count) {
            Token *left = &tokens->tokens[last];
            Token *right = &tokens->tokens[next];
            int32 other_prefix = csane_string_prefix(right);

            if (other_prefix != prefix || !csane_string_code(file, next)
                || memcmp64(first->text, right->text, prefix) != 0
                || !csane_string_whitespace(source,
                                            left->offset + left->len,
                                            right->offset)
                || !csane_string_safe_boundary(left, right, prefix)) {
                break;
            }
            for (int32 j = left->offset + left->len;
                 j < right->offset; j += 1) {
                if (source[j] == '\n') {
                    multiline = true;
                    break;
                }
            }
            last = next;
            next = csane_tokens_next(&view, last);
        }
        if (last == i || !multiline) {
            i = last + 1;
            continue;
        }
        line_start = first->offset;
        while (line_start > 0 && source[line_start - 1] != '\n') {
            line_start -= 1;
        }
        line_end = tokens->tokens[last].offset + tokens->tokens[last].len;
        while (line_end < tokens->text_len && source[line_end] != '\n') {
            line_end += 1;
        }
        columns = csane_string_columns(source, line_start, first->offset)
                  + csane_string_columns(source,
                                         tokens->tokens[last].offset
                                             + tokens->tokens[last].len,
                                         line_end);
        for (int32 j = i; j <= last;) {
            Token *part = &tokens->tokens[j];

            columns += csane_string_columns(part->text, 0, part->len);
            if (j != i) {
                columns -= prefix + 2;
            }
            j = csane_tokens_next(&view, j);
        }
        if (columns <= file->column_limit) {
            csane_rule_issue(file, first->offset, SL("FMT-STRING-SPLIT"),
                             "adjacent string literals can fit on one line");
        }
        i = last + 1;
    }
    return;
}

#endif /* CSANECHECK_STRING_LITERALS_C */
