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

/* A fragment ending in a newline escape intentionally starts a new source
 * line for the following fragment. A doubled backslash is not an escape. */
static bool
csane_string_ends_with_newline(Token *token) {
    int32 last = token->len - 2;
    int32 slashes = 0;

    if (last < 1 || token->text[last] != 'n') {
        return false;
    }
    for (int32 i = last - 1; i >= 0 && token->text[i] == '\\'; i -= 1) {
        slashes += 1;
    }
    return slashes % 2 == 1;
}

static bool
csane_string_code(CsaneFile *file, int32 token) {
    return file->preprocessor->token_roles == NULL
           || file->preprocessor->token_roles[token] == C_PP_TOKEN_CODE;
}

/* Color formatters expand into string literals. */
static int32
csane_string_format_close(CsaneFile *file, CsaneTokenView *view, int32 name) {
    Tokenization *tokens = file->tokens;
    int32 open;
    int32 close;
    Token *macro = &tokens->tokens[name];

    if (!csane_string_code(file, name)
        || !(TOKEN_IS(macro, "RED") || TOKEN_IS(macro, "GREEN")
             || TOKEN_IS(macro, "YELLOW") || TOKEN_IS(macro, "BLUE")
             || TOKEN_IS(macro, "CYAN") || TOKEN_IS(macro, "PURPLE")
             || TOKEN_IS(macro, "BRED") || TOKEN_IS(macro, "BGREEN")
             || TOKEN_IS(macro, "BYELLOW") || TOKEN_IS(macro, "BBLUE")
             || TOKEN_IS(macro, "BCYAN"))) {
        return -1;
    }
    open = csane_tokens_next(view, name);
    if (open >= ARRAY_LEN(tokens->tokens)
        || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return -1;
    }
    close = csane_tokens_match(view, open);
    if (close < 0 || !csane_string_code(file, close)) {
        return -1;
    }
    return close;
}

/* Compute the width of the complete format expression, not only the two
 * neighboring literals. A continuation after a color macro may make the
 * expression too long to combine even when the first pair alone fits. */
static int32
csane_string_format_columns(CsaneFile *file, CsaneTokenView *view,
                            int32 first, int32 prefix, bool *has_macro) {
    Tokenization *tokens = file->tokens;
    char *source = tokens->text;
    int32 count = ARRAY_LEN(tokens->tokens);
    int32 start = first;
    int32 previous = -1;
    int32 end = -1;
    int32 width = 0;

    while (start > 0) {
        Token *right = &tokens->tokens[start];
        Token *left;
        int32 before = csane_tokens_previous(view, start);

        if (before < 0 || !csane_string_code(file, before)) {
            break;
        }
        left = &tokens->tokens[before];
        if (!csane_string_whitespace(source, left->offset + left->len,
                                     right->offset)) {
            break;
        }
        if (csane_string_prefix(left) == prefix) {
            if (csane_string_ends_with_newline(left)
                || (csane_string_prefix(right) == prefix
                    && !csane_string_safe_boundary(left, right, prefix))) {
                break;
            }
            start = before;
            continue;
        }
        if (TOKEN_IS(left, ")")) {
            int32 open = csane_tokens_match(view, before);
            int32 name;

            if (open < 0) {
                break;
            }
            name = csane_tokens_previous(view, open);
            if (name < 0
                || csane_string_format_close(file, view, name) != before) {
                break;
            }
            start = name;
            continue;
        }
        break;
    }
    for (int32 i = start; i < count;) {
        Token *current = &tokens->tokens[i];
        int32 close = csane_string_format_close(file, view, i);
        int32 literal_prefix = csane_string_prefix(current);

        if (literal_prefix != prefix && close < 0) {
            break;
        }
        if (previous >= 0) {
            Token *left = &tokens->tokens[previous];
            if (!csane_string_whitespace(source,
                                         left->offset + left->len,
                                         current->offset)) {
                break;
            }
            if (csane_string_prefix(left) == prefix) {
                if (csane_string_ends_with_newline(left)) {
                    break;
                }
                if (literal_prefix == prefix) {
                    if (!csane_string_safe_boundary(left, current, prefix)) {
                        break;
                    }
                    width -= prefix + 2;
                }
            }
        }
        if (close >= 0) {
            Token *closing = &tokens->tokens[close];
            int32 after = closing->offset + closing->len;

            *has_macro = true;
            for (int32 j = current->offset; j < after; j += 1) {
                if (source[j] == '\n' || source[j] == '\r') {
                    return INT32_MAX;
                }
            }
            width += csane_string_columns(source, current->offset, after);
            previous = close;
        } else {
            width += csane_string_columns(current->text, 0, current->len);
            previous = i;
        }
        end = previous;
        i = csane_tokens_next(view, previous);
    }
    if (end < 0) {
        return INT32_MAX;
    }
    {
        Token *head = &tokens->tokens[start];
        Token *tail = &tokens->tokens[end];
        int32 line_start = head->offset;
        int32 line_end = tail->offset + tail->len;

        while (line_start > 0 && source[line_start - 1] != '\n') {
            line_start -= 1;
        }
        while (line_end < tokens->text_len && source[line_end] != '\n') {
            line_end += 1;
        }
        width += csane_string_columns(source, line_start, head->offset);
        width += csane_string_columns(source, tail->offset + tail->len,
                                      line_end);
    }
    return width;
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
                || csane_string_ends_with_newline(left)
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
            bool has_macro = false;
            int32 format_columns =
                csane_string_format_columns(file, &view, i, prefix,
                                            &has_macro);

            if (!has_macro || format_columns <= file->column_limit) {
                csane_rule_issue(file, first->offset,
                                 SL("FMT-STRING-SPLIT"),
                                 "adjacent string literals fit on one line");
            }
        }
        i = last + 1;
    }
    return;
}

#endif /* CSANECHECK_STRING_LITERALS_C */
