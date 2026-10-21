// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_LAYOUT_C)
#define CSANECHECK_LAYOUT_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

/* Operator layout uses only borrowed source tokens. A bounded source region
 * keeps adjacent tokens in mutually exclusive #if branches from becoming
 * fake operands. The same classifier is used for rule coverage. */
static bool
csane_layout_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static bool
csane_layout_code(CsaneFile *file, int32 index) {
    return index >= 0 && index < file->tokens->token_count
           && (file->preprocessor->token_roles == NULL
               || file->preprocessor->token_roles[index]
                  == C_PP_TOKEN_CODE);
}

static bool
csane_layout_is(Tokenization *tokens, int32 i, char *name) {
    return i >= 0 && i < tokens->token_count
           && TOKEN_IS(&tokens->tokens[i], name);
}

static bool
csane_layout_keyword(Token *token) {
    static char *names[] = {
        "if", "else", "for", "while", "do", "switch", "return",
        "case", "default", "goto", "break", "continue", "typedef",
        "struct", "union", "enum", "const", "volatile", "static",
        "extern", "register", "restrict", "inline", "sizeof",
        "_Alignof", "_Atomic", "_Generic", "_Static_assert",
    };
    for (int32 i = 0; i < SIZEOF(names)/SIZEOF(names[0]); i += 1) {
        if (TOKEN_IS(token, names[i])) {
            return true;
        }
    }
    return false;
}

static bool
csane_layout_operand_end(Token *token) {
    if (token->kind == TOKEN_IDENT) {
        return !csane_layout_keyword(token);
    }
    return token->kind == TOKEN_LITERAL || TOKEN_IS(token, ")")
           || TOKEN_IS(token, "]") || TOKEN_IS(token, "}")
           || TOKEN_IS(token, "++") || TOKEN_IS(token, "--");
}

static bool
csane_layout_operand_start(Token *token) {
    if (token->kind == TOKEN_IDENT) {
        return !csane_layout_keyword(token) || TOKEN_IS(token, "sizeof")
               || TOKEN_IS(token, "_Alignof") || TOKEN_IS(token, "_Generic");
    }
    return token->kind == TOKEN_LITERAL || TOKEN_IS(token, "(")
           || TOKEN_IS(token, "{") || TOKEN_IS(token, "*")
           || TOKEN_IS(token, "&") || TOKEN_IS(token, "+")
           || TOKEN_IS(token, "-") || TOKEN_IS(token, "!")
           || TOKEN_IS(token, "~") || TOKEN_IS(token, "++")
           || TOKEN_IS(token, "--");
}

static bool
csane_layout_assignment(Token *op) {
    static char *names[] = {
        "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "<<=", ">>=",
    };
    for (int32 i = 0; i < SIZEOF(names)/SIZEOF(names[0]); i += 1) {
        if (TOKEN_IS(op, names[i])) {
            return true;
        }
    }
    return false;
}

static bool
csane_layout_binary(Token *op) {
    return TOKEN_IS(op, "+") || TOKEN_IS(op, "-")
           || TOKEN_IS(op, "&") || TOKEN_IS(op, "<<")
           || TOKEN_IS(op, ">>") || TOKEN_IS(op, "&&")
           || TOKEN_IS(op, "||") || TOKEN_IS(op, "==")
           || TOKEN_IS(op, "!=") || TOKEN_IS(op, "<")
           || TOKEN_IS(op, "<=") || TOKEN_IS(op, ">")
           || TOKEN_IS(op, ">=");
}

static bool
csane_layout_qualifier(Token *token) {
    return TOKEN_IS(token, "const") || TOKEN_IS(token, "volatile")
           || TOKEN_IS(token, "restrict") || TOKEN_IS(token, "typedef")
           || TOKEN_IS(token, "struct") || TOKEN_IS(token, "union")
           || TOKEN_IS(token, "enum") || TOKEN_IS(token, "static")
           || TOKEN_IS(token, "extern") || TOKEN_IS(token, "register")
           || TOKEN_IS(token, "inline") || TOKEN_IS(token, "_Atomic");
}

/* A token immediately after a closing cast parenthesis begins a unary
 * expression. Do not mistake (Type)-x or (int32*)*p for binary operators. */
