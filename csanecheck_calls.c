// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CALLS_C)
#define CSANECHECK_CALLS_C

#include "cbase.h"
#include "csanecheck.h"

/* All offsets refer to the original tokenization. Neither argument text nor
 * whitespace is reconstructed, and unparsed regions produce no AST claims. */
static bool
csane_calls_code(CsaneFile *file, int32 index) {
    if (index < 0 || index >= file->tokens->token_count) {
        return false;
    }
    return !file->preprocessor->token_roles
           || file->preprocessor->token_roles[index] == C_PP_TOKEN_CODE;
}

static bool
csane_calls_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static SourceLocation
csane_calls_location(CsaneFile *file, int32 index) {
    return tokenization_source_location(file->tokens,
                                        file->tokens->tokens[index].offset);
}

static int32
csane_calls_begin(CsaneFile *file, TokenRange range) {
    int32 index = token_range_first_significant(file->tokens, range);
    return index < range.end ? index : -1;
}

static int32
csane_calls_arg(CsaneFile *file, int32 index) {
    CAst *ast = file->ast;
    if (index < 0 || index >= ast->argument_count) {
        return -1;
    }
    int32 expression = ast->arguments[index].expression;
    if (expression < 0 || expression >= ast->expression_count) {
        return -1;
    }
    return csane_calls_begin(file, ast->expressions[expression].tokens);
}

static int32
csane_calls_arg_end(CsaneFile *file, int32 argument) {
    CAst *ast = file->ast;
    int32 expression = ast->arguments[argument].expression;
    TokenRange range = ast->expressions[expression].tokens;
    return token_range_last_significant(file->tokens, range);
}

static bool
csane_calls_line_break(CsaneFile *file, int32 before, int32 after) {
    return csane_calls_location(file, before).line
           < csane_calls_location(file, after).line;
}

/* String tokens, not arbitrary source bytes, identify the format anchor.
 * Percent signs in comments and character literals do not qualify. */
static bool
csane_calls_percent(CsaneFile *file, int32 arg) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    int32 expression = ast->arguments[arg].expression;
    TokenRange range = ast->expressions[expression].tokens;
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &tokens->tokens[i];
        int32 quote;
        if (token->kind != TOKEN_LITERAL) {
            continue;
        }
        quote = literal_quote_index(token->text, token->len, 0);
        if (quote < 0 || token->text[quote] != '"') {
            continue;
        }
        for (int32 j = quote + 1; j < token->len; j += 1) {
            if (token->text[j] == '\\') {
                j += 1;
            } else if (token->text[j] == '%') {
                return true;
            }
        }
    }
    return false;
}

static int32
csane_calls_identifier(CsaneFile *file, int32 argument) {
    CAst *ast = file->ast;
    int32 expression = ast->arguments[argument].expression;
    CExpression *node = &ast->expressions[expression];
    if (node->kind != C_EXPR_IDENTIFIER) {
        return -1;
    }
    return csane_calls_begin(file, node->tokens);
}

static bool
csane_calls_related(Token *a, Token *b) {
    int32 prefix;
    if (b->len == a->len + 4
        && !memcmp64(b->text, a->text, a->len)
        && !memcmp64(b->text + a->len, "_len", 4)) {
        return true;
    }
    if (a->len >= 1 && a->text[a->len - 1] == 'x') {
        prefix = a->len - 1;
        if ((prefix == 0 || a->text[prefix - 1] == '_')
            && b->len == prefix + 1
            && !memcmp64(a->text, b->text, prefix)
            && b->text[prefix] == 'y') {
            return true;
        }
    }
    if (a->len >= 5 && !memcmp64(a->text + a->len - 5, "width", 5)) {
        prefix = a->len - 5;
        if ((prefix == 0 || a->text[prefix - 1] == '_')
            && b->len == prefix + 6
            && !memcmp64(a->text, b->text, prefix)
            && !memcmp64(b->text + prefix, "height", 6)) {
            return true;
        }
    }
    return false;
}

