// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_LAYOUT_C)
#define CSANECHECK_LAYOUT_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"
#include "csck_statements.h"

/* Each token points to its sole declaration-relevant statement or top-level
 * header. Overlapping ranges retain the original ordered scan, since separate
 * preprocessor streams can describe the same source token differently. */
static void
csane_layout_index_range(int32 *index, int32 count, TokenRange range,
                         int32 item) {
    if (range.first < 0) {
        range.first = 0;
    }
    if (range.end > count) {
        range.end = count;
    }
    for (int32 i = range.first; i < range.end; i += 1) {
        if (index[i] == -1) {
            index[i] = item;
        } else {
            index[i] = -2;
        }
    }
    return;
}

void
csane_layout_index_init(CsaneFile *file) {
    int32 count = ARRAY_LEN(file->tokens->tokens);

    ARRAY_INIT_COUNT(file->layout_statement_index, count);
    ARRAY_INIT_COUNT(file->layout_top_index, count);
    for (int32 i = 0; i < count; i += 1) {
        file->layout_statement_index[i] = -1;
        file->layout_top_index[i] = -1;
    }
    if (file->statements != NULL) {
        for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
            CsaneStatement *stmt = &file->statements->items[i];
            TokenRange range = stmt->tokens;

            if (stmt->kind == CSANE_STMT_FOR) {
                if (range.end > stmt->header.end) {
                    range.end = stmt->header.end;
                }
            } else if (stmt->kind != CSANE_STMT_SIMPLE) {
                continue;
            }
            csane_layout_index_range(file->layout_statement_index, count,
                                     range, i);
        }
    }
    if (file->toplevel != NULL) {
        for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
            CsaneTopItem *item = &file->toplevel->items[i];
            TokenRange range = item->tokens;

            if (item->kind != CSANE_TOP_DECLARATION
                && item->kind != CSANE_TOP_TYPE_DECLARATION
                && item->kind != CSANE_TOP_FUNCTION_DEFINITION
                && item->kind != CSANE_TOP_FUNCTION_DECLARATION) {
                continue;
            }
            if (range.end > item->header.end) {
                range.end = item->header.end;
            }
            csane_layout_index_range(file->layout_top_index, count, range, i);
        }
    }
    return;
}

void
csane_layout_index_free(CsaneFile *file) {
    ARRAY_FREE(file->layout_statement_index);
    ARRAY_FREE(file->layout_top_index);
    return;
}

/* Operator layout uses only borrowed source tokens. A bounded source region
 * keeps adjacent tokens in mutually exclusive #if branches from becoming
 * fake operands. The same classifier is used for rule coverage. */
static bool
csane_layout_generated(char *path, int32 path_len) {
    return ENDS_WITH(path, path_len, ".meta.h");
}

static bool
csane_layout_code(CsaneFile *file, int32 index) {
    return index >= 0 && index < ARRAY_LEN(file->tokens->tokens)
           && (file->preprocessor->token_roles == NULL
               || file->preprocessor->token_roles[index]
                  == C_PP_TOKEN_CODE);
}

static bool
csane_layout_is(Tokenization *tokens, int32 i, char *name, int32 name_len) {
    return i >= 0 && i < ARRAY_LEN(tokens->tokens)
           && TOKEN_IS(&tokens->tokens[i], name, name_len);
}

static StrFlex *csane_layout_keywords[] = {
    SFLIT("if"), SFLIT("else"), SFLIT("for"), SFLIT("while"),
    SFLIT("do"), SFLIT("switch"), SFLIT("return"), SFLIT("case"),
    SFLIT("default"), SFLIT("goto"), SFLIT("break"),
    SFLIT("continue"), SFLIT("typedef"), SFLIT("struct"),
    SFLIT("union"), SFLIT("enum"), SFLIT("const"),
    SFLIT("volatile"), SFLIT("static"), SFLIT("extern"),
    SFLIT("register"), SFLIT("restrict"), SFLIT("inline"),
    SFLIT("sizeof"), SFLIT("_Alignof"), SFLIT("_Atomic"),
    SFLIT("_Generic"), SFLIT("_Static_assert"),
};