static bool
csane_layout_cast(CsaneTokenView *view, int32 close) {
    Tokenization *tokens = view->tokens;
    if (!csane_layout_is(tokens, close, ")")) {
        return false;
    }
    int32 open = csane_tokens_match(view, close);
    if (open < 0) {
        return false;
    }
    int32 first = csane_tokens_next(view, open);
    if (first >= close) {
        return false;
    }
    Token *type = &tokens->tokens[first];
    if (!csane_type_name(type) && !csane_layout_qualifier(type)) {
        return false;
    }
    for (int32 i = first; i >= 0 && i < close;) {
        Token *part = &tokens->tokens[i];
        if (part->kind != TOKEN_IDENT && !TOKEN_IS(part, "*")) {
            return false;
        }
        i = csane_tokens_next(view, i);
    }
    return true;
}

/* A pointer declarator needs a type prefix, not a translation-unit typedef
 * table. Built-in and CamelCase types, qualifiers, and struct tags can be
 * recognized from the small neighboring token window. */
static bool
csane_layout_pointer(CsaneTokenView *view, int32 before, int32 after) {
    Tokenization *tokens = view->tokens;
    Token *left = &tokens->tokens[before];
    Token *right = &tokens->tokens[after];
    if (right->kind != TOKEN_IDENT && !TOKEN_IS(right, "*")
        && !TOKEN_IS(right, "(") && !TOKEN_IS(right, ")")) {
        return false;
    }
    if (csane_type_name(left) || csane_layout_qualifier(left)) {
        return true;
    }
    if (left->kind != TOKEN_IDENT) {
        return false;
    }
    int32 preceding = csane_tokens_previous(view, before);
    /* The conventional lowercase typedef suffix is only meaningful at a
     * declaration boundary. In 'return value_t *p' it is multiplication. */
    if (left->len > 2 && left->text[left->len - 2] == '_'
        && left->text[left->len - 1] == 't'
        && (preceding < view->range.first
            || csane_layout_is(tokens, preceding, ";")
            || csane_layout_is(tokens, preceding, "{")
            || csane_layout_is(tokens, preceding, "}"))) {
        return true;
    }
    if (preceding >= view->range.first) {
        Token *prefix = &tokens->tokens[preceding];
        if (csane_layout_qualifier(prefix) || csane_type_name(prefix)) {
            return true;
        }
    }
    return false;
}

static bool
csane_layout_control_close(CsaneTokenView *view, int32 before) {
    Tokenization *tokens = view->tokens;
    if (!csane_layout_is(tokens, before, ")")) {
        return false;
    }
    int32 open = csane_tokens_match(view, before);
    if (open < 0) {
        return false;
    }
    int32 keyword = csane_tokens_previous(view, open);
    return csane_layout_is(tokens, keyword, "if")
           || csane_layout_is(tokens, keyword, "while")
           || csane_layout_is(tokens, keyword, "for")
           || csane_layout_is(tokens, keyword, "switch");
}

/* Only examine neighbors inside the same original code region. When a local
 * expression is incomplete, return UNRESOLVED instead of certifying it. */
enum CsaneLayoutOperator
csane_layout_classify(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    if (!csane_layout_code(file, index)) {
        return CSANE_LAYOUT_SKIP;
    }
    Token *op = &tokens->tokens[index];
    bool multiply = TOKEN_IS(op, "*");
    bool binary = csane_layout_binary(op);
    bool assign = csane_layout_assignment(op);
    if (!multiply && !binary && !assign) {
        return CSANE_LAYOUT_SKIP;
    }
    int32 region = csane_regions_at_token(file->regions, index);
    CsaneTokenView view;
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    int32 before = csane_tokens_previous(&view, index);
    int32 after = csane_tokens_next(&view, index);
    if (before < view.range.first || after >= view.range.end) {
        if (!assign && before < view.range.first) {
            return CSANE_LAYOUT_SKIP;
        }
        return CSANE_LAYOUT_UNRESOLVED;
    }
    Token *left = &tokens->tokens[before];
    Token *right = &tokens->tokens[after];
    if (!assign && (multiply || TOKEN_IS(op, "+")
                    || TOKEN_IS(op, "-") || TOKEN_IS(op, "&"))) {
        if (!csane_layout_operand_end(left)
            || csane_layout_cast(&view, before)
            || csane_layout_control_close(&view, before)) {
            return CSANE_LAYOUT_SKIP;
        }
        if (multiply && csane_layout_pointer(&view, before, after)) {
            return CSANE_LAYOUT_SKIP;
        }
    }
    if (!csane_layout_operand_end(left)
        || !csane_layout_operand_start(right)) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    if (assign) {
        return CSANE_LAYOUT_ASSIGN;
    }
    return multiply ? CSANE_LAYOUT_MULTIPLY : CSANE_LAYOUT_BINARY;
}