static int32
csane_calls_line_count(CsaneFile *file, CExpression *call, int32 line) {
    CAst *ast = file->ast;
    int32 count = 0;
    int32 argument = call->first_argument;
    for (int32 i = 0; i < call->argument_count && argument >= 0; i += 1) {
        int32 token = csane_calls_arg(file, argument);
        if (token >= 0 && csane_calls_location(file, token).line == line) {
            count += 1;
        }
        argument = ast->arguments[argument].next;
    }
    return count;
}

static void
csane_calls_literals(CsaneFile *file, int32 argument) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    int32 expr = ast->arguments[argument].expression;
    TokenRange range = ast->expressions[expr].tokens;
    int32 first = -1;
    int32 last = -1;
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &tokens->tokens[i];
        int32 quote = token->kind == TOKEN_LITERAL
                      ? literal_quote_index(token->text, token->len, 0) : -1;
        if (quote >= 0 && token->text[quote] == '"') {
            if (first >= 0 && last >= 0
                && csane_calls_line_break(file, last, i)
                && csane_calls_location(file, first).column
                   != csane_calls_location(file, i).column) {
                csane_rule_issue(file, token->offset, "FMT-LITERAL-ALIGN",
                                 "call string literal is not aligned "
                                 "with first literal");
                return;
            }
            if (first < 0) {
                first = i;
            }
            last = i;
        } else if (token->kind == TOKEN_COMMENT
                   || !token_is_trivia(token)) {
            first = -1;
            last = -1;
        }
    }
    return;
}

static void
csane_calls_one(CsaneFile *file, CExpression *call) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    int32 open = call->operator_token;
    int32 first_arg = call->first_argument;
    int32 first;
    int32 format = -1;
    int32 anchor;
    int32 argument;
    int32 previous = -1;
    int32 format_line = -1;
    bool on_format = false;
    bool off_format = false;
    if (!csane_calls_code(file, open) || first_arg < 0) {
        return;
    }
    first = csane_calls_arg(file, first_arg);
    if (first < 0) {
        return;
    }
    /* Only whitespace before a newline qualifies as a break directly
     * after '('. Comments between '(' and its argument are exempt. */
    int32 from = tokens->tokens[open].offset + tokens->tokens[open].len;
    int32 to = tokens->tokens[first].offset;
    bool newline = false;
    bool plain = true;
    for (int32 j = from; j < to; j += 1) {
        char c = tokens->text[j];
        if (c == '\n') {
            newline = true;
        } else if (c != '\r' && c != ' ' && c != '\t') {
            plain = false;
            break;
        }
    }
    if (newline && plain) {
        csane_rule_issue(file, tokens->tokens[open].offset,
                         "FMT-CALL-FIRST",
                         "call breaks before the first argument");
    }
    if (!csane_calls_line_break(file, open,
                                token_range_last_significant(tokens,
                                                             call->tokens))) {
        return;
    }
    argument = first_arg;
    for (int32 i = 0; i < call->argument_count && argument >= 0; i += 1) {
        if (format < 0 && csane_calls_percent(file, argument)) {
            format = argument;
        }
        csane_calls_literals(file, argument);
        argument = ast->arguments[argument].next;
    }
    anchor = format >= 0 ? format : first_arg;
    if (format >= 0) {
        int32 end_token = csane_calls_arg_end(file, format);
        format_line = csane_calls_location(file, end_token).line;
    }
    argument = anchor;
    previous = -1;
    for (int32 i = 0; i < call->argument_count && argument >= 0; i += 1) {
        int32 current = csane_calls_arg(file, argument);
        int32 next = ast->arguments[argument].next;
        if (current < 0) {
            break;
        }
        int32 anchor_token = csane_calls_arg(file, anchor);
        if (previous >= 0
            && csane_calls_line_break(file, previous, current)
            && csane_calls_location(file, current).column
               != csane_calls_location(file, anchor_token).column) {
            csane_rule_issue(file, tokens->tokens[current].offset,
                             "FMT-ARG-ALIGN",
                             "call argument is not aligned "
                             "with its alignment anchor");
            /* One alignment diagnostic per call, as in the legacy checker. */
            break;
        }
        previous = csane_calls_arg_end(file, argument);
        argument = next;
    }
    if (format >= 0) {
        argument = ast->arguments[format].next;
        while (argument >= 0) {
            int32 current = csane_calls_arg(file, argument);
            if (current >= 0) {
                if (csane_calls_location(file, current).line == format_line) {
                    on_format = true;
                } else {
                    off_format = true;
                }
            }
            argument = ast->arguments[argument].next;
        }
        if (on_format && off_format) {
            int32 format_token = csane_calls_arg(file, format);
            csane_rule_issue(file, tokens->tokens[format_token].offset,
                             "FMT-FORMAT-LINE",
                             "format-string line must contain all "
                             "format arguments or none");
        }
    }
    if (call->argument_count < 3) {
        return;
    }
    argument = first_arg;
    while (argument >= 0) {
        int32 next = ast->arguments[argument].next;
        int32 a = csane_calls_identifier(file, argument);
        int32 b = next >= 0 ? csane_calls_identifier(file, next) : -1;
        if (a >= 0 && b >= 0
            && csane_calls_related(&tokens->tokens[a], &tokens->tokens[b])) {
            int32 a_line = csane_calls_location(file, a).line;
            int32 b_line = csane_calls_location(file, b).line;
            if (a_line != b_line
                && (csane_calls_line_count(file, call, a_line) != 1
                    || csane_calls_line_count(file, call, b_line) != 1)) {
                csane_rule_issue(file, tokens->tokens[b].offset,
                                 "FMT-RELATED-ARGS",
                                 "related arguments must be the only "
                                 "arguments on their lines");
                break;
            }
        }
        argument = next;
    }
    return;
}

