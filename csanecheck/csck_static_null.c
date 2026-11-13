// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATIC_NULL_C)
#define CSANECHECK_STATIC_NULL_C

#include "cbase.h"
#include "csck.h"

/* This rule proves only direct, local calls with an address-of identifier.
 * An unclassified reference invalidates the proof, not just that call. */
static bool
csane_static_null_same(Token *left, Token *right) {
    return left->kind == TOKEN_IDENT && right->kind == TOKEN_IDENT
           && left->len == right->len
           && !memcmp64(left->text, right->text, left->len);
}

static bool
csane_static_null_code(CsaneFile *file, int32 index) {
    int32 region = csane_regions_at_token(file->regions, index);

    return region >= 0
           && file->regions->items[region].kind == CSANE_REGION_CODE
           && file->regions->items[region].complete;
}

/* Return the zero-based position of a simple pointer parameter, or -1.
 * Complex declarators are deliberately not accepted as proof. */
static int32
csane_static_null_parameter(CsaneTokenView *view, CsaneTopItem *item,
                            Token *name) {
    Tokenization *tokens = view->tokens;
    TokenRange parameters[64];
    int32 open = csane_tokens_next(view, item->name_token);
    int32 count;

    if (open >= item->header.end
        || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return -1;
    }
    count = csane_tokens_arguments(view, open, parameters, LENGTH(parameters));
    if (count < 0) {
        return -1;
    }
    for (int32 i = 0; i < count; i += 1) {
        CsaneTokenView parameter;
        int32 first;
        int32 last;
        bool pointer = false;
        bool complex = false;

        if (!csane_tokens_subview(&parameter, view, parameters[i])) {
            return -1;
        }
        last = csane_tokens_previous(&parameter, parameter.range.end);
        if (last < parameter.range.first
            || !csane_static_null_same(&tokens->tokens[last], name)) {
            continue;
        }
        first = parameter.range.first - 1;
        for (int32 j = csane_tokens_next(&parameter, first); j < last;
             j = csane_tokens_next(&parameter, j)) {
            if (TOKEN_IS(&tokens->tokens[j], "*")) {
                pointer = true;
            } else if (TOKEN_IS(&tokens->tokens[j], "(")
                       || TOKEN_IS(&tokens->tokens[j], ")")
                       || TOKEN_IS(&tokens->tokens[j], "[")) {
                complex = true;
            }
        }
        return pointer && !complex ? i : -1;
    }
    return -1;
}

/* Recognize precisely 'pointer == NULL', 'NULL == pointer', or '!pointer'. */
static int32
csane_static_null_guard(CsaneTokenView *view, int32 open) {
    Tokenization *tokens = view->tokens;
    TokenRange condition;
    int32 first;
    int32 second;
    int32 third;

    if (!csane_tokens_group(view, open, &condition, NULL)) {
        return -1;
    }
    condition = csane_tokens_unwrap(view, condition);
    first = token_range_next_significant(tokens, condition,
                                         condition.first - 1);
    second = token_range_next_significant(tokens, condition, first);
    third = token_range_next_significant(tokens, condition, second);
    if (first >= condition.end) {
        return -1;
    }
    if (TOKEN_IS(&tokens->tokens[first], "!")
        && second < condition.end
        && tokens->tokens[second].kind == TOKEN_IDENT
        && third >= condition.end) {
        return second;
    }
    if (third >= condition.end
        || token_range_next_significant(tokens, condition, third)
           < condition.end
        || !TOKEN_IS(&tokens->tokens[second], "==")) {
        return -1;
    }
    if (tokens->tokens[first].kind == TOKEN_IDENT
        && TOKEN_IS(&tokens->tokens[third], "NULL")) {
        return first;
    }
    if (TOKEN_IS(&tokens->tokens[first], "NULL")
        && tokens->tokens[third].kind == TOKEN_IDENT) {
        return third;
    }
    return -1;
}

/* Only a no-else, immediate 'return;' counts as the removable guard. */
static int32
csane_static_null_return(CsaneTokenView *view, int32 close) {
    Tokenization *tokens = view->tokens;
    int32 first = csane_tokens_next(view, close);
    int32 end;
    int32 after;

    if (first >= view->range.end) {
        return -1;
    }
    if (TOKEN_IS(&tokens->tokens[first], "{")) {
        int32 return_token = csane_tokens_next(view, first);
        int32 semicolon = csane_tokens_next(view, return_token);

        end = csane_tokens_next(view, semicolon);
        if (end >= view->range.end
            || !TOKEN_IS(&tokens->tokens[return_token], "return")
            || !TOKEN_IS(&tokens->tokens[semicolon], ";")
            || !TOKEN_IS(&tokens->tokens[end], "}")) {
            return -1;
        }
    } else {
        end = csane_tokens_next(view, first);
        if (end >= view->range.end
            || !TOKEN_IS(&tokens->tokens[first], "return")
            || !TOKEN_IS(&tokens->tokens[end], ";")) {
            return -1;
        }
    }
    after = csane_tokens_next(view, end);
    if (after < view->range.end && TOKEN_IS(&tokens->tokens[after], "else")) {
        return -1;
    }
    return end;
}

/* Unknown uses, address escapes, or calls with non-address arguments suppress
 * the diagnostic. This also rejects names in macro replacements. */