/* Comments between an operator and operand deliberately count as a neutral
 * gap, just as they did with the AST-based implementation. */
static bool
csane_layout_gap(CsaneFile *file, int32 start, int32 end,
                 bool *horizontal, bool *separated) {
    char *text = file->tokens->text;
    bool space = false;
    bool newline = false;
    if (start < 0 || end < start || end > file->tokens->text_len) {
        return false;
    }
    for (int32 i = start; i < end; i += 1) {
        if (text[i] == ' ' || text[i] == '\t') {
            space = true;
        } else if (text[i] == '\n' || text[i] == '\r') {
            newline = true;
        } else {
            return false;
        }
    }
    *horizontal = space && !newline;
    *separated = space || newline;
    return true;
}

static bool
csane_layout_spacing(CsaneFile *file, int32 index, bool tight) {
    Tokenization *tokens = file->tokens;
    int32 region = csane_regions_at_token(file->regions, index);
    if (region < 0) {
        return false;
    }
    CsaneTokenView view;
    if (!csane_tokens_view(&view, tokens,
                           file->regions->items[region].tokens)) {
        return false;
    }
    int32 before = csane_tokens_previous(&view, index);
    int32 after = csane_tokens_next(&view, index);
    if (before < view.range.first || after >= view.range.end) {
        return false;
    }
    Token *left = &tokens->tokens[before];
    Token *op = &tokens->tokens[index];
    Token *right = &tokens->tokens[after];
    bool before_space = false;
    bool after_space = false;
    bool before_sep = false;
    bool after_sep = false;
    if (!csane_layout_gap(file, left->offset + left->len, op->offset,
                          &before_space, &before_sep)
        || !csane_layout_gap(file, op->offset + op->len, right->offset,
                             &after_space, &after_sep)) {
        return false;
    }
    return tight ? before_space || after_space
                 : !before_sep || !after_sep;
}

/* Unlike operator spacing, the placement of a continuation-line '=' must
 * also be checked in unsupported expressions, as long as it is C source and
 * not a preprocessor directive or macro replacement. */
static void
csane_layout_leading_assignment(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    char *text = tokens->text;
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        int32 start;
        if (!TOKEN_IS(token, "=") || !csane_layout_code(file, i)) {
            continue;
        }
        start = token->offset;
        while (start > 0 && text[start - 1] != '\n') {
            start -= 1;
        }
        for (int32 j = start; j < token->offset;) {
            if (text[j] == ' ' || text[j] == '\t' || text[j] == '\r') {
                j += 1;
                continue;
            }
            bool comment = false;
            for (int32 k = 0; k < tokens->token_count; k += 1) {
                Token *part = &tokens->tokens[k];
                if (part->offset > j) {
                    break;
                }
                if (part->kind == TOKEN_COMMENT && part->offset <= j
                    && part->offset + part->len > j
                    && part->offset + part->len <= token->offset) {
                    j = part->offset + part->len;
                    comment = true;
                    break;
                }
            }
            if (!comment) {
                start = -1;
                break;
            }
        }
        if (start >= 0 && start < token->offset + 1) {
            csane_rule_issue(file, token->offset, "FMT-ASSIGN-EQ",
                             "assignment = must end the preceding line");
        }
    }
    return;
}

void
csane_layout_check(CsaneFile *file) {
    if (file->is_ryu || csane_layout_generated(file->path)) {
        return;
    }
    for (int32 i = 0; i < file->tokens->token_count; i += 1) {
        enum CsaneLayoutOperator kind = csane_layout_classify(file, i);
        Token *op = &file->tokens->tokens[i];
        if (kind == CSANE_LAYOUT_MULTIPLY) {
            if (csane_layout_spacing(file, i, true)) {
                csane_rule_issue(file, op->offset, "FMT-MULTIPLY",
                                 "no spaces around multiplication");
            }
        } else if (kind == CSANE_LAYOUT_BINARY) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, op->offset, "FMT-BINARY-SPACING",
                                 "spaces required around binary operator");
            }
        } else if (kind == CSANE_LAYOUT_ASSIGN) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, op->offset, "FMT-ASSIGN-SPACING",
                                 "assignment requires surrounding whitespace");
            }
        }
    }
}

void
csane_layout_assignment_check(CsaneFile *file) {
    if (!file->is_ryu && !csane_layout_generated(file->path)) {
        csane_layout_leading_assignment(file);
    }
}

#endif /* CSANECHECK_LAYOUT_C */
