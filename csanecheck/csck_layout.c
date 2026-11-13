// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_LAYOUT_C)
#define CSANECHECK_LAYOUT_C

#include "cbase.h"
#include "csck.h"

/* Give repeated region-range lookups a compact spelling in the layout scan. */
static TokenRange
csane_rt_tokens(CsaneFile *file, int32 region) {
    return file->regions->items[region].tokens;
}
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
csane_layout_is(Tokenization *tokens, int32 token_index,
                char *name, int32 name_len) {
    return token_index >= 0 && token_index < ARRAY_LEN(tokens->tokens)
           && TOKEN_IS(&tokens->tokens[token_index], name, name_len);
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
csane_layout_known_type(CsaneFile *file, CsaneTokenView *view, int32 index) {
    Token *token = &view->tokens->tokens[index];
    return csane_type_name(token)
           || csane_type_hints_typedef(file->type_hints, token)
           || csane_layout_tag_prefix(view, index);
}

/* Type arguments such as va_arg(ap, int32 *) and cast fragments such as
 * (const int32 *) are type syntax even when they occur inside expressions. */

/* At a declaration boundary, "Type *name = ..." is a declarator. The
 * otherwise identical "x *y;" may be an expression or a declaration, so
 * it must remain unresolved rather than produce a false multiplication. */
static enum CsaneLayoutOperator
csane_layout_pointer(CsaneFile *file, CsaneTokenView *view, int32 index,
                     int32 before, int32 after) {
                         bool _i34_result;
                         bool _i34_predicate;
    Tokenization *layout_tokens = view->tokens;
    Token *name = &layout_tokens->tokens[after];
    int32 following;
    Token *tail;

    if (csane_layout_in_cast(file, view, index)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(&layout_tokens->tokens[before], ")")
        && csane_type_hints_macro_typedef(file->type_hints, view, before)) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(name, ")")
        && csane_layout_is(layout_tokens, csane_tokens_previous(view, before),
                           STRLIT("("))) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    do {
        Tokenization *tokens = view->tokens;
        int32 open = -1;
        int32 close = -1;
        int32 nested_open = -1;
        int32 depth = 0;
        bool _i33_predicate;
        bool _i33_result;
        do {
            int32 *_i33_open = &open;
            int32 *_i33_close = &close;

            Tokenization *nested_tokens = view->tokens;
            if (file->toplevel == NULL) {
                _i33_result = false;
                goto _i33_done;
            }
            for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
                CsaneTopItem *item = &file->toplevel->items[i];
                int32 p_open;
                int32 p_close;
                if ((item->kind == CSANE_TOP_TYPE_DECLARATION
                     || item->kind == CSANE_TOP_DECLARATION
                     || item->kind == CSANE_TOP_MEMBER_DECLARATION)
                    && index >= item->header.first
                        && index < item->header.end) {
                    for (p_open = csane_tokens_previous(view, index);
                         p_open >= item->header.first
                         && p_open >= view->range.first;
                         p_open = csane_tokens_previous(view, p_open)) {
                        int32 declarator_close;
                        int32 declarator_open;
                        int32 first;
                        int32 type;

                        if (!csane_layout_is(nested_tokens, p_open,
                                             STRLIT("("))) {
                            continue;
                        }
                        p_close = csane_tokens_match(view, p_open);
                        if (p_close <= index
                            || p_close >= item->header.end) {
                            continue;
                        }
                        declarator_close = csane_tokens_previous(view, p_open);
                        if (declarator_close >= item->header.first
                            && nested_tokens->tokens[declarator_close].kind
                                == TOKEN_IDENT) {
                            type = csane_tokens_previous(view,
                                                         declarator_close);
                            if (csane_layout_is(nested_tokens, type,
                                                STRLIT("*"))) {
                                type = csane_tokens_previous(view, type);
                            }
                            if (type >= item->header.first) {
                                Token *candidate = &nested_tokens->tokens[type];
                                bool type_hint =
                                    csane_type_hints_typedef(file->type_hints,
                                                             candidate);
                                if (csane_type_name(candidate) || type_hint
                                    || csane_layout_tag_prefix(view, type)) {
                                    *_i33_open = p_open;
                                    *_i33_close = p_close;
                                    _i33_result = true;
                                    goto _i33_done;
                                }
                            }
                        }
                        if (!csane_layout_is(nested_tokens, declarator_close,
                                             STRLIT(")"))) {
                            continue;
                        }
                        declarator_open = csane_tokens_match(view,
                                                             declarator_close);
                        first = csane_tokens_next(view, declarator_open);
                        if (declarator_open >= item->header.first
                            && first < declarator_close
                            && csane_layout_is(nested_tokens, first,
                                               STRLIT("*"))) {
                            *_i33_open = p_open;
                            *_i33_close = p_close;
                            _i33_result = true;
                            goto _i33_done;
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
                p_open = csane_tokens_next(view, item->name_token);
                if (!csane_layout_is(nested_tokens, p_open, STRLIT("("))) {
                    continue;
                }
                p_close = csane_tokens_match(view, p_open);
                if (p_close < 0 || index <= p_open
                    || index >= p_close) {
                    continue;
                }
                *_i33_open = p_open;
                *_i33_close = p_close;
                _i33_result = true;
                goto _i33_done;
            }
            _i33_result = false;
            goto _i33_done;
            _i33_done: ;
        } while (false);
        _i33_predicate = !_i33_result;
        if (!_i33_predicate) {
            _i33_predicate = csane_layout_inside_brackets(view, open, index);
        }
        if (_i33_predicate) {
            _i34_result = false;
            goto _i34_done;
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
            _i34_result = true;
            goto _i34_done;
        }
        /* Parenthesized pointer declarators, as in int (*compare)(...), and
         * parameters of the function type that immediately follows one. */
        if (csane_layout_is(tokens, csane_tokens_previous(view, index),
                            STRLIT("("))) {
            _i34_result = true;
            goto _i34_done;
        }
        if (depth == 1 && nested_open >= 0) {
            int32 declarator_close = csane_tokens_previous(view, nested_open);
            int32 declarator_open;
            int32 first;
            if (!csane_layout_is(tokens, declarator_close, STRLIT(")"))) {
                _i34_result = false;
                goto _i34_done;
            }
            declarator_open = csane_tokens_match(view, declarator_close);
            if (declarator_open < 0) {
                _i34_result = false;
                goto _i34_done;
            }
            first = csane_tokens_next(view, declarator_open);
            _i34_result = first < declarator_close
                   && TOKEN_IS(&tokens->tokens[first], "*");
            goto _i34_done;
        }
        _i34_result = false;
        goto _i34_done;
        _i34_done: ;
    } while (false);
    _i34_predicate = _i34_result;
    if (!_i34_predicate) {
        bool _i35_result;
        do {
            Tokenization *tokens = view->tokens;
            int32 first = before;
            int32 previous;
            if (!csane_layout_is(tokens, after, STRLIT(")"))
                || !csane_layout_known_type(file, view, first)) {
                _i35_result = false;
                goto _i35_done;
            }
            previous = csane_tokens_previous(view, first);
            while (previous >= view->range.first
                   && csane_layout_qualifier(&tokens->tokens[previous])) {
                first = previous;
                previous = csane_tokens_previous(view, first);
            }
            _i35_result = previous >= view->range.first
                   && (TOKEN_IS(&tokens->tokens[previous], "(")
                       || TOKEN_IS(&tokens->tokens[previous], ","));
            goto _i35_done;
            _i35_done: ;
        } while (false);
        _i34_predicate = _i35_result;
    }
    if (_i34_predicate) {
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

        if (csane_layout_is(layout_tokens, pointer, STRLIT("*"))
            && declarator >= view->range.first
            && declarator < view->range.end
            && layout_tokens->tokens[declarator].kind == TOKEN_IDENT
            && csane_layout_is(layout_tokens, close, STRLIT(")"))
            && csane_tokens_match(view, after) == close
            && csane_layout_is(layout_tokens, parameters, STRLIT("("))) {
            return CSANE_LAYOUT_DECLARATOR;
        }
    }
    if (name->kind != TOKEN_IDENT && !TOKEN_IS(name, "*")) {
        return CSANE_LAYOUT_MULTIPLY;
    }
    if (csane_layout_is(layout_tokens, csane_tokens_previous(view, before),
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
    tail = &layout_tokens->tokens[following];
    if (TOKEN_IS(tail, "=") || TOKEN_IS(tail, ",")
        || TOKEN_IS(tail, "[") || TOKEN_IS(tail, "(")) {
        return CSANE_LAYOUT_DECLARATOR;
    }
    if (TOKEN_IS(tail, ";")) {
        /* A top-level declaration cannot instead be an expression
         * statement. Local "T *p;" remains syntactically ambiguous. */
        if (csane_name_kind(&layout_tokens->tokens[before])
            == CSANE_NAME_PRIMITIVE
            || csane_type_hints_typedef(file->type_hints,
                                        &layout_tokens->tokens[before])
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
    bool _i32_result;
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
    binary = TOKEN_IS(op, "+") || TOKEN_IS(op, "-")
             || TOKEN_IS(op, "&") || TOKEN_IS(op, "<<")
             || TOKEN_IS(op, ">>") || TOKEN_IS(op, "&&")
             || TOKEN_IS(op, "||") || TOKEN_IS(op, "==")
             || TOKEN_IS(op, "!=") || TOKEN_IS(op, "<")
             || TOKEN_IS(op, "<=") || TOKEN_IS(op, ">")
             || TOKEN_IS(op, ">=");
    do {
        int32 count = LENGTH(csane_layout_assignments);
        for (int32 i = 0; i < count; i += 1) {
            StrFlex *word = csane_layout_assignments[i];
            if (TOKEN_IS(op, word->data, word->len)) {
                _i32_result = true;
                goto _i32_done;
            }
        }
        _i32_result = false;
        goto _i32_done;
        _i32_done: ;
    } while (false);
    assign = _i32_result;
    if (!multiply && !binary && !assign) {
        return CSANE_LAYOUT_SKIP;
    }
    region = csane_regions_at_token(file->regions, index);
    if (region < 0
        || !csane_tokens_view(&view, tokens,
                              csane_rt_tokens(file, region))) {
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
    /* A bare operator used as one call argument has no operands in the
     * source token stream and must not be classified as binary. */
    if (!assign
        && (TOKEN_IS(&tokens->tokens[before], "(")
            || TOKEN_IS(&tokens->tokens[before], ","))
        && (TOKEN_IS(&tokens->tokens[after], ")")
            || TOKEN_IS(&tokens->tokens[after], ","))) {
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
    if (!csane_tokens_view(&view, tokens, csane_rt_tokens(file, region))) {
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

/* Braces separated only by horizontal whitespace or comments still need
 * distinct lines. Token positions avoid matching braces in literals or
 * comments, and preprocessor roles keep unrelated directives apart. */

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

    /* Parenthesized array types in compound literals or casts are not
     * subscripts: (int32[length]){0} and (NamedType[length])value. */
    if (csane_layout_known_type(file, view, before)
        && csane_layout_is(tokens, csane_tokens_previous(view, before),
                           STRLIT("("))) {
        return true;
    }
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

/* A split subscript is unnecessary when joining its physical lines still
 * fits the column limit. Include the entire first line and the closing
 * bracket's suffix, so surrounding assignments and punctuation count too.
 * The shared wrap guard excludes comments, directives and macro expansions.
 */
static void
csane_layout_array_wrap_check(CsaneFile *file, CsaneTokenView *view,
                              int32 open, int32 close) {
    Tokenization *tokens = view->tokens;
    SourceLocation first = tokenization_token_location(tokens, open);
    SourceLocation last = tokenization_token_location(tokens, close);
    int32 start = tokens->tokens[open].offset;
    int32 end;
    int32 width;

    if (first.line == last.line
        || !csane_wrap_safe(file, open, close, false)) {
        return;
    }
    while (start > 0 && tokens->text[start - 1] != '\n') {
        start -= 1;
    }
    end = tokenization_physical_line_end_offset(tokens, last.line);
    if (end > start && tokens->text[end - 1] == '\n') {
        end -= 1;
    }
    width = csane_wrap_flat_length(tokens->text, start, end,
                                   file->column_limit);
    if (width >= 0 && (file->column_limit <= 0
                       || width <= file->column_limit)) {
        csane_rule_issue(file, tokens->tokens[open].offset,
                         STRLIT("FMT-ARRAY-WRAP"),
                         "array indexing fits on one line but is split "
                         "across multiple lines");
    }
    return;
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

static int32
csane_layout_arith_expression(CsaneArithParser *parser, int32 precedence,
                              int32 depth) {
    int32 first = csane_layout_arith_operand(parser, depth);
    if (first < 0) {
        return -1;
    }
    while (parser->current < parser->end) {
        int32 right;
        int32 rank;
        int32 index = parser->current;
        Token *token = &parser->view.tokens->tokens[index];
        int32 in_result_precedence;
        do {
            if (TOKEN_IS(token, "+") || TOKEN_IS(token, "-")) {
                in_result_precedence = 1;
                break;
            }
            if (TOKEN_IS(token, "*") || TOKEN_IS(token, "/")
                || TOKEN_IS(token, "%")) {
                in_result_precedence = 2;
                break;
            }
            in_result_precedence = 0;
        } while (false);
        rank = in_result_precedence;
        if (rank < precedence) {
            break;
        }
        parser->current = csane_tokens_next(&parser->view, index);
        right = parser->current;
        if (right >= parser->end) {
            return -1;
        }
        {
            CsaneArithParser *in_parser = parser;
            int32 in_anchor = first;

            Tokenization *tokens = in_parser->view.tokens;
            int32 previous = csane_tokens_previous(&in_parser->view, index);
            SourceLocation op = tokenization_token_location(tokens, index);
            SourceLocation start =
                tokenization_token_location(tokens, in_anchor);
            SourceLocation operand = tokenization_token_location(tokens, right);

            if (op.line < operand.line) {
                ARRAY_PUSH(in_parser->findings,
                           ((CsaneArithFinding){index, false}));
            }
            if (previous >= in_parser->view.range.first
                && op.line > tokenization_token_location(tokens, previous).line
                && op.column != start.column) {
                ARRAY_PUSH(in_parser->findings,
                           ((CsaneArithFinding){index, true}));
            }
        }
        if (csane_layout_arith_expression(parser, rank + 1, depth + 1) < 0) {
            return -1;
        }
    }
    return first;
}

void
csane_layout_check(CsaneFile *file) {
    if (file->is_ryu || csane_layout_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->tokens->tokens); i += 1) {
        Token *outer_op = &file->tokens->tokens[i];
        enum CsaneLayoutOperator kind = CSANE_LAYOUT_SKIP;
        if (outer_op->kind == TOKEN_OPERATOR) {
            kind = csane_layout_classify(file, i);
        }
        do {
            int32 in_index = i;

            Tokenization *tokens = file->tokens;
            Token *first = &tokens->tokens[in_index];
            int32 next;
            enum CPreprocessorTokenRole role = C_PP_TOKEN_CODE;
            SourceLocation a;
            SourceLocation b;

            if (!TOKEN_IS(first, "{") && !TOKEN_IS(first, "}")) {
                break;
            }
            if (file->preprocessor->token_roles != NULL) {
                role = file->preprocessor->token_roles[in_index];
                if (role != C_PP_TOKEN_CODE
                    && role != C_PP_TOKEN_MACRO_REPLACEMENT) {
                    break;
                }
            }
            next = tokenization_next_significant(tokens, in_index);
            if (next >= ARRAY_LEN(tokens->tokens)) {
                break;
            }
            if (TOKEN_IS(first, "{")) {
                if (!TOKEN_IS(&tokens->tokens[next], "{")) {
                    break;
                }
            } else if (!TOKEN_IS(&tokens->tokens[next], "}")) {
                break;
            }
            if (file->preprocessor->token_roles != NULL) {
                if (file->preprocessor->token_roles[next] != role) {
                    break;
                }
                if (role == C_PP_TOKEN_MACRO_REPLACEMENT) {
                    if (file->preprocessor->token_directives[in_index]
                        != file->preprocessor->token_directives[next]) {
                        break;
                    }
                }
            }
            a = tokenization_token_location(tokens, in_index);
            b = tokenization_token_location(tokens, next);
            if (a.line == b.line) {
                csane_rule_issue(file, tokens->tokens[next].offset,
                                 STRLIT("FMT-NESTED-BRACES"),
                                 "put consecutive braces on separate lines "
                                 "with nesting indentation");
            }
        } while (false);
        do {
            CsaneFile *in_file = file;
            int32 in_index = i;

            Tokenization *tokens = in_file->tokens;
            int32 region;
            CsaneTokenView view;
            int32 before;
            int32 first;
            int32 close;
            SourceLocation start;
            SourceLocation argument;

            if (!csane_layout_is(tokens, in_index, STRLIT("["))
                || (!csane_layout_code(in_file, in_index)
                    && (in_file->preprocessor->token_roles == NULL
                        || in_file->preprocessor->token_roles[in_index]
                           != C_PP_TOKEN_MACRO_REPLACEMENT))) {
                break;
            }
            region = csane_regions_at_token(in_file->regions, in_index);
            if (region < 0
                || !csane_tokens_view(&view, tokens,
                                      csane_rt_tokens(in_file, region))) {
                break;
            }
            before = csane_tokens_previous(&view, in_index);
            if (before < view.range.first
                || (tokens->tokens[before].kind != TOKEN_IDENT
                    && !csane_layout_is(tokens, before, STRLIT(")"))
                    && !csane_layout_is(tokens, before, STRLIT("]")))) {
                break;
            }
            close = csane_tokens_match(&view, in_index);
            first = csane_tokens_next(&view, in_index);
            /* Macro continuation backslashes are source tokens, not indices. */
            if (in_file->preprocessor->token_roles != NULL
                && in_file->preprocessor->token_roles[in_index]
                   == C_PP_TOKEN_MACRO_REPLACEMENT) {
                while (first < close
                       && csane_layout_is(tokens, first, STRLIT("\\"))) {
                    first = csane_tokens_next(&view, first);
                }
            }
            if (close <= first || first >= view.range.end
                || csane_layout_is(tokens, first, STRLIT("["))
                || csane_layout_array_declaration(in_file, &view,
                                                  in_index, before)
                || csane_layout_array_declaration_list(in_file, &view,
                                                       in_index)) {
                break;
            }
            start = tokenization_token_location(tokens, in_index);
            argument = tokenization_token_location(tokens, first);
            if (start.line != argument.line) {
                csane_rule_issue(in_file, tokens->tokens[in_index].offset,
                                 STRLIT("FMT-ARRAY-FIRST"),
                                 "array index must begin on the opening "
                                 "bracket's line");
                break;
            }
            csane_layout_array_alignment_check(in_file, &view, first, close);
            if (csane_layout_code(in_file, in_index)) {
                csane_layout_array_wrap_check(in_file, &view, in_index, close);
            }
        } while (false);
        do {
            int32 _i37_equal = i;

            Tokenization *tokens = file->tokens;
            TokenRange statement = {0};
            CsaneTokenView view;
            CsaneArithParser parser = {0};
            int32 region;
            int32 before;
            int32 first;
            int32 semicolon;

            if (!csane_layout_code(file, _i37_equal)
                || !csane_layout_is(tokens, _i37_equal, STRLIT("="))) {
                goto _i37_done;
            }
            if (file->layout_statement_index != NULL
                && _i37_equal < ARRAY_LEN(file->layout_statement_index)) {
                int32 item = file->layout_statement_index[_i37_equal];
                if (item >= 0
                    && file->statements->items[item].kind
                        == CSANE_STMT_SIMPLE) {
                    statement = file->statements->items[item].tokens;
                }
            }
            if (statement.end == 0 && file->layout_top_index != NULL
                && _i37_equal < ARRAY_LEN(file->layout_top_index)) {
                int32 item = file->layout_top_index[_i37_equal];
                if (item >= 0
                    && file->toplevel->items[item].kind
                        == CSANE_TOP_DECLARATION) {
                    statement = file->toplevel->items[item].header;
                }
            }
            region = csane_regions_at_token(file->regions, _i37_equal);
            if (statement.end <= _i37_equal || region < 0) {
                goto _i37_done;
            }
            if (statement.first < csane_rt_tokens(file, region).first
                || statement.end > csane_rt_tokens(file, region).end
                || !csane_tokens_view(&view, tokens, statement)) {
                goto _i37_done;
            }
            before = csane_tokens_previous(&view, _i37_equal);
            if (before < view.range.first
                || tokens->tokens[before].kind != TOKEN_IDENT
                || csane_tokens_top_level(&view, view.range.first - 1,
                                          STRLIT("=")) != _i37_equal) {
                goto _i37_done;
            }
            first = csane_tokens_next(&view, _i37_equal);
            semicolon = csane_tokens_previous(&view, view.range.end);
            if (first >= semicolon
                || !csane_layout_is(tokens, semicolon, STRLIT(";"))) {
                goto _i37_done;
            }
            /* Comma-separated declarations and compound/conditional expressions
             * are intentionally excluded from this first, narrow rule. */
            parser.view = view;
            parser.current = first;
            parser.end = semicolon;
            if (csane_layout_arith_expression(&parser, 1, 0) >= 0
                && parser.current == parser.end) {
                for (int32 nested_i =
                    0; nested_i < ARRAY_LEN(parser.findings); nested_i += 1) {
                    CsaneArithFinding *finding = &parser.findings[nested_i];
                    if (finding->alignment) {
                        csane_rule_issue(file,
                                         tokens->tokens[finding->token].offset,
                                         STRLIT("FMT-ARITH-ALIGN"),
                                         "arithmetic operator must align with "
                                         "its precedence group");
                    } else {
                        csane_rule_issue(file,
                                         tokens->tokens[finding->token].offset,
                                         STRLIT("FMT-ARITH-WRAP"),
                                         "break before arithmetic operator, "
                                         "not after it");
                    }
                }
            }
            ARRAY_FREE(parser.findings);
            goto _i37_done;
            _i37_done: ;
        } while (false);
        do {
            int32 _i36_index = i;

            Token *token = &file->tokens->tokens[_i36_index];
            if (!csane_layout_code(file, _i36_index)) {
                goto _i36_done;
            }
            if (TOKEN_IS(token, ",")) {
                do {
                    Tokenization *tokens = file->tokens;
                    Token *comma = &tokens->tokens[_i36_index];
                    int32 region = csane_regions_at_token(file->regions,
                                                          _i36_index);
                    CsaneTokenView view;
                    int32 next;
                    int32 after;
                    if (region < 0
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    next = csane_tokens_next(&view, _i36_index);
                    if (next >= view.range.end
                        || TOKEN_IS(&tokens->tokens[next], ")")
                        || TOKEN_IS(&tokens->tokens[next], "]")
                        || TOKEN_IS(&tokens->tokens[next], "}")) {
                        break;
                    }
                    after = comma->offset + comma->len;
                    if (after >= tokens->text_len
                        || (tokens->text[after] != ' '
                            && tokens->text[after] != '\t'
                            && tokens->text[after] != '\r'
                            && tokens->text[after] != '\n')) {
                        csane_rule_issue(file, comma->offset,
                                         STRLIT("FMT-COMMA-SPACING"),
                                         "whitespace required after comma");
                    }
                } while (false);
            } else if (TOKEN_IS(token, ".") || TOKEN_IS(token, "->")) {
                do {
                    Tokenization *tokens = file->tokens;
                    Token *op = &tokens->tokens[_i36_index];
                    int32 region = csane_regions_at_token(file->regions,
                                                          _i36_index);
                    CsaneTokenView view;
                    int32 before;
                    int32 after;
                    bool designator;
                    bool bad;
                    if (region < 0
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    before = csane_tokens_previous(&view, _i36_index);
                    after = csane_tokens_next(&view, _i36_index);
                    if (after >= view.range.end) {
                        break;
                    }
                    designator = TOKEN_IS(op, ".")
                                 && before >= view.range.first
                                 && (TOKEN_IS(&tokens->tokens[before], "{")
                                     || TOKEN_IS(&tokens->tokens[before], ",")
                                     || TOKEN_IS(&tokens->tokens[before], "="));
                    bad = csane_layout_horizontal_gap(file, op,
                                                      &tokens->tokens[after]);
                    if (!designator && before >= view.range.first) {
                        Token *prior = &tokens->tokens[before];
                        bad = bad || csane_layout_horizontal_gap(file,
                                                                 prior, op);
                    }
                    if (bad) {
                        csane_rule_issue(file, op->offset,
                                         STRLIT("FMT-MEMBER-SPACING"),
                                         "no whitespace around member access");
                    }
                } while (false);
            } else if (TOKEN_IS(token, "!") || TOKEN_IS(token, "~")
                       || TOKEN_IS(token, "+") || TOKEN_IS(token, "-")
                       || TOKEN_IS(token, "&") || TOKEN_IS(token, "*")
                       || TOKEN_IS(token, "++") || TOKEN_IS(token, "--")) {
                do {
                    Tokenization *tokens = file->tokens;
                    Token *op = &tokens->tokens[_i36_index];
                    int32 region = csane_regions_at_token(file->regions,
                                                          _i36_index);
                    CsaneTokenView view;
                    int32 before;
                    int32 after;
                    if (region < 0
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    before = csane_tokens_previous(&view, _i36_index);
                    after = csane_tokens_next(&view, _i36_index);
                    if (after >= view.range.end) {
                        break;
                    }
                    if (csane_layout_prefix_unary(file, &view, _i36_index)) {
                        if (!csane_layout_adjacent(op,
                                                   &tokens->tokens[after])) {
                            csane_rule_issue(file, op->offset,
                                             STRLIT("FMT-UNARY-SPACING"),
                                             "no whitespace after "
                                             "unary operator");
                        }
                        break;
                    }
                    if ((TOKEN_IS(op, "++") || TOKEN_IS(op, "--"))
                        && before >= view.range.first
                        && csane_layout_operand_end(&tokens->tokens[before])
                        && !csane_layout_adjacent(&tokens->tokens[before],
                                                  op)) {
                        csane_rule_issue(file, op->offset,
                                         STRLIT("FMT-UNARY-SPACING"),
                                         "no whitespace before "
                                         "postfix operator");
                    }
                } while (false);
            }
            if (TOKEN_IS(token, ")")) {
                do {
                    int32 in_close = _i36_index;

                    Tokenization *tokens = file->tokens;
                    int32 region = csane_regions_at_token(file->regions,
                                                          in_close);
                    CsaneTokenView view;
                    int32 after;
                    if (region < 0
                        || csane_layout_function_header_close(file, in_close)
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))
                        || !csane_layout_cast(file, &view, in_close)) {
                        break;
                    }
                    after = csane_tokens_next(&view, in_close);
                    if (after < view.range.end
                        && csane_layout_operand_start(&tokens->tokens[after])
                        && !csane_layout_adjacent(&tokens->tokens[in_close],
                                                  &tokens->tokens[after])) {
                        csane_rule_issue(file, tokens->tokens[in_close].offset,
                                         STRLIT("FMT-CAST-SPACING"),
                                         "no whitespace after cast");
                    }
                } while (false);
            }
            if (TOKEN_IS(token, "*")) {
                do {
                    enum CsaneLayoutOperator in_kind = kind;

                    Tokenization *tokens = file->tokens;
                    Token *star = &tokens->tokens[_i36_index];
                    int32 region;
                    CsaneTokenView view;
                    int32 before;
                    int32 after;
                    bool bad = false;
                    if (csane_layout_return_pointer(file, _i36_index)) {
                        break;
                    }
                    region = csane_regions_at_token(file->regions, _i36_index);
                    if (region < 0
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    if (csane_layout_in_cast(file, &view, _i36_index)) {
                        break;
                    }
                    if (in_kind != CSANE_LAYOUT_DECLARATOR
                        && !csane_layout_function_pointer(&view, _i36_index)) {
                        break;
                    }
                    before = csane_tokens_previous(&view, _i36_index);
                    after = csane_tokens_next(&view, _i36_index);
                    if (before >= view.range.first
                        && !TOKEN_IS(&tokens->tokens[before], "(")
                        && !TOKEN_IS(&tokens->tokens[before], "*")
                        && csane_layout_adjacent(&tokens->tokens[before],
                                                 star)) {
                        bad = true;
                    }
                    if (after < view.range.end
                        && tokens->tokens[after].kind == TOKEN_IDENT
                        && !csane_layout_qualifier(&tokens->tokens[after])
                        && !csane_layout_adjacent(star,
                                                  &tokens->tokens[after])) {
                        bad = true;
                    }
                    if (bad) {
                        csane_rule_issue(file, star->offset,
                                         STRLIT("FMT-POINTER-SPACING"),
                                         "use `type *name` pointer spacing");
                    }
                } while (false);
            }
            if (TOKEN_IS(token, "++") || TOKEN_IS(token, "--")) {
                int32 region = csane_regions_at_token(file->regions,
                                                      _i36_index);
                CsaneTokenView view;
                bool allowed;

                int32 index = _i36_index;
                allowed = region >= 0
                          && csane_tokens_view(&view, file->tokens,
                                               csane_rt_tokens(file, region))
                          && csane_layout_inside_brackets(&view,
                                                          view.range.first,
                                                          index);
                if (!allowed && region >= 0
                    && csane_tokens_view(&view, file->tokens,
                                         csane_rt_tokens(file, region))) {
                    int32 before = csane_tokens_previous(&view, _i36_index);
                    int32 after = csane_tokens_next(&view, _i36_index);

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
                               && file->tokens->tokens[before].kind
                                   == TOKEN_IDENT
                               && TOKEN_IS(&file->tokens->tokens[after], ";")) {
                        Token *ts = file->tokens->tokens;
                        int32 dereference = csane_tokens_previous(&view,
                                                                  before);
                        int32 equal = csane_tokens_previous(&view, dereference);

                        while (equal >= view.range.first
                               && csane_layout_cast(file, &view, equal)) {
                            int32 open = csane_tokens_match(&view, equal);
                            equal = csane_tokens_previous(&view, open);
                        }
                        allowed = dereference >= view.range.first
                                  && equal >= view.range.first
                                  && TOKEN_IS(&ts[dereference], "*")
                                  && TOKEN_IS(&ts[equal], "=");
                    }
                }
                if (!allowed) {
                    csane_rule_issue(file, token->offset,
                                     STRLIT("STYLE-INCDEC"),
                                     "prefer += 1 or -= 1 instead of ++ or --");
                }
            }
            if (TOKEN_IS(token, "{")) {
                do {
                    int32 in_open = _i36_index;

                    Tokenization *tokens = file->tokens;
                    int32 region = csane_regions_at_token(file->regions,
                                                          in_open);
                    CsaneTokenView view;
                    int32 equal;
                    int32 close;
                    int32 last;
                    int32 tok_i;
                    int32 match;
                    int32 elements;
                    bool array;
                    SourceLocation open_loc;
                    SourceLocation close_loc;
                    if (region < 0
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    equal = csane_tokens_previous(&view, in_open);
                    if (equal < view.range.first
                        || !TOKEN_IS(&tokens->tokens[equal], "=")) {
                        break;
                    }
                    close = csane_tokens_match(&view, in_open);
                    if (close < 0
                        || csane_layout_zero_initializer(tokens, &view,
                                                         in_open, close)) {
                        break;
                    }
                    last = csane_tokens_previous(&view, close);
                    if (last <= in_open
                        || TOKEN_IS(&tokens->tokens[last], ",")) {
                        break;
                    }
                    array = csane_layout_array_initializer(&view, equal);
                    elements = 1;
                    if (array) {
                        tok_i = csane_tokens_next(&view, in_open);
                        while (tok_i < close) {
                            if (TOKEN_IS(&tokens->tokens[tok_i], ",")) {
                                elements += 1;
                            } else if (TOKEN_IS(&tokens->tokens[tok_i],
                                                "(")
                                       || TOKEN_IS(&tokens->tokens[tok_i], "[")
                                       || TOKEN_IS(&tokens->tokens[tok_i],
                                                   "{")) {
                                match = csane_tokens_match(&view, tok_i);
                                if (match > tok_i && match < close) {
                                    tok_i = match;
                                }
                            }
                            tok_i = csane_tokens_next(&view, tok_i);
                        }
                    }
                    open_loc = tokenization_token_location(tokens, in_open);
                    close_loc = tokenization_token_location(tokens, close);
                    if ((array && elements >= 4)
                        || (!array && open_loc.line != close_loc.line)) {
                        csane_rule_issue(file, tokens->tokens[last].offset,
                                         STRLIT("FMT-TRAILING-COMMA"),
                                         "initializer requires "
                                         "a trailing comma");
                    }
                } while (false);
                do {
                    int32 in_open = _i36_index;

                    Tokenization *tokens = file->tokens;
                    int32 region = csane_regions_at_token(file->regions,
                                                          in_open);
                    CsaneTokenView view;
                    int32 close;

                    if (region < 0 || !csane_layout_code(file, in_open)
                        || file->regions->items[region].kind
                            != CSANE_REGION_CODE
                        || !csane_tokens_view(&view, tokens,
                                              csane_rt_tokens(file, region))) {
                        break;
                    }
                    close = csane_tokens_match(&view, in_open);
                    if (close < 0
                        || csane_layout_zero_initializer(tokens, &view,
                                                         in_open, close)) {
                        break;
                    }
                    if (csane_layout_struct_designator(&view, in_open, close)) {
                        int32 first = csane_tokens_next(&view, in_open);
                        int32 last = csane_tokens_previous(&view, close);
                        SourceLocation start =
                            tokenization_token_location(tokens, in_open);
                        SourceLocation end =
                            tokenization_token_location(tokens, close);
                        SourceLocation member =
                            tokenization_token_location(tokens, first);
                        SourceLocation previous =
                            tokenization_token_location(tokens, last);

                        if (start.line == end.line) {
                            CsaneTokenView members;
                            TokenRange content = {in_open + 1, close};

                            if (csane_tokens_subview(&members, &view,
                                                     content)) {
                                int32 comma =
                                    csane_tokens_top_level(&members, in_open,
                                                           STRLIT(","));
                                if (comma >= 0
                                    && (comma == close
                                        || csane_tokens_next(&members, comma)
                                               == close)) {
                                    break;
                                }
                            }
                        }
                        if (start.line == member.line
                            || end.line == previous.line) {
                            csane_rule_issue(file,
                                             tokens->tokens[in_open].offset,
                                             STRLIT("FMT-STRUCT-INITIALIZER"),
                                             "put designated struct "
                                             "initializer members and "
                                             "braces on separate lines");
                        }
                    }
                } while (false);
            }
            _i36_done: ;
        } while (false);
        if (kind == CSANE_LAYOUT_MULTIPLY) {
            if (csane_layout_spacing(file, i, true)) {
                csane_rule_issue(file, outer_op->offset, STRLIT("FMT-MULTIPLY"),
                                 "no spaces around multiplication");
            }
        } else if (kind == CSANE_LAYOUT_BINARY) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, outer_op->offset,
                                 STRLIT("FMT-BINARY-SPACING"),
                                 "spaces required around binary operator");
            }
        } else if (kind == CSANE_LAYOUT_ASSIGN) {
            if (csane_layout_spacing(file, i, false)) {
                csane_rule_issue(file, outer_op->offset,
                                 STRLIT("FMT-ASSIGN-SPACING"),
                                 "assignment requires surrounding whitespace");
            }
        }
    }
}

/* Unlike operator spacing, the placement of a continuation-line '=' must
 * also be checked in unsupported expressions, as long as it is C source and
 * not a preprocessor directive or macro replacement. */
void
csane_layout_assignment_check(CsaneFile *file) {
    if (!file->is_ryu && !csane_layout_generated(file->path, file->path_len)) {
        Tokenization *tokens = file->tokens;
        char *text = tokens->text;
        for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
            Token *token = &tokens->tokens[i];
            int32 start;
            int32 part_index;
            if (!TOKEN_IS(token, "=")
                || !csane_layout_code(file, i)) {
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
                csane_rule_issue(file, token->offset,
                                 STRLIT("FMT-ASSIGN-EQ"),
                                 "assignment = must end "
                                 "the preceding line");
            }
        }
    }
}

#endif /* CSANECHECK_LAYOUT_C */