static bool
csane_static_null_all_calls(CsaneFile *file, CsaneTopItem *item,
                            int32 parameter) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    Token *name = &tokens->tokens[item->name_token];
    int32 calls = 0;

    for (int32 i = 0; i < ARRAY_LEN(tokens->tokens); i += 1) {
        int32 previous;
        int32 next;
        TokenRange args[64];
        int32 count;
        bool declaration = false;
        bool member = false;

        if (i == item->name_token
            || !csane_static_null_same(&tokens->tokens[i], name)) {
            continue;
        }
        for (int32 j = 0; j < ARRAY_LEN(file->toplevel->items); j += 1) {
            CsaneTopItem *other = &file->toplevel->items[j];

            if (other->name_token == i
                && other->kind == CSANE_TOP_FUNCTION_DECLARATION) {
                declaration = true;
                break;
            }
            if (other != item && other->name_token == i
                && other->kind == CSANE_TOP_FUNCTION_DEFINITION) {
                return false;
            }
            if (other->kind == CSANE_TOP_TYPE_DECLARATION
                && i >= other->tokens.first && i < other->tokens.end) {
                int32 depth = 0;

                for (int32 k = other->tokens.first; k < i; k += 1) {
                    if (TOKEN_IS(&tokens->tokens[k], "{")) {
                        depth += 1;
                    } else if (TOKEN_IS(&tokens->tokens[k], "}")) {
                        depth -= 1;
                    }
                }
                if (depth > 0) {
                    member = true;
                    break;
                }
            }
        }
        if (declaration || member) {
            continue;
        }
        previous = tokenization_previous_significant(tokens, i);
        next = tokenization_next_significant(tokens, i);
        if (previous >= 0
            && (TOKEN_IS(&tokens->tokens[previous], ".")
                || TOKEN_IS(&tokens->tokens[previous], "->")
                || TOKEN_IS(&tokens->tokens[previous], "struct")
                || TOKEN_IS(&tokens->tokens[previous], "union")
                || TOKEN_IS(&tokens->tokens[previous], "enum")
                || TOKEN_IS(&tokens->tokens[previous], "goto"))) {
            continue;
        }
        if (!csane_static_null_code(file, i)
            || next >= ARRAY_LEN(tokens->tokens)
            || !TOKEN_IS(&tokens->tokens[next], "(")) {
            return false;
        }
        count = csane_tokens_arguments(&view, next, args, LENGTH(args));
        if (count < 0 || parameter >= count) {
            return false;
        }
        {
            TokenRange argument = csane_tokens_unwrap(&view, args[parameter]);
            int32 first = token_range_next_significant(tokens, argument,
                                                       argument.first - 1);
            int32 second = token_range_next_significant(tokens, argument,
                                                        first);

            if (first >= argument.end || second >= argument.end
                || !TOKEN_IS(&tokens->tokens[first], "&")
                || tokens->tokens[second].kind != TOKEN_IDENT
                || token_range_next_significant(tokens, argument, second)
                   < argument.end) {
                return false;
            }
        }
        calls += 1;
    }
    return calls > 0;
}

void
csane_static_null_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView full = csane_tokens_full(tokens);

    if (!ENDS_WITH(file->path, file->path_len, ".c") || file->is_ryu
        || !file->preprocessor_ok || file->include_graph == NULL) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        CsaneTokenView body;
        bool is_static = false;
        int32 cursor;

        if (item->kind != CSANE_TOP_FUNCTION_DEFINITION
            || item->name_token < 0
            || !csane_static_null_code(file, item->name_token)) {
            continue;
        }
        for (int32 j = item->header.first; j < item->name_token; j += 1) {
            if (TOKEN_IS(&tokens->tokens[j], "static")) {
                is_static = true;
                break;
            }
        }
        if (!is_static
            || !csane_tokens_subview(&body, &full, item->body)) {
            continue;
        }
        cursor = csane_tokens_next(&body, body.range.first);
        while (cursor < body.range.end
               && TOKEN_IS(&tokens->tokens[cursor], "if")) {
            Token *name = &tokens->tokens[item->name_token];
            Token *guarded_name;
            int32 open = csane_tokens_next(&body, cursor);
            int32 close;
            int32 parameter_name;
            int32 parameter;
            int32 guard_end;

            if (open >= body.range.end
                || !TOKEN_IS(&tokens->tokens[open], "(")) {
                break;
            }
            close = csane_tokens_match(&body, open);
            if (close < 0) {
                break;
            }
            parameter_name = csane_static_null_guard(&body, open);
            guard_end = csane_static_null_return(&body, close);
            if (parameter_name < 0 || guard_end < 0) {
                break;
            }
            guarded_name = &tokens->tokens[parameter_name];
            parameter = csane_static_null_parameter(&full, item, guarded_name);
            if (parameter >= 0
                && !csane_include_graph_foreign_reference(file->include_graph,
                                                          file->path,
                                                          name->text,
                                                          name->len)
                && csane_static_null_all_calls(file, item, parameter)) {
                char message[256];

                fmt_snprintf(message, SIZEOF(message),
                             "static function '%.*s': NULL guard for '%.*s' "
                             "is redundant; all direct calls pass &variable",
                             name->len, name->text,
                             tokens->tokens[parameter_name].len,
                             tokens->tokens[parameter_name].text);
                csane_rule_issue(file, tokens->tokens[cursor].offset,
                                 SL("STYLE-STATIC-NULL-GUARD"), message);
            }
            cursor = csane_tokens_next(&body, guard_end);
        }
    }
    return;
}

#endif /* CSANECHECK_STATIC_NULL_C */