static int32
csane_calls_name(CsaneFile *file, int32 root) {
    CAst *ast = file->ast;
    for (int32 i = 0; i < 64 && root >= 0; i += 1) {
        CDeclaratorNode *node = &ast->declarators[root];
        if (node->name_token >= 0) {
            return node->name_token;
        }
        root = node->inner;
    }
    return -1;
}

static void
csane_calls_headers(CsaneFile *file) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    for (int32 d = 0; d < ast->declaration_count; d += 1) {
        CDeclaration *decl = &ast->declarations[d];
        int32 root = decl->first_declarator;
        int32 name;
        int32 parameter;
        int32 first = -1;
        int32 previous_end = -1;
        if (decl->kind != C_DECLARATION_NORMAL || root < 0
            || ast->declarators[root].kind != C_DECL_FUNCTION) {
            continue;
        }
        name = csane_calls_name(file, root);
        if (name < 0 || !csane_calls_code(file, name)
            || csane_calls_location(file, name).column != 0) {
            continue;
        }
        parameter = ast->declarators[root].first_parameter;
        for (int32 i = 0; i < ast->declarators[root].parameter_count
             && parameter >= 0; i += 1) {
            TokenRange param_range = ast->declarations[parameter].tokens;
            int32 current = csane_calls_begin(file, param_range);
            if (current < 0) {
                break;
            }
            if (first < 0) {
                first = current;
            } else if (previous_end >= 0
                       && csane_calls_line_break(file, previous_end, current)
                       && csane_calls_location(file, first).column
                          != csane_calls_location(file, current).column) {
                csane_rule_issue(file, tokens->tokens[current].offset,
                                 "FMT-FUNC-ALIGN",
                                 "function header parameter is not aligned "
                                 "with first parameter");
                break;
            }
            previous_end = token_range_last_significant(tokens, param_range);
            parameter = ast->declarations[parameter].next;
        }
    }
    return;
}

void
csane_calls_check(CsaneFile *file) {
    CAst *ast = file->ast;
    if (csane_calls_generated(file->path)) {
        return;
    }
    csane_calls_headers(file);
    for (int32 i = 0; i < ast->expression_count; i += 1) {
        if (ast->expressions[i].kind == C_EXPR_CALL) {
            csane_calls_one(file, &ast->expressions[i]);
        }
    }
    return;
}

#endif /* CSANECHECK_CALLS_C */