static bool
csane_layout_keyword(Token *token) {
    for (int32 i = 0; i < LENGTH(csane_layout_keywords); i += 1) {
        StrFlex *word = csane_layout_keywords[i];
        if (TOKEN_IS(token, word->data, word->len)) {
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

static StrFlex *csane_layout_assignments[] = {
    SFLIT("="), SFLIT("+="), SFLIT("-="), SFLIT("*="),
    SFLIT("/="), SFLIT("%="), SFLIT("&="), SFLIT("|="),
    SFLIT("^="), SFLIT("<<="), SFLIT(">>="),
};

static bool
csane_layout_assignment(Token *op) {
    int32 count = LENGTH(csane_layout_assignments);
    for (int32 i = 0; i < count; i += 1) {
        StrFlex *word = csane_layout_assignments[i];
        if (TOKEN_IS(op, word->data, word->len)) {
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
           || TOKEN_IS(token, "inline") || TOKEN_IS(token, "_Atomic")
           || TOKEN_IS(token, "_Noreturn") || TOKEN_IS(token, "noreturn");
}

/* A token immediately after a closing cast parenthesis begins a unary
 * expression. Do not mistake (Type)-x or (int32*)*p for binary operators. */
static bool
csane_layout_cast(CsaneFile *file, CsaneTokenView *view, int32 close) {
    Tokenization *tokens = view->tokens;
    int32 open;
    int32 previous;
    int32 first;
    bool base = false;
    bool named = false;
    bool tag = false;
    bool unknown = false;
    bool pointer = false;
    if (!csane_layout_is(tokens, close, STRLIT(")"))) {
        return false;
    }
    open = csane_tokens_match(view, close);
    if (open < 0) {
        return false;
    }
    previous = csane_tokens_previous(view, open);
    if (previous >= view->range.first
        && (TOKEN_IS(&tokens->tokens[previous], "if")
            || TOKEN_IS(&tokens->tokens[previous], "while")
            || TOKEN_IS(&tokens->tokens[previous], "for")
            || TOKEN_IS(&tokens->tokens[previous], "switch")
            || csane_layout_operand_end(&tokens->tokens[previous])
            || TOKEN_IS(&tokens->tokens[previous], "sizeof")
            || TOKEN_IS(&tokens->tokens[previous], "_Alignof")
            || TOKEN_IS(&tokens->tokens[previous], "_Atomic")
            || TOKEN_IS(&tokens->tokens[previous], "_Generic"))) {
        return false;
    }
    first = csane_tokens_next(view, open);
    if (first >= close) {
        return false;
    }
    for (int32 i = first; i >= 0 && i < close;) {
        Token *part = &tokens->tokens[i];
        bool primitive = csane_name_kind(part) == CSANE_NAME_PRIMITIVE;
        bool type_name = csane_type_name(part)
                         || csane_type_hints_typedef(file->type_hints, part);
        if (TOKEN_IS(part, "*")) {
            if (!base) {
                return false;
            }
            pointer = true;
        } else if (tag) {
            if (part->kind != TOKEN_IDENT) {
                return false;
            }
            tag = false;
            base = true;
            named = true;
        } else if (TOKEN_IS(part, "struct") || TOKEN_IS(part, "union")
                   || TOKEN_IS(part, "enum")) {
            if (base) {
                return false;
            }
            tag = true;
        } else if (csane_layout_qualifier(part)) {
            /* Qualifiers do not identify the base type by themselves. */
        } else if (primitive) {
            if (named) {
                return false;
            }
            base = true;
        } else if (type_name) {
            if (base) {
                return false;
            }
            base = true;
            named = true;
        } else if (!base && part->kind == TOKEN_IDENT) {
            base = true;
            named = true;
            unknown = true;
        } else {
            return false;
        }
        i = csane_tokens_next(view, i);
    }
    return base && !tag && (!unknown || pointer);
}

static bool
csane_layout_in_cast(CsaneFile *file, CsaneTokenView *view, int32 index) {
    Tokenization *tokens = view->tokens;
    for (int32 i = csane_tokens_previous(view, index);
         i >= view->range.first; i = csane_tokens_previous(view, i)) {
        if (TOKEN_IS(&tokens->tokens[i], ")")
            || TOKEN_IS(&tokens->tokens[i], "]")
            || TOKEN_IS(&tokens->tokens[i], "}")) {
            return false;
        }
        if (TOKEN_IS(&tokens->tokens[i], "(")) {
            int32 close = csane_tokens_match(view, i);
            return close > index && csane_layout_cast(file, view, close);
        }
        if (TOKEN_IS(&tokens->tokens[i], ";")
            || TOKEN_IS(&tokens->tokens[i], "{")
            || TOKEN_IS(&tokens->tokens[i], ",")) {
            return false;
        }
    }
    return false;
}

/* Recognize the prefix of a declaration without guessing typedef names.
 * The enclosing statement or function header supplies the boundary. */
static bool
csane_layout_type_prefix(CsaneTokenView *view, int32 first, int32 type_end) {
    Tokenization *tokens = view->tokens;
    for (int32 i = first; i >= 0 && i < type_end;) {
        Token *part = &tokens->tokens[i];
        if (!csane_layout_qualifier(part)
            && !TOKEN_IS(part, "signed")
            && !TOKEN_IS(part, "unsigned")
            && !TOKEN_IS(part, "long")
            && !TOKEN_IS(part, "short")
            && csane_name_kind(part) != CSANE_NAME_MACRO) {
            return false;
        }
        i = csane_tokens_next(view, i);
        if (i != type_end && (i < 0 || i >= view->range.end)) {
            return false;
        }
    }
    return type_end >= view->range.first
           && type_end < view->range.end
           && tokens->tokens[type_end].kind == TOKEN_IDENT;
}

static bool
csane_layout_statement_prefix(CsaneFile *file, CsaneTokenView *view,
                              int32 index, int32 before) {
    Tokenization *tokens = view->tokens;
    CsaneStatements *statements = file->statements;
    int32 statement_first = 0;
    int32 statement_end = 0;
    int32 top_first = 0;
    int32 top_end = 0;

    if (statements) {
        statement_end = ARRAY_LEN(statements->items);
        if (file->layout_statement_index != NULL
            && index >= 0
            && index < ARRAY_LEN(file->layout_statement_index)) {
            int32 candidate = file->layout_statement_index[index];
            if (candidate == -1) {
                statement_end = 0;
            } else if (candidate >= 0) {
                statement_first = candidate;
                statement_end = candidate + 1;
            }
        }
        for (int32 i = statement_first; i < statement_end; i += 1) {
            CsaneStatement *stmt = &statements->items[i];
            if (index < stmt->tokens.first
                || index >= stmt->tokens.end) {
                continue;
            }
            if (stmt->kind == CSANE_STMT_SIMPLE) {
                int32 first = csane_tokens_next(view, stmt->tokens.first - 1);
                if (first <= before
                    && csane_layout_type_prefix(view, first, before)) {
                    return true;
                }
            } else if (stmt->kind == CSANE_STMT_FOR
                       && index < stmt->header.end) {
                int32 keyword = csane_tokens_next(view, stmt->header.first - 1);
                int32 open = csane_tokens_next(view, keyword);
                int32 close = csane_tokens_match(view, open);
                TokenRange initializer;
                CsaneTokenView init_view;
                int32 semicolon;
                int32 first;

                if (!csane_layout_is(tokens, keyword, STRLIT("for"))
                    || !csane_layout_is(tokens, open, STRLIT("("))
                    || close < 0) {
                    continue;
                }
                initializer = (TokenRange){open + 1, close};
                if (!csane_tokens_subview(&init_view, view, initializer)) {
                    continue;
                }
                semicolon = csane_tokens_top_level(&init_view,
                                                   initializer.first - 1,
                                                   STRLIT(";"));
                first = csane_tokens_next(&init_view,
                                          initializer.first - 1);
                if (index < semicolon && first <= before
                    && csane_layout_type_prefix(&init_view, first, before)) {
                    return true;
                }
            }
        }
    }
    if (file->toplevel != NULL) {
        top_end = ARRAY_LEN(file->toplevel->items);
        if (file->layout_top_index != NULL
            && index >= 0 && index < ARRAY_LEN(file->layout_top_index)) {
            int32 candidate = file->layout_top_index[index];
            if (candidate == -1) {
                top_end = 0;
            } else if (candidate >= 0) {
                top_first = candidate;
                top_end = candidate + 1;
            }
        }
        for (int32 i = top_first; i < top_end; i += 1) {
            CsaneTopItem *item = &file->toplevel->items[i];
            if (index < item->tokens.first || index >= item->tokens.end) {
                continue;
            }
            if ((item->kind == CSANE_TOP_DECLARATION
                 || item->kind == CSANE_TOP_TYPE_DECLARATION
                 || item->kind == CSANE_TOP_FUNCTION_DEFINITION
                 || item->kind == CSANE_TOP_FUNCTION_DECLARATION)
                && index < item->header.end) {
                int32 first = csane_tokens_next(view, item->header.first - 1);
                if (first <= before
                    && csane_layout_type_prefix(view, first, before)) {
                    return true;
                }
            }
            /* A function parameter starts after '(' or ',' in its header.
             * Calls inside function bodies are deliberately excluded. */
            if ((item->kind == CSANE_TOP_FUNCTION_DEFINITION
                 || item->kind == CSANE_TOP_FUNCTION_DECLARATION)
                && index < item->header.end) {
                int32 first = before;
                int32 previous = csane_tokens_previous(view, first);
                while (previous >= view->range.first
                       && (csane_layout_qualifier(&tokens->tokens[previous])
                           || TOKEN_IS(&tokens->tokens[previous], "signed")
                           || TOKEN_IS(&tokens->tokens[previous], "unsigned")
                           || TOKEN_IS(&tokens->tokens[previous], "long")
                           || TOKEN_IS(&tokens->tokens[previous], "short"))) {
                    first = previous;
                    previous = csane_tokens_previous(view, first);
                }
                if (csane_layout_type_prefix(view, first, before)
                    && (previous < view->range.first
                        || TOKEN_IS(&tokens->tokens[previous], "(")
                        || TOKEN_IS(&tokens->tokens[previous], ","))) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* A locally recognizable declaration can start after a statement
 * separator. This also covers incomplete/opaque statements not emitted by
 * the lightweight statement scanner, without resolving typedef names. */
static bool
csane_layout_local_prefix(CsaneTokenView *view, int32 before) {
    Tokenization *tokens = view->tokens;
    int32 first = before;
    int32 previous;

    for (int32 i = 0; i < 8; i += 1) {
        Token *part;

        previous = csane_tokens_previous(view, first);
        if (previous < view->range.first) {
            break;
        }
        part = &tokens->tokens[previous];
        if (!csane_layout_qualifier(part) && !TOKEN_IS(part, "signed")
            && !TOKEN_IS(part, "unsigned") && !TOKEN_IS(part, "long")
            && !TOKEN_IS(part, "short")) {
            break;
        }
        first = previous;
    }
    if (!csane_layout_type_prefix(view, first, before)) {
        return false;
    }
    previous = csane_tokens_previous(view, first);
    return previous < view->range.first
           || TOKEN_IS(&tokens->tokens[previous], ";")
           || TOKEN_IS(&tokens->tokens[previous], "{")
           || TOKEN_IS(&tokens->tokens[previous], "}")
           || TOKEN_IS(&tokens->tokens[previous], ":");
}

static bool
csane_layout_tag_prefix(CsaneTokenView *view, int32 type) {
    Tokenization *tokens = view->tokens;
    int32 previous = csane_tokens_previous(view, type);
    return previous >= view->range.first
           && (TOKEN_IS(&tokens->tokens[previous], "struct")
               || TOKEN_IS(&tokens->tokens[previous], "union")
               || TOKEN_IS(&tokens->tokens[previous], "enum"));
}

static bool
csane_layout_function_parameters(CsaneFile *file, CsaneTokenView *view,
                                 int32 index, int32 *open, int32 *close) {
    Tokenization *tokens = view->tokens;
    if (file->toplevel == NULL) {
        return false;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        int32 parameter_open;
        int32 parameter_close;
        if ((item->kind == CSANE_TOP_TYPE_DECLARATION
             || item->kind == CSANE_TOP_DECLARATION
             || item->kind == CSANE_TOP_MEMBER_DECLARATION)
            && index >= item->header.first && index < item->header.end) {
            for (parameter_open = csane_tokens_previous(view, index);
                 parameter_open >= item->header.first
                 && parameter_open >= view->range.first;
                 parameter_open = csane_tokens_previous(view,
                                                        parameter_open)) {
                int32 declarator_close;
                int32 declarator_open;
                int32 first;
                int32 type;

                if (!csane_layout_is(tokens, parameter_open, STRLIT("("))) {
                    continue;
                }
                parameter_close = csane_tokens_match(view, parameter_open);
                if (parameter_close <= index
                    || parameter_close >= item->header.end) {
                    continue;
                }
                declarator_close = csane_tokens_previous(view, parameter_open);
                if (declarator_close >= item->header.first
                    && tokens->tokens[declarator_close].kind == TOKEN_IDENT) {
                    type = csane_tokens_previous(view, declarator_close);
                    if (csane_layout_is(tokens, type, STRLIT("*"))) {
                        type = csane_tokens_previous(view, type);
                    }
                    if (type >= item->header.first
                        && (csane_type_name(&tokens->tokens[type])
                            || csane_type_hints_typedef(file->type_hints,
                                                        &tokens->tokens[type])
                            || csane_layout_tag_prefix(view, type))) {
                        *open = parameter_open;
                        *close = parameter_close;
                        return true;
                    }
                }
                if (!csane_layout_is(tokens, declarator_close, STRLIT(")"))) {
                    continue;
                }
                declarator_open = csane_tokens_match(view, declarator_close);
                first = csane_tokens_next(view, declarator_open);
                if (declarator_open >= item->header.first
                    && first < declarator_close
                    && csane_layout_is(tokens, first, STRLIT("*"))) {
                    *open = parameter_open;
                    *close = parameter_close;
                    return true;
                }
            }
            continue;
        }
        if (item->kind != CSANE_TOP_FUNCTION_DEFINITION
            && item->kind != CSANE_TOP_FUNCTION_DECLARATION) {
            continue;
        }
        if (item->name_token < 0 || index < item->header.first
            || index >= item->header.end) {
            continue;
        }
        parameter_open = csane_tokens_next(view, item->name_token);
        if (!csane_layout_is(tokens, parameter_open, STRLIT("("))) {
            continue;
        }
        parameter_close = csane_tokens_match(view, parameter_open);
        if (parameter_close < 0 || index <= parameter_open
            || index >= parameter_close) {
            continue;
        }
        *open = parameter_open;
        *close = parameter_close;
        return true;
    }
    return false;
}

static bool
csane_layout_inside_brackets(CsaneTokenView *view, int32 first, int32 index) {
    Tokenization *tokens = view->tokens;
    for (int32 i = csane_tokens_next(view, first);
         i >= 0 && i < index; i = csane_tokens_next(view, i)) {
        int32 close;
        if (!TOKEN_IS(&tokens->tokens[i], "[")) {
            continue;
        }
        close = csane_tokens_match(view, i);
        if (close > index) {
            return true;
        }
        if (close >= 0) {
            i = close;
        }
    }
    return false;
}

/* Inside the primary function-parameter list, a top-level '*' cannot be a
 * multiplication operator: C has no parameter default expressions. Array
 * bounds are excluded because they may contain ordinary arithmetic. */
static bool
csane_layout_parameter_pointer(CsaneFile *file, CsaneTokenView *view,
                               int32 index) {
    Tokenization *tokens = view->tokens;
    int32 open = -1;
    int32 close = -1;
    int32 nested_open = -1;
    int32 depth = 0;
    if (!csane_layout_function_parameters(file, view, index, &open, &close)
        || csane_layout_inside_brackets(view, open, index)) {
        return false;
    }
    for (int32 i = csane_tokens_next(view, open);
         i >= 0 && i < index; i = csane_tokens_next(view, i)) {
        Token *part = &tokens->tokens[i];
        if (TOKEN_IS(part, "(")) {
            depth += 1;
            if (depth == 1) {
                nested_open = i;
            }
        } else if (TOKEN_IS(part, ")")) {
            if (depth > 0) {
                depth -= 1;
            }
            if (depth == 0) {
                nested_open = -1;
            }
        }
    }
    if (depth == 0) {
        return true;
    }
    /* Parenthesized pointer declarators, as in int (*compare)(...), and
     * parameters of the function type that immediately follows one. */
    if (csane_layout_is(tokens, csane_tokens_previous(view, index),
                        STRLIT("("))) {
        return true;
    }
    if (depth == 1 && nested_open >= 0) {
        int32 declarator_close = csane_tokens_previous(view, nested_open);
        int32 declarator_open;
        int32 first;
        if (!csane_layout_is(tokens, declarator_close, STRLIT(")"))) {
            return false;
        }
        declarator_open = csane_tokens_match(view, declarator_close);
        if (declarator_open < 0) {
            return false;
        }
        first = csane_tokens_next(view, declarator_open);
        return first < declarator_close
               && TOKEN_IS(&tokens->tokens[first], "*");
    }
    return false;
}

static bool
csane_layout_known_type(CsaneFile *file, CsaneTokenView *view, int32 index) {
    Token *token = &view->tokens->tokens[index];
    return csane_type_name(token)
           || csane_type_hints_typedef(file->type_hints, token)
           || csane_layout_tag_prefix(view, index);
}

/* Type arguments such as va_arg(ap, int32 *) and cast fragments such as
 * (const int32 *) are type syntax even when they occur inside expressions. */
static bool
csane_layout_type_argument_pointer(CsaneFile *file, CsaneTokenView *view,
                                   int32 before, int32 after) {
    Tokenization *tokens = view->tokens;
    int32 first = before;
    int32 previous;
    if (!csane_layout_is(tokens, after, STRLIT(")"))
        || !csane_layout_known_type(file, view, first)) {
        return false;
    }
    previous = csane_tokens_previous(view, first);
    while (previous >= view->range.first
           && csane_layout_qualifier(&tokens->tokens[previous])) {
        first = previous;
        previous = csane_tokens_previous(view, first);
    }
    return previous >= view->range.first
           && (TOKEN_IS(&tokens->tokens[previous], "(")
               || TOKEN_IS(&tokens->tokens[previous], ","));
}

/* A bare operator passed as one macro/call argument has no operands in the
 * source token stream. It is therefore not a binary operator occurrence to
 * format, regardless of what the callee eventually does with that token. */
static bool
csane_layout_operator_argument(CsaneTokenView *view, int32 before,
                               int32 after) {
    Tokenization *tokens = view->tokens;
    return (TOKEN_IS(&tokens->tokens[before], "(")
            || TOKEN_IS(&tokens->tokens[before], ","))
           && (TOKEN_IS(&tokens->tokens[after], ")")
               || TOKEN_IS(&tokens->tokens[after], ","));
}

/* At a declaration boundary, "Type *name = ..." is a declarator. The
 * otherwise identical "x *y;" may be an expression or a declaration, so
 * it must remain unresolved rather than produce a false multiplication. */
static enum CsaneLayoutOperator
csane_layout_pointer(CsaneFile *file, CsaneTokenView *view, int32 index,
                     int32 before, int32 after) {
    Tokenization *tokens = view->tokens;
    Token *name = &tokens->tokens[after];
    int32 following;
    Token *tail;

    if (csane_layout_in_cast(file, view, index)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(&tokens->tokens[before], ")")
        && csane_type_hints_macro_typedef(file->type_hints, view, before)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(name, ")")
        && csane_layout_is(tokens, csane_tokens_previous(view, before),
                           STRLIT("("))) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (csane_layout_parameter_pointer(file, view, index)
        || csane_layout_type_argument_pointer(file, view, before, after)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (csane_layout_tag_prefix(view, before)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(name, "(") && csane_layout_known_type(file, view, before)
        && (csane_layout_statement_prefix(file, view, index, before)
            || csane_layout_local_prefix(view, before))) {
        int32 pointer = csane_tokens_next(view, after);
        int32 declarator = csane_tokens_next(view, pointer);
        int32 close = csane_tokens_next(view, declarator);
        int32 parameters = csane_tokens_next(view, close);

        if (csane_layout_is(tokens, pointer, STRLIT("*"))
            && declarator >= view->range.first
            && declarator < view->range.end
            && tokens->tokens[declarator].kind == TOKEN_IDENT
            && csane_layout_is(tokens, close, STRLIT(")"))
            && csane_tokens_match(view, after) == close
            && csane_layout_is(tokens, parameters, STRLIT("("))) {
            return CSANE_LAYOUT_DECLARATOR;
        }
    }
    if (name->kind != TOKEN_IDENT && !TOKEN_IS(name, "*")) {
        return CSANE_LAYOUT_MULTIPLY;
    }
    if (csane_layout_is(tokens, csane_tokens_previous(view, before),
                        STRLIT("return"))) {
        return CSANE_LAYOUT_MULTIPLY;
    }
    if (!csane_layout_statement_prefix(file, view, index, before)
        && !csane_layout_local_prefix(view, before)) {
        return CSANE_LAYOUT_MULTIPLY;
    }
    if (TOKEN_IS(name, "*")) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    following = csane_tokens_next(view, after);
    if (following >= view->range.end) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    tail = &tokens->tokens[following];
    if (TOKEN_IS(tail, "=") || TOKEN_IS(tail, ",")
        || TOKEN_IS(tail, "[") || TOKEN_IS(tail, "(")) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(tail, ";")) {
        /* A top-level declaration cannot instead be an expression
         * statement. Local "T *p;" remains syntactically ambiguous. */
        if (csane_name_kind(&tokens->tokens[before])
            == CSANE_NAME_PRIMITIVE
            || csane_type_hints_typedef(file->type_hints,
                                        &tokens->tokens[before])
            || csane_layout_tag_prefix(view, before)) {
            return CSANE_LAYOUT_DECLARATOR;
        }
        if (file->toplevel != NULL) {
            for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
                CsaneTopItem *top = &file->toplevel->items[i];
                if ((top->kind == CSANE_TOP_DECLARATION
                     || top->kind == CSANE_TOP_TYPE_DECLARATION)
                    && index >= top->tokens.first
                    && index < top->tokens.end) {
                    return CSANE_LAYOUT_DECLARATOR;
                }
            }
        }
        return CSANE_LAYOUT_UNRESOLVED;
    }
    if (TOKEN_IS(tail, ")")) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    return CSANE_LAYOUT_MULTIPLY;
}

static bool
csane_layout_control_close(CsaneTokenView *view, int32 before) {
    Tokenization *tokens = view->tokens;
    int32 open;
    int32 keyword;

    if (!csane_layout_is(tokens, before, STRLIT(")"))) {
        return false;
    }
    open = csane_tokens_match(view, before);
    if (open < 0) {
        return false;
    }
    keyword = csane_tokens_previous(view, open);
    return csane_layout_is(tokens, keyword, STRLIT("if"))
           || csane_layout_is(tokens, keyword, STRLIT("while"))
           || csane_layout_is(tokens, keyword, STRLIT("for"))
           || csane_layout_is(tokens, keyword, STRLIT("switch"));
}

/* Only examine neighbors inside the same original code region. When a local
 * expression is incomplete, return UNRESOLVED instead of certifying it. */
enum CsaneLayoutOperator
csane_layout_classify(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *op;
    bool multiply;
    bool binary;
    bool assign;
    int32 region;
    CsaneTokenView view;
    int32 before;
    int32 after;
    Token *left;
    Token *right;

    if (!csane_layout_code(file, index)) {
        return CSANE_LAYOUT_SKIP;
    }
    op = &tokens->tokens[index];
    multiply = TOKEN_IS(op, "*");
    binary = csane_layout_binary(op);
    assign = csane_layout_assignment(op);
    if (!multiply && !binary && !assign) {
        return CSANE_LAYOUT_SKIP;
    }
    region = csane_regions_at_token(file->regions, index);
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    before = csane_tokens_previous(&view, index);
    after = csane_tokens_next(&view, index);
    if (before < view.range.first || after >= view.range.end) {
        if (!assign && before < view.range.first) {
            return CSANE_LAYOUT_SKIP;
        }
        return CSANE_LAYOUT_UNRESOLVED;
    }
    left = &tokens->tokens[before];
    right = &tokens->tokens[after];
    if (!assign && csane_layout_operator_argument(&view, before, after)) {
        return CSANE_LAYOUT_SKIP;
    }
    if (!assign && (multiply || TOKEN_IS(op, "+")
                    || TOKEN_IS(op, "-") || TOKEN_IS(op, "&"))) {
        if (!csane_layout_operand_end(left)
            || csane_layout_cast(file, &view, before)
            || csane_layout_control_close(&view, before)) {
            return CSANE_LAYOUT_SKIP;
        }
        if (multiply) {
            enum CsaneLayoutOperator pointer =
                csane_layout_pointer(file, &view, index, before, after);
            if (pointer != CSANE_LAYOUT_MULTIPLY) {
                return pointer;
            }
        }
    }
    if (!csane_layout_operand_end(left)
        || !csane_layout_operand_start(right)) {
        return CSANE_LAYOUT_UNRESOLVED;
    }
    if (assign) {
        return CSANE_LAYOUT_ASSIGN;
    }
    if (multiply) {
        return CSANE_LAYOUT_MULTIPLY;
    }
    return CSANE_LAYOUT_BINARY;
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
    CsaneTokenView view;
    int32 before;
    int32 after;
    Token *left;
    Token *op;
    Token *right;
    bool before_space = false;
    bool after_space = false;
    bool before_sep = false;
    bool after_sep = false;

    if (region < 0) {
        return false;
    }
    if (!csane_tokens_view(&view, tokens,
                           file->regions->items[region].tokens)) {
        return false;
    }
    before = csane_tokens_previous(&view, index);
    after = csane_tokens_next(&view, index);
    if (before < view.range.first || after >= view.range.end) {
        return false;
    }
    left = &tokens->tokens[before];
    op = &tokens->tokens[index];
    right = &tokens->tokens[after];
    if (!csane_layout_gap(file, left->offset + left->len, op->offset,
                          &before_space, &before_sep)
        || !csane_layout_gap(file, op->offset + op->len, right->offset,
                             &after_space, &after_sep)) {
        return false;
    }
    if (tight) {
        return before_space || after_space;
    }
    return !before_sep || !after_sep;
}

/* Unlike operator spacing, the placement of a continuation-line '=' must
 * also be checked in unsupported expressions, as long as it is C source and
 * not a preprocessor directive or macro replacement. */
static void
csane_layout_leading_assignment(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    char *text = tokens->text;
    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        Token *token = &tokens->tokens[i];
        int32 start;
        int32 part_index;
        if (!TOKEN_IS(token, "=") || !csane_layout_code(file, i)) {
            continue;
        }
        start = token->offset;
        while (start > 0 && text[start - 1] != '\n') {
            start -= 1;
        }
        part_index = tokenization_token_at_or_after_offset(tokens, start);
        for (int32 j = start; j < token->offset;) {
            if (text[j] == ' ' || text[j] == '\t' || text[j] == '\r') {
                j += 1;
                continue;
            }
            while (part_index < ARRAY_LEN(tokens->tokens)
                   && tokens->tokens[part_index].offset
                      + tokens->tokens[part_index].len <= j) {
                part_index += 1;
            }
            if (part_index < ARRAY_LEN(tokens->tokens)) {
                Token *part = &tokens->tokens[part_index];

                if (part->kind == TOKEN_COMMENT && part->offset <= j
                    && part->offset + part->len <= token->offset) {
                    j = part->offset + part->len;
                    continue;
                }
            }
            start = -1;
            break;
        }
        if (start >= 0 && start < token->offset + 1) {
            csane_rule_issue(file, token->offset, STRLIT("FMT-ASSIGN-EQ"),
                             "assignment = must end the preceding line");
        }
    }
    return;
}

static bool
csane_layout_adjacent(Token *left, Token *right) {
    return left->offset + left->len == right->offset;
}

static bool
csane_layout_horizontal_gap(CsaneFile *file, Token *left, Token *right) {
    char *text = file->tokens->text;
    bool horizontal = false;
    for (int32 i = left->offset + left->len; i < right->offset; i += 1) {
        if (text[i] == '\n' || text[i] == '\r') {
            return false;
        }
        if (text[i] == ' ' || text[i] == '\t') {
            horizontal = true;
        }
    }
    return horizontal;
}

static bool
csane_layout_closing(Token *token) {
    return TOKEN_IS(token, ")") || TOKEN_IS(token, "]") || TOKEN_IS(token, "}");
}

static void
csane_layout_comma_check(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *comma = &tokens->tokens[index];
    int32 region = csane_regions_at_token(file->regions, index);
    CsaneTokenView view;
    int32 next;
    int32 after;
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    next = csane_tokens_next(&view, index);
    if (next >= view.range.end || csane_layout_closing(&tokens->tokens[next])) {
        return;
    }
    after = comma->offset + comma->len;
    if (after >= tokens->text_len
        || (tokens->text[after] != ' ' && tokens->text[after] != '\t'
            && tokens->text[after] != '\r' && tokens->text[after] != '\n')) {
        csane_rule_issue(file, comma->offset, STRLIT("FMT-COMMA-SPACING"),
                         "whitespace required after comma");
    }
}

static void
csane_layout_member_check(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *op = &tokens->tokens[index];
    int32 region = csane_regions_at_token(file->regions, index);
    CsaneTokenView view;
    int32 before;
    int32 after;
    bool designator;
    bool bad;
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    before = csane_tokens_previous(&view, index);
    after = csane_tokens_next(&view, index);
    if (after >= view.range.end) {
        return;
    }
    designator = TOKEN_IS(op, ".")
                 && before >= view.range.first
                 && (TOKEN_IS(&tokens->tokens[before], "{")
                     || TOKEN_IS(&tokens->tokens[before], ",")
                     || TOKEN_IS(&tokens->tokens[before], "="));
    bad = csane_layout_horizontal_gap(file, op, &tokens->tokens[after]);
    if (!designator && before >= view.range.first) {
        bad = bad
              || csane_layout_horizontal_gap(file, &tokens->tokens[before],
                                             op);
    }
    if (bad) {
        csane_rule_issue(file, op->offset, STRLIT("FMT-MEMBER-SPACING"),
                         "no whitespace around member access");
    }
}

static bool
csane_layout_function_pointer(CsaneTokenView *view, int32 index) {
    Tokenization *tokens = view->tokens;
    int32 before = csane_tokens_previous(view, index);
    int32 name = csane_tokens_next(view, index);
    int32 close;
    int32 after;

    if (before < view->range.first || name >= view->range.end
        || !TOKEN_IS(&tokens->tokens[before], "(")
        || tokens->tokens[name].kind != TOKEN_IDENT) {
        return false;
    }

    close = csane_tokens_next(view, name);
    if (close >= view->range.end || !TOKEN_IS(&tokens->tokens[close], ")")) {
        return false;
    }

    after = csane_tokens_next(view, close);
    return after < view->range.end && TOKEN_IS(&tokens->tokens[after], "(");
}

static bool
csane_layout_return_pointer(CsaneFile *file, int32 index) {
    if (file->toplevel == NULL) {
        return false;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if ((item->kind == CSANE_TOP_FUNCTION_DEFINITION
             || item->kind == CSANE_TOP_FUNCTION_DECLARATION)
            && item->name_token > index
            && index >= item->header.first && index < item->header.end) {
            return true;
        }
    }
    return false;
}

static bool
csane_layout_prefix_unary(CsaneFile *file, CsaneTokenView *view, int32 index) {
    Tokenization *tokens = view->tokens;
    Token *op = &tokens->tokens[index];
    int32 before = csane_tokens_previous(view, index);
    int32 after = csane_tokens_next(view, index);
    if (after >= view->range.end) {
        return false;
    }
    if (TOKEN_IS(op, "*")
        && (csane_layout_function_pointer(view, index)
            || csane_layout_return_pointer(file, index))) {
        return false;
    }
    if (TOKEN_IS(op, "!") || TOKEN_IS(op, "~")) {
        return true;
    }
    if (TOKEN_IS(op, "++") || TOKEN_IS(op, "--")) {
        return before < view->range.first
               || !csane_layout_operand_end(&tokens->tokens[before]);
    }
    if (!TOKEN_IS(op, "+") && !TOKEN_IS(op, "-")
        && !TOKEN_IS(op, "&") && !TOKEN_IS(op, "*")) {
        return false;
    }
    if (csane_layout_classify(file, index) != CSANE_LAYOUT_SKIP) {
        return false;
    }
    if (before < view->range.first) {
        return true;
    }
    return !csane_layout_operand_end(&tokens->tokens[before])
           || csane_layout_cast(file, view, before)
           || csane_layout_control_close(view, before);
}

static void
csane_layout_unary_check(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *op = &tokens->tokens[index];
    int32 region = csane_regions_at_token(file->regions, index);
    CsaneTokenView view;
    int32 before;
    int32 after;
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    before = csane_tokens_previous(&view, index);
    after = csane_tokens_next(&view, index);
    if (after >= view.range.end) {
        return;
    }
    if (csane_layout_prefix_unary(file, &view, index)) {
        if (!csane_layout_adjacent(op, &tokens->tokens[after])) {
            csane_rule_issue(file, op->offset, STRLIT("FMT-UNARY-SPACING"),
                             "no whitespace after unary operator");
        }
        return;
    }
    if ((TOKEN_IS(op, "++") || TOKEN_IS(op, "--"))
        && before >= view.range.first
        && csane_layout_operand_end(&tokens->tokens[before])
        && !csane_layout_adjacent(&tokens->tokens[before], op)) {
        csane_rule_issue(file, op->offset, STRLIT("FMT-UNARY-SPACING"),
                         "no whitespace before postfix operator");
    }
}

static bool
csane_layout_function_header_close(CsaneFile *file, int32 close) {
    if (file->toplevel == NULL) {
        return false;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if ((item->kind == CSANE_TOP_FUNCTION_DEFINITION
             || item->kind == CSANE_TOP_FUNCTION_DECLARATION)
            && close >= item->header.first && close < item->header.end) {
            return true;
        }
    }
    return false;
}

static void
csane_layout_cast_spacing_check(CsaneFile *file, int32 close) {
    Tokenization *tokens = file->tokens;
    int32 region = csane_regions_at_token(file->regions, close);
    CsaneTokenView view;
    int32 after;
    if (region < 0 || csane_layout_function_header_close(file, close)
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)
        || !csane_layout_cast(file, &view, close)) {
        return;
    }
    after = csane_tokens_next(&view, close);
    if (after < view.range.end
        && csane_layout_operand_start(&tokens->tokens[after])
        && !csane_layout_adjacent(&tokens->tokens[close],
                                  &tokens->tokens[after])) {
        csane_rule_issue(file, tokens->tokens[close].offset,
                         STRLIT("FMT-CAST-SPACING"),
                         "no whitespace after cast");
    }
}

static void
csane_layout_pointer_spacing_check(CsaneFile *file, int32 index,
                                   enum CsaneLayoutOperator kind) {
    Tokenization *tokens = file->tokens;
    Token *star = &tokens->tokens[index];
    int32 region;
    CsaneTokenView view;
    int32 before;
    int32 after;
    bool bad = false;
    if (csane_layout_return_pointer(file, index)) {
        return;
    }
    region = csane_regions_at_token(file->regions, index);
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    if (csane_layout_in_cast(file, &view, index)) {
        return;
    }
    if (kind != CSANE_LAYOUT_DECLARATOR
        && !csane_layout_function_pointer(&view, index)) {
        return;
    }
    before = csane_tokens_previous(&view, index);
    after = csane_tokens_next(&view, index);
    if (before >= view.range.first
        && !TOKEN_IS(&tokens->tokens[before], "(")
        && !TOKEN_IS(&tokens->tokens[before], "*")
        && csane_layout_adjacent(&tokens->tokens[before], star)) {
        bad = true;
    }
    if (after < view.range.end && tokens->tokens[after].kind == TOKEN_IDENT
        && !csane_layout_qualifier(&tokens->tokens[after])
        && !csane_layout_adjacent(star, &tokens->tokens[after])) {
        bad = true;
    }
    if (bad) {
        csane_rule_issue(file, star->offset, STRLIT("FMT-POINTER-SPACING"),
                         "use `type *name` pointer spacing");
    }
}

static bool
csane_layout_zero_initializer(Tokenization *tokens, CsaneTokenView *view,
                              int32 open, int32 close) {
    int32 first = csane_tokens_next(view, open);
    int32 next;
    if (first >= close || tokens->tokens[first].kind != TOKEN_LITERAL
        || tokens->tokens[first].len != 1
        || tokens->tokens[first].text[0] != '0') {
        return false;
    }
    next = csane_tokens_next(view, first);
    return next == close;
}

static bool
csane_layout_array_initializer(CsaneTokenView *view, int32 equal) {
    Tokenization *tokens = view->tokens;
    for (int32 i = csane_tokens_previous(view, equal);
         i >= view->range.first; i = csane_tokens_previous(view, i)) {
        if (TOKEN_IS(&tokens->tokens[i], ";")
            || TOKEN_IS(&tokens->tokens[i], "{")
            || TOKEN_IS(&tokens->tokens[i], "}")) {
            break;
        }
        if (TOKEN_IS(&tokens->tokens[i], "]")) {
            return true;
        }
    }
    return false;
}

static void
csane_layout_trailing_comma_check(CsaneFile *file, int32 open) {
    Tokenization *tokens = file->tokens;
    int32 region = csane_regions_at_token(file->regions, open);
    CsaneTokenView view;
    int32 equal;
    int32 close;
    int32 last;
    int32 token;
    int32 match;
    int32 elements;
    bool array;
    SourceLocation open_loc;
    SourceLocation close_loc;
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    equal = csane_tokens_previous(&view, open);
    if (equal < view.range.first || !TOKEN_IS(&tokens->tokens[equal], "=")) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0
        || csane_layout_zero_initializer(tokens, &view, open, close)) {
        return;
    }
    last = csane_tokens_previous(&view, close);
    if (last <= open || TOKEN_IS(&tokens->tokens[last], ",")) {
        return;
    }
    array = csane_layout_array_initializer(&view, equal);
    elements = 1;
    if (array) {
        token = csane_tokens_next(&view, open);
        while (token < close) {
            if (TOKEN_IS(&tokens->tokens[token], ",")) {
                elements += 1;
            } else if (TOKEN_IS(&tokens->tokens[token], "(")
                       || TOKEN_IS(&tokens->tokens[token], "[")
                       || TOKEN_IS(&tokens->tokens[token], "{")) {
                match = csane_tokens_match(&view, token);
                if (match > token && match < close) {
                    token = match;
                }
            }
            token = csane_tokens_next(&view, token);
        }
    }
    open_loc = tokenization_token_location(tokens, open);
    close_loc = tokenization_token_location(tokens, close);
    if ((array && elements >= 4)
        || (!array && open_loc.line != close_loc.line)) {
        csane_rule_issue(file, tokens->tokens[last].offset,
                         STRLIT("FMT-TRAILING-COMMA"),
                         "initializer requires a trailing comma");
    }
}

/* Only direct .field = designators are subject to this rule. A dot
 * after an array designator, as in [0].field, does not make the outer
 * array initializer a struct initializer. */
static bool
csane_layout_struct_designator(CsaneTokenView *view, int32 open, int32 close) {
    Tokenization *tokens = view->tokens;
    for (int32 i = csane_tokens_next(view, open); i < close;
         i = csane_tokens_next(view, i)) {
        if (TOKEN_IS(&tokens->tokens[i], ".")) {
            int32 before = csane_tokens_previous(view, i);
            int32 field = csane_tokens_next(view, i);
            int32 equal = csane_tokens_next(view, field);
            if ((before == open || TOKEN_IS(&tokens->tokens[before], ","))
                && field < close && equal < close
                && tokens->tokens[field].kind == TOKEN_IDENT
                && TOKEN_IS(&tokens->tokens[equal], "=")) {
                return true;
            }
        }
        if (TOKEN_IS(&tokens->tokens[i], "(")
            || TOKEN_IS(&tokens->tokens[i], "[")
            || TOKEN_IS(&tokens->tokens[i], "{")) {
            int32 match = csane_tokens_match(view, i);
            if (match < 0 || match >= close) {
                return false;
            }
            i = match;
        }
    }
    return false;
}

static void
csane_layout_struct_initializer_check(CsaneFile *file, int32 open) {
    Tokenization *tokens = file->tokens;
    int32 region = csane_regions_at_token(file->regions, open);
    CsaneTokenView view;
    int32 close;

    if (region < 0 || !csane_layout_code(file, open)
        || file->regions->items[region].kind != CSANE_REGION_CODE
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0
        || csane_layout_zero_initializer(tokens, &view, open, close)) {
        return;
    }
    if (csane_layout_struct_designator(&view, open, close)) {
        int32 first = csane_tokens_next(&view, open);
        int32 last = csane_tokens_previous(&view, close);
        SourceLocation start = tokenization_token_location(tokens, open);
        SourceLocation end = tokenization_token_location(tokens, close);
        SourceLocation member = tokenization_token_location(tokens, first);
        SourceLocation previous = tokenization_token_location(tokens, last);

        if (start.line == end.line) {
            CsaneTokenView members;
            TokenRange content = {open + 1, close};

            if (csane_tokens_subview(&members, &view, content)) {
                int32 comma = csane_tokens_top_level(&members, open,
                                                     STRLIT(","));
                if (comma >= 0
                    && (comma == close
                        || csane_tokens_next(&members, comma) == close)) {
                    return;
                }
            }
        }
        if (start.line == member.line || end.line == previous.line) {
            csane_rule_issue(file, tokens->tokens[open].offset,
                             STRLIT("FMT-STRUCT-INITIALIZER"),
                             "put designated struct initializer members "
                             "and braces on separate lines");
        }
    }
}

/* Braces separated only by horizontal whitespace or comments still need
 * distinct lines. Token positions avoid matching braces in literals or
 * comments, and preprocessor roles keep unrelated directives apart. */
static void
csane_layout_nested_braces_check(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *first = &tokens->tokens[index];
    int32 next;
    enum CPreprocessorTokenRole role = C_PP_TOKEN_CODE;
    SourceLocation a;
    SourceLocation b;

    if (!TOKEN_IS(first, "{") && !TOKEN_IS(first, "}")) {
        return;
    }
    if (file->preprocessor->token_roles != NULL) {
        role = file->preprocessor->token_roles[index];
        if (role != C_PP_TOKEN_CODE
            && role != C_PP_TOKEN_MACRO_REPLACEMENT) {
            return;
        }
    }
    next = tokenization_next_significant(tokens, index);
    if (next >= ARRAY_LEN(tokens->tokens)) {
        return;
    }
    if (TOKEN_IS(first, "{")) {
        if (!TOKEN_IS(&tokens->tokens[next], "{")) {
            return;
        }
    } else if (!TOKEN_IS(&tokens->tokens[next], "}")) {
        return;
    }
    if (file->preprocessor->token_roles != NULL) {
        if (file->preprocessor->token_roles[next] != role) {
            return;
        }
        if (role == C_PP_TOKEN_MACRO_REPLACEMENT) {
            if (file->preprocessor->token_directives[index]
                != file->preprocessor->token_directives[next]) {
                return;
            }
        }
    }
    a = tokenization_token_location(tokens, index);
    b = tokenization_token_location(tokens, next);
    if (a.line == b.line) {
        csane_rule_issue(file, tokens->tokens[next].offset,
                         STRLIT("FMT-NESTED-BRACES"),
                         "put consecutive braces on separate lines "
                         "with nesting indentation");
    }
}

/* Distinguish later array declarators in a comma-separated declaration
 * from subscripts in an initializer. Delimiters enclosing the bracket mean
 * the bracket is inside an expression instead of a declarator. */
static bool
csane_layout_array_declaration_list(CsaneFile *file, CsaneTokenView *view,
                                    int32 index) {
    Tokenization *tokens = view->tokens;
    TokenRange range = {0};
    int32 first;
    bool initializer = false;

    if (file->layout_statement_index != NULL
        && index < ARRAY_LEN(file->layout_statement_index)) {
        int32 candidate = file->layout_statement_index[index];
        if (candidate >= 0) {
            CsaneStatement *stmt = &file->statements->items[candidate];
            if (stmt->kind == CSANE_STMT_SIMPLE) {
                range = stmt->tokens;
            }
        }
    }
    if (range.end == 0 && file->layout_top_index != NULL
        && index < ARRAY_LEN(file->layout_top_index)) {
        int32 candidate = file->layout_top_index[index];
        if (candidate >= 0) {
            range = file->toplevel->items[candidate].header;
        }
    }
    if (range.end <= index) {
        return false;
    }
    first = csane_tokens_next(view, range.first - 1);
    while (first < index && !csane_layout_known_type(file, view, first)
           && (csane_layout_qualifier(&tokens->tokens[first])
               || csane_layout_is(tokens, first, STRLIT("signed"))
               || csane_layout_is(tokens, first, STRLIT("unsigned")))) {
        first = csane_tokens_next(view, first);
    }
    if (first >= index || !csane_layout_known_type(file, view, first)) {
        return false;
    }
    for (int32 i = csane_tokens_next(view, first); i < index;) {
        if (csane_layout_is(tokens, i, STRLIT("="))) {
            initializer = true;
        } else if (csane_layout_is(tokens, i, STRLIT(","))) {
            initializer = false;
        } else if (csane_layout_is(tokens, i, STRLIT("("))
                   || csane_layout_is(tokens, i, STRLIT("["))
                   || csane_layout_is(tokens, i, STRLIT("{"))) {
            int32 match = csane_tokens_match(view, i);
            if (match < 0 || match >= index) {
                return false;
            }
            i = match;
        }
        i = csane_tokens_next(view, i);
    }
    return !initializer;
}

/* Array subscripts must start on the opening bracket's line. Keep the
 * diagnostic local to a balanced bracket pair and its source region; a
 * conditional branch or directive must not supply a missing index. */
static bool
csane_layout_array_declaration(CsaneFile *file, CsaneTokenView *view,
                               int32 index, int32 before) {
    Tokenization *tokens = view->tokens;
    int32 type;

    /* Walk back through earlier dimensions of the same declarator. */
    while (csane_layout_is(tokens, before, STRLIT("]"))) {
        int32 open = csane_tokens_match(view, before);
        if (open < 0) {
            return false;
        }
        before = csane_tokens_previous(view, open);
    }
    /* Parenthesized pointer-to-array declarators: int32 (*p)[N]. */
    if (csane_layout_is(tokens, before, STRLIT(")"))) {
        int32 open = csane_tokens_match(view, before);
        int32 star = csane_tokens_next(view, open);
        if (open < 0 || !csane_layout_is(tokens, star, STRLIT("*"))) {
            return false;
        }
        type = csane_tokens_previous(view, open);
    } else {
        if (before < view->range.first
            || tokens->tokens[before].kind != TOKEN_IDENT) {
            return false;
        }
        type = csane_tokens_previous(view, before);
        while (csane_layout_is(tokens, type, STRLIT("*"))) {
            type = csane_tokens_previous(view, type);
        }
    }
    return type >= view->range.first
           && csane_layout_known_type(file, view, type)
           && (csane_layout_statement_prefix(file, view, index, type)
               || csane_layout_local_prefix(view, type));
}

/* Each top-level continuation line in a subscript starts at the same
 * column as the first index token. Nested calls and bracketed expressions
 * own their own continuation alignment, so skip balanced nested groups. */
static void
csane_layout_array_alignment_check(CsaneFile *file, CsaneTokenView *view,
                                   int32 first, int32 close) {
    Tokenization *tokens = file->tokens;
    SourceLocation anchor = tokenization_token_location(tokens, first);

    for (int32 i = first; i < close; i = csane_tokens_next(view, i)) {
        SourceLocation current;
        SourceLocation previous_location;
        int32 previous;

        if (csane_layout_is(tokens, i, STRLIT("\\"))) {
            continue;
        }
        if (csane_layout_is(tokens, i, STRLIT("("))
            || csane_layout_is(tokens, i, STRLIT("["))
            || csane_layout_is(tokens, i, STRLIT("{"))) {
            int32 match = csane_tokens_match(view, i);
            if (match <= i || match >= close) {
                return;
            }
            i = match;
            continue;
        }
        if (i == first) {
            continue;
        }
        previous = tokenization_previous_significant(tokens, i);
        if (previous < 0) {
            continue;
        }
        current = tokenization_token_location(tokens, i);
        previous_location = tokenization_token_location(tokens, previous);
        if (current.line > previous_location.line
            && current.column != anchor.column) {
            csane_rule_issue(file, tokens->tokens[i].offset,
                             STRLIT("FMT-ARRAY-ALIGN"),
                             "array index continuation is not aligned "
                             "with the first index token");
            return;
        }
    }
    return;
}

static void
csane_layout_array_first_check(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    int32 region;
    CsaneTokenView view;
    int32 before;
    int32 first;
    int32 close;
    SourceLocation start;
    SourceLocation argument;

    if (!csane_layout_is(tokens, index, STRLIT("["))
        || (!csane_layout_code(file, index)
            && (file->preprocessor->token_roles == NULL
                || file->preprocessor->token_roles[index]
                   != C_PP_TOKEN_MACRO_REPLACEMENT))) {
        return;
    }
    region = csane_regions_at_token(file->regions, index);
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              file->regions->items[region].tokens)) {
        return;
    }
    before = csane_tokens_previous(&view, index);
    if (before < view.range.first
        || (tokens->tokens[before].kind != TOKEN_IDENT
            && !csane_layout_is(tokens, before, STRLIT(")"))
            && !csane_layout_is(tokens, before, STRLIT("]")))) {
        return;
    }
    close = csane_tokens_match(&view, index);
    first = csane_tokens_next(&view, index);
    /* Macro continuation backslashes are source tokens, not indices. */
    if (file->preprocessor->token_roles != NULL
        && file->preprocessor->token_roles[index]
           == C_PP_TOKEN_MACRO_REPLACEMENT) {
        while (first < close
               && csane_layout_is(tokens, first, STRLIT("\\"))) {
            first = csane_tokens_next(&view, first);
        }
    }
    if (close <= first || first >= view.range.end
        || csane_layout_is(tokens, first, STRLIT("["))
        || csane_layout_array_declaration(file, &view, index, before)
        || csane_layout_array_declaration_list(file, &view, index)) {
        return;
    }
    start = tokenization_token_location(tokens, index);
    argument = tokenization_token_location(tokens, first);
    if (start.line != argument.line) {
        csane_rule_issue(file, tokens->tokens[index].offset,
                         STRLIT("FMT-ARRAY-FIRST"),
                         "array index must begin on the opening "
                         "bracket's line");
        return;
    }
    csane_layout_array_alignment_check(file, &view, first, close);
    return;
}

static void
csane_layout_local_style_check(CsaneFile *file, int32 index,
                               enum CsaneLayoutOperator kind) {
    Token *token = &file->tokens->tokens[index];
    if (!csane_layout_code(file, index)) {
        return;
    }
    if (TOKEN_IS(token, ",")) {
        csane_layout_comma_check(file, index);
    } else if (TOKEN_IS(token, ".") || TOKEN_IS(token, "->")) {
        csane_layout_member_check(file, index);
    } else if (TOKEN_IS(token, "!") || TOKEN_IS(token, "~")
               || TOKEN_IS(token, "+") || TOKEN_IS(token, "-")
               || TOKEN_IS(token, "&") || TOKEN_IS(token, "*")
               || TOKEN_IS(token, "++") || TOKEN_IS(token, "--")) {
        csane_layout_unary_check(file, index);
    }
    if (TOKEN_IS(token, ")")) {
        csane_layout_cast_spacing_check(file, index);
    }
    if (TOKEN_IS(token, "*")) {
        csane_layout_pointer_spacing_check(file, index, kind);
    }
    if (TOKEN_IS(token, "++") || TOKEN_IS(token, "--")) {
        int32 region = csane_regions_at_token(file->regions, index);
        CsaneTokenView view;
        bool allowed;

        allowed = region >= 0
                  && csane_tokens_view(&view, file->tokens,
                                       file->regions->items[region].tokens)
                  && csane_layout_inside_brackets(&view,
                                                  view.range.first, index);
        if (!allowed && region >= 0
            && csane_tokens_view(&view, file->tokens,
                                 file->regions->items[region].tokens)) {
            int32 before = csane_tokens_previous(&view, index);
            int32 after = csane_tokens_next(&view, index);

            if (TOKEN_IS(token, "--") && before >= view.range.first
                && after < view.range.end
                && file->tokens->tokens[before].kind == TOKEN_IDENT
                && TOKEN_IS(&file->tokens->tokens[after], ")")) {
                int32 open = csane_tokens_match(&view, after);
                int32 first = csane_tokens_previous(&view, before);
                int32 keyword = csane_tokens_previous(&view, open);

                allowed = open >= view.range.first && first == open
                          && TOKEN_IS(&file->tokens->tokens[keyword],
                                      "while");
            } else if (TOKEN_IS(token, "++")
                       && before >= view.range.first
                       && after < view.range.end
                       && file->tokens->tokens[before].kind == TOKEN_IDENT
                       && TOKEN_IS(&file->tokens->tokens[after], ";")) {
                int32 dereference = csane_tokens_previous(&view, before);
                int32 equal = csane_tokens_previous(&view, dereference);

                while (equal >= view.range.first
                       && csane_layout_cast(file, &view, equal)) {
                    int32 open = csane_tokens_match(&view, equal);
                    equal = csane_tokens_previous(&view, open);
                }
                allowed = dereference >= view.range.first
                          && equal >= view.range.first
                          && TOKEN_IS(&file->tokens->tokens[dereference], "*")
                          && TOKEN_IS(&file->tokens->tokens[equal], "=");
            }
        }
        if (!allowed) {
            csane_rule_issue(file, token->offset, STRLIT("STYLE-INCDEC"),
                             "prefer += 1 or -= 1 instead of ++ or --");
        }
    }
    if (TOKEN_IS(token, "{")) {
        csane_layout_trailing_comma_check(file, index);
        csane_layout_struct_initializer_check(file, index);
    }
}

/* Arithmetic operators in simple assignment RHS expressions are checked
 * against the first token of their precedence group. Calls and subscripts
 * are opaque operands; arithmetic in their arguments belongs to other rules.
 * A bounded expression parse avoids diagnosing ambiguous C syntax. */
typedef struct CsaneArithFinding {
    int32 token;
    bool alignment;
} CsaneArithFinding;

typedef struct CsaneArithParser {
    CsaneTokenView view;
    int32 current;
    int32 end;
    CsaneArithFinding *findings;
} CsaneArithParser;

static int32
csane_layout_arith_precedence(Token *token) {
    if (TOKEN_IS(token, "+") || TOKEN_IS(token, "-")) {
        return 1;
    }
    if (TOKEN_IS(token, "*") || TOKEN_IS(token, "/")
        || TOKEN_IS(token, "%")) {
        return 2;
    }
    return 0;
}

static bool
csane_layout_arith_consume(CsaneArithParser *parser, char *text,
                           int32 text_len) {
    if (parser->current >= parser->end
        || !csane_layout_is(parser->view.tokens, parser->current,
                            text, text_len)) {
        return false;
    }
    parser->current = csane_tokens_next(&parser->view, parser->current);
    return true;
}

static int32 csane_layout_arith_expression(CsaneArithParser *, int32, int32);

static int32
csane_layout_arith_operand(CsaneArithParser *parser, int32 depth) {
    Tokenization *tokens = parser->view.tokens;
    int32 first = parser->current;
    int32 close;

    if (first >= parser->end || depth > 64) {
        return -1;
    }
    if (csane_layout_is(tokens, first, STRLIT("+"))
        || csane_layout_is(tokens, first, STRLIT("-"))
        || csane_layout_is(tokens, first, STRLIT("*"))
        || csane_layout_is(tokens, first, STRLIT("&"))
        || csane_layout_is(tokens, first, STRLIT("!"))
        || csane_layout_is(tokens, first, STRLIT("~"))) {
        parser->current = csane_tokens_next(&parser->view, first);
        if (csane_layout_arith_operand(parser, depth + 1) < 0) {
            return -1;
        }
    } else if (csane_layout_arith_consume(parser, STRLIT("("))) {
        if (csane_layout_arith_expression(parser, 1, depth + 1) < 0
            || !csane_layout_arith_consume(parser, STRLIT(")"))) {
            return -1;
        }
    } else if (tokens->tokens[first].kind == TOKEN_IDENT
               || tokens->tokens[first].kind == TOKEN_LITERAL) {
        parser->current = csane_tokens_next(&parser->view, first);
    } else {
        return -1;
    }

    while (parser->current < parser->end) {
        int32 index = parser->current;
        if (csane_layout_is(tokens, index, STRLIT("("))
            || csane_layout_is(tokens, index, STRLIT("["))) {
            close = csane_tokens_match(&parser->view, index);
            if (close <= index || close >= parser->end) {
                return -1;
            }
            parser->current = csane_tokens_next(&parser->view, close);
        } else if (csane_layout_is(tokens, index, STRLIT("."))
                   || csane_layout_is(tokens, index, STRLIT("->"))) {
            int32 field = csane_tokens_next(&parser->view, index);
            if (field >= parser->end
                || tokens->tokens[field].kind != TOKEN_IDENT) {
                return -1;
            }
            parser->current = csane_tokens_next(&parser->view, field);
        } else {
            break;
        }
    }
    return first;
}

static void
csane_layout_arith_operator(CsaneArithParser *parser, int32 index,
                            int32 anchor, int32 right) {
    Tokenization *tokens = parser->view.tokens;
    int32 previous = csane_tokens_previous(&parser->view, index);
    SourceLocation op = tokenization_token_location(tokens, index);
    SourceLocation start = tokenization_token_location(tokens, anchor);
    SourceLocation operand = tokenization_token_location(tokens, right);

    if (op.line < operand.line) {
        ARRAY_PUSH(parser->findings,
                   ((CsaneArithFinding){index, false}));
    }
    if (previous >= parser->view.range.first
        && op.line > tokenization_token_location(tokens, previous).line
        && op.column != start.column) {
        ARRAY_PUSH(parser->findings,
                   ((CsaneArithFinding){index, true}));
    }
    return;
}

static int32
csane_layout_arith_expression(CsaneArithParser *parser, int32 precedence,
                              int32 depth) {
    int32 first = csane_layout_arith_operand(parser, depth);
    if (first < 0) {
        return -1;
    }
    while (parser->current < parser->end) {
        int32 index = parser->current;
        Token *token = &parser->view.tokens->tokens[index];
        int32 rank = csane_layout_arith_precedence(token);
        int32 right;
        if (rank < precedence) {
            break;
        }
        parser->current = csane_tokens_next(&parser->view, index);
        right = parser->current;
        if (right >= parser->end) {
            return -1;
        }
        csane_layout_arith_operator(parser, index, first, right);
        if (csane_layout_arith_expression(parser, rank + 1, depth + 1) < 0) {
            return -1;
        }
    }
    return first;
}

static void
csane_layout_arith_assignment_check(CsaneFile *file, int32 equal) {
    Tokenization *tokens = file->tokens;
    TokenRange statement = {0};
    CsaneTokenView view;
    CsaneArithParser parser = {0};
    int32 region;
    int32 before;
    int32 first;
    int32 semicolon;

    if (!csane_layout_code(file, equal)
        || !csane_layout_is(tokens, equal, STRLIT("="))) {
        return;
    }
    if (file->layout_statement_index != NULL
        && equal < ARRAY_LEN(file->layout_statement_index)) {
        int32 item = file->layout_statement_index[equal];
        if (item >= 0
            && file->statements->items[item].kind == CSANE_STMT_SIMPLE) {
            statement = file->statements->items[item].tokens;
        }
    }
    if (statement.end == 0 && file->layout_top_index != NULL
        && equal < ARRAY_LEN(file->layout_top_index)) {
        int32 item = file->layout_top_index[equal];
        if (item >= 0
            && file->toplevel->items[item].kind == CSANE_TOP_DECLARATION) {
            statement = file->toplevel->items[item].header;
        }
    }
    region = csane_regions_at_token(file->regions, equal);
    if (statement.end <= equal || region < 0) {
        return;
    }
    if (statement.first < file->regions->items[region].tokens.first
        || statement.end > file->regions->items[region].tokens.end
        || !csane_tokens_view(&view, tokens, statement)) {
        return;
    }
    before = csane_tokens_previous(&view, equal);
    if (before < view.range.first
        || tokens->tokens[before].kind != TOKEN_IDENT
        || csane_tokens_top_level(&view, view.range.first - 1,
                                  STRLIT("=")) != equal) {
        return;
    }
    first = csane_tokens_next(&view, equal);
    semicolon = csane_tokens_previous(&view, view.range.end);
    if (first >= semicolon
        || !csane_layout_is(tokens, semicolon, STRLIT(";"))) {
        return;
    }
    /* Comma-separated declarations and compound/conditional expressions
     * are intentionally excluded from this first, narrow rule. */
    parser.view = view;
    parser.current = first;
    parser.end = semicolon;
    if (csane_layout_arith_expression(&parser, 1, 0) >= 0
        && parser.current == parser.end) {
        for (int32 i = 0; i < ARRAY_LEN(parser.findings); i += 1) {
            CsaneArithFinding *finding = &parser.findings[i];
            if (finding->alignment) {
                csane_rule_issue(file, tokens->tokens[finding->token].offset,
                                 STRLIT("FMT-ARITH-ALIGN"),
                                 "arithmetic operator must align with "
                                 "its precedence group");
            } else {
                csane_rule_issue(file, tokens->tokens[finding->token].offset,
                                 STRLIT("FMT-ARITH-WRAP"),
                                 "break before arithmetic operator, "
                                 "not after it");
            }
        }
    }
    ARRAY_FREE(parser.findings);
    return;
}

void
csane_layout_check(CsaneFile *file) {
    if (file->is_ryu || csane_layout_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->tokens->tokens); i += 1) {
        Token *op = &file->tokens->tokens[i];
        enum CsaneLayoutOperator kind = CSANE_LAYOUT_SKIP;
        if (op->kind == TOKEN_OPERATOR) {
            kind = csane_layout_classify(file, i);
        }
        csane_layout_nested_braces_check(file, i);
        csane_layout_array_first_check(file, i);
        csane_layout_arith_assignment_check(file, i);
        csane_layout_local_style_check(file, i, kind);
        if (kind == CSANE_LAYOUT_MULTIPLY) {
            if (csane_layout_spacing(file, i, true)) {
                csane_rule_issue(file, op->offset, STRLIT("FMT-MULTIPLY"),
                                 "no spaces around multiplication");
            }
        } else if (kind == CSANE_LAYOUT_BINARY) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, op->offset, STRLIT("FMT-BINARY-SPACING"),
                                 "spaces required around binary operator");
            }
        } else if (kind == CSANE_LAYOUT_ASSIGN) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, op->offset, STRLIT("FMT-ASSIGN-SPACING"),
                                 "assignment requires surrounding whitespace");
            }
        }
    }
}

void
csane_layout_assignment_check(CsaneFile *file) {
    if (!file->is_ryu && !csane_layout_generated(file->path, file->path_len)) {
        csane_layout_leading_assignment(file);
    }
}

#endif /* CSANECHECK_LAYOUT_C */
