// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_WRAP_C)
#define CSANECHECK_WRAP_C

#include "cbase.h"
#include "csanecheck.h"

/* All ranges below refer to the original source. A layout candidate must
 * be a complete AST statement; a nested call is never a separate candidate.
 */
typedef struct CsaneWrapSpan {
    int32 start;
    int32 end;
    int32 start_line;
    int32 end_line;
    int32 indent;
    bool fits;
    bool wrapped;
} CsaneWrapSpan;

enum CsaneWrapKind {
    CSANE_WRAP_CALL = 1,
    CSANE_WRAP_ASSIGN,
};

static bool
csane_wrap_token_is(Token *token, char *word) {
    int32 len = strlen32(word);
    return token->len == len && !memcmp64(token->text, word, len);
}

static int32
csane_wrap_line_start(char *text, int32 offset) {
    while (offset > 0 && text[offset - 1] != '\n') {
        offset -= 1;
    }
    return offset;
}

static SourceLocation
csane_wrap_location(Tokenization *tokens, int32 index) {
    return tokenization_source_location(tokens, tokens->tokens[index].offset);
}

static bool
csane_wrap_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static int32
csane_wrap_unparen(CAst *ast, int32 index) {
    while (index >= 0 && index < ast->expression_count
           && ast->expressions[index].kind == C_EXPR_PAREN) {
        index = ast->expressions[index].left;
    }
    return index;
}

static int32
csane_wrap_call_expression(CAst *ast, int32 index) {
    index = csane_wrap_unparen(ast, index);
    if (index < 0 || index >= ast->expression_count) {
        return -1;
    }
    CExpression *expr = &ast->expressions[index];
    if (expr->kind != C_EXPR_CALL) {
        return -1;
    }
    int32 function = csane_wrap_unparen(ast, expr->left);
    if (function < 0 || function >= ast->expression_count
        || ast->expressions[function].kind != C_EXPR_IDENTIFIER) {
        return -1;
    }
    return token_range_first_significant(ast->tokenization,
                                         ast->expressions[function].tokens);
}

static int32
csane_wrap_assignment(CsaneFile *file, CStatement *stmt) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    int32 index = csane_wrap_unparen(ast, stmt->expression);
    if (stmt->kind == C_STMT_EXPRESSION && index >= 0
        && index < ast->expression_count) {
        CExpression *expr = &ast->expressions[index];
        int32 op = expr->operator_token;
        if (expr->kind == C_EXPR_ASSIGN && op >= 0
            && csane_wrap_token_is(&tokens->tokens[op], "=")) {
            return op;
        }
    }
    if (stmt->kind != C_STMT_DECLARATION || stmt->declaration < 0) {
        return -1;
    }
    CDeclaration *decl = &ast->declarations[stmt->declaration];
    if (decl->declarator_count != 1 || decl->first_declarator < 0) {
        return -1;
    }
    CDeclaratorNode *node = &ast->declarators[decl->first_declarator];
    if (token_range_is_empty(tokens, node->extra)) {
        return -1;
    }
    int32 right = token_range_first_significant(tokens, node->extra);
    TokenRange before = {0, right};
    int32 op = token_range_previous_significant(tokens, before, right);
    if (op >= 0 && csane_wrap_token_is(&tokens->tokens[op], "=")) {
        return op;
    }
    return -1;
}

static int32
csane_wrap_is_call(CsaneFile *file, CStatement *stmt) {
    CAst *ast = file->ast;
    int32 expression = stmt->expression;
    if (stmt->kind == C_STMT_DECLARATION && stmt->declaration >= 0) {
        CDeclaration *decl = &ast->declarations[stmt->declaration];
        if (decl->declarator_count != 1 || decl->first_declarator < 0) {
            return -1;
        }
        TokenRange range = ast->declarators[decl->first_declarator].extra;
        if (token_range_is_empty(file->tokens, range)) {
            return -1;
        }
        /* Initializers are parsed by the API rule, which runs first. The
         * root is the expression spanning the entire initializer range.
         */
        for (int32 i = ast->expression_count - 1; i >= 0; i -= 1) {
            TokenRange value = ast->expressions[i].tokens;
            if (value.first == range.first && value.end == range.end) {
                return csane_wrap_call_expression(ast, i);
            }
        }
        return -1;
    }
    if (stmt->kind != C_STMT_EXPRESSION && stmt->kind != C_STMT_RETURN) {
        return -1;
    }
    expression = csane_wrap_unparen(ast, expression);
    if (expression >= 0 && expression < ast->expression_count
        && ast->expressions[expression].kind == C_EXPR_ASSIGN) {
        CExpression *assign = &ast->expressions[expression];
        int32 op = assign->operator_token;
        if (op < 0 || !csane_wrap_token_is(&file->tokens->tokens[op], "=")) {
            return -1;
        }
        expression = assign->right;
    }
    return csane_wrap_call_expression(ast, expression);
}

/* Scan token kinds rather than treating comment markers inside string
 * literals as comments. Never suggest flattening macro continuations,
 * initializer blocks, multiple statements, or split string literals.
 */
static bool
csane_wrap_safe(CsaneFile *file, int32 first, int32 last) {
    Tokenization *tokens = file->tokens;
    int32 previous_string = -1;
    for (int32 i = first; i <= last; i += 1) {
        Token *token = &tokens->tokens[i];
        enum CPreprocessorTokenRole role = C_PP_TOKEN_CODE;
        if (file->preprocessor->token_roles != NULL) {
            role = file->preprocessor->token_roles[i];
        }
        if (role != C_PP_TOKEN_CODE || token->kind == TOKEN_COMMENT
            || csane_wrap_token_is(token, "{")
            || csane_wrap_token_is(token, "}")) {
            return false;
        }
        if (csane_wrap_token_is(token, ";") && i != last) {
            return false;
        }
        if (token->kind == TOKEN_LITERAL
            && literal_quote_index(token->text, token->len, 0) >= 0) {
            int32 quote = literal_quote_index(token->text, token->len, 0);
            if (token->text[quote] == '"') {
                if (previous_string >= 0) {
                    int32 previous = previous_string;
                    SourceLocation a = csane_wrap_location(tokens, previous);
                    SourceLocation b = csane_wrap_location(tokens, i);
                    if (a.line != b.line) {
                        return false;
                    }
                }
                previous_string = i;
            }
        } else if (!token_is_trivia(token)) {
            previous_string = -1;
        }
    }
    return true;
}

/* Compute the width of joining the original lines, preserving their actual
 * spelling. A break before a closing delimiter needs no added space.
 */
static int32
csane_wrap_flat_length(char *text, int32 start, int32 end) {
    int32 width = 0;
    for (int32 i = start; i < end;) {
        if (text[i] == '\\') {
            int32 j = i + 1;
            if (j < end && text[j] == '\r') {
                j += 1;
            }
            if (j < end && text[j] == '\n') {
                return -1;
            }
        }
        if (text[i] == '\n') {
            int32 j = i + 1;
            while (j < end && (text[j] == ' ' || text[j] == '\t'
                                || text[j] == '\r')) {
                j += 1;
            }
            if (j >= end || text[j] == '#' || text[j] == '\n') {
                return -1;
            }
            while (width > 0 && (text[i - 1] == ' ' || text[i - 1] == '\r'
                                 || text[i - 1] == '\t')) {
                /* Trailing whitespace was counted in the preceding run. */
                width -= 1;
                i -= 1;
            }
            width += text[j] == ')' || text[j] == ']' || text[j] == ','
                     || text[j] == ';' ? 0 : 1;
            i = j;
            continue;
        }
        width += 1;
        i += 1;
    }
    return width;
}

static bool
csane_wrap_span(CsaneFile *file, int32 statement,
                enum CsaneWrapKind kind, CsaneWrapSpan *span) {
    CAst *ast = file->ast;
    Tokenization *tokens = file->tokens;
    CStatement *stmt = &ast->statements[statement];
    int32 first = token_range_first_significant(tokens, stmt->tokens);
    int32 last = token_range_last_significant(tokens, stmt->tokens);
    int32 op = -1;
    if (first < 0 || last < first || last >= tokens->token_count
        || !csane_wrap_token_is(&tokens->tokens[last], ";")) {
        return false;
    }
    if (kind == CSANE_WRAP_CALL) {
        int32 name = csane_wrap_is_call(file, stmt);
        if (name < 0 || csane_wrap_location(tokens, name).line
            != csane_wrap_location(tokens, first).line
            || csane_wrap_token_is(&tokens->tokens[name], "error_impl")
            || csane_wrap_token_is(&tokens->tokens[name], "assert_error")) {
            return false;
        }
    }
    if (kind == CSANE_WRAP_ASSIGN) {
        op = csane_wrap_assignment(file, stmt);
        if (op < 0) {
            return false;
        }
    }
    char *text = tokens->text;
    int32 first_offset = tokens->tokens[first].offset;
    int32 line_start = csane_wrap_line_start(text, first_offset);
    if (line_start == first_offset) {
        return false;
    }
    for (int32 i = line_start; i < first_offset; i += 1) {
        if (text[i] != ' ') {
            return false;
        }
    }
    span->start = line_start;
    span->end = tokens->tokens[last].offset + tokens->tokens[last].len;
    span->indent = first_offset - line_start;
    span->start_line = tokenization_source_location(tokens, first_offset).line;
    int32 end_offset = tokens->tokens[last].offset;
    span->end_line = tokenization_source_location(tokens, end_offset).line;
    span->wrapped = span->end_line > span->start_line;
    if (kind == CSANE_WRAP_ASSIGN) {
        int32 after = tokens->tokens[op].offset + tokens->tokens[op].len;
        int32 j = after;
        while (j < tokens->text_len && (text[j] == ' ' || text[j] == '\r'
                                        || text[j] == '\t')) {
            j += 1;
        }
        span->wrapped = j < tokens->text_len && text[j] == '\n';
    }
    span->fits = false;
    if (!csane_wrap_safe(file, first, last)) {
        return true;
    }
    int32 width = csane_wrap_flat_length(text, span->start, span->end);
    span->fits = width >= 0 && (file->column_limit <= 0
                                || width <= file->column_limit);
    return true;
}

static bool
csane_wrap_comment_lines(CsaneFile *file, CsaneWrapSpan *a,
                         CsaneWrapSpan *b) {
    Tokenization *tokens = file->tokens;
    char *text = tokens->text;
    int32 position = a->end;
    while (position < b->start && text[position] != '\n') {
        position += 1;
    }
    if (position >= b->start) {
        return false;
    }
    position += 1;
    for (int32 line = a->end_line + 1; line < b->start_line;
         line += 1) {
        int32 end = position;
        bool found_comment = false;
        while (end < b->start && text[end] != '\n') {
            end += 1;
        }
        for (int32 i = 0; i < tokens->token_count; i += 1) {
            Token *token = &tokens->tokens[i];
            if (token->offset >= end
                || token->offset + token->len <= position) {
                continue;
            }
            if (token->kind == TOKEN_COMMENT) {
                found_comment = true;
            } else if (!token_is_trivia(token)) {
                return false;
            }
        }
        if (!found_comment) {
            return false;
        }
        position = end + (end < b->start);
    }
    return true;
}

static bool
csane_wrap_adjacent(CsaneFile *file, CsaneWrapSpan *a,
                    CsaneWrapSpan *b, enum CsaneWrapKind kind) {
    if (a->indent != b->indent || b->start_line <= a->end_line) {
        return false;
    }
    if (b->start_line == a->end_line + 1) {
        return true;
    }
    if (kind != CSANE_WRAP_ASSIGN) {
        return false;
    }
    /* Only comment-only intervening lines keep an assignment group intact.
     * A blank line, directive, or arbitrary code always splits the group.
     */
    return csane_wrap_comment_lines(file, a, b);
}

/* The parser may skip a neighboring long statement because its type
 * comes from a header. Such a statement could justify a short wrap in the
 * same group. Never infer "all statements fit" across nearby parse issues.
 * A blank line separates independent groups despite neighboring issues.
 */
static bool
csane_wrap_blank_between(Tokenization *tokens, int32 a, int32 b) {
    if (a > b) {
        int32 previous = a;
        a = b;
        b = previous;
    }
    for (int32 line = a + 1; line < b; line += 1) {
        int32 start = tokens->line_starts[line - 1];
        int32 end = line < tokens->line_count
                    ? tokens->line_starts[line] : tokens->text_len;
        bool nonempty = false;
        for (int32 i = start; i < end; i += 1) {
            if (tokens->text[i] != ' ' && tokens->text[i] != '\t'
                && tokens->text[i] != '\r'
                && tokens->text[i] != '\n') {
                nonempty = true;
                break;
            }
        }
        if (!nonempty) {
            return true;
        }
    }
    return false;
}

static bool
csane_wrap_uncertain(Tokenization *tokens, CsaneWrapSpan *span,
                     bool *issue_lines) {
    int32 first = span->start_line - 4;
    int32 last = span->end_line + 4;
    if (first < 1) {
        first = 1;
    }
    if (last > tokens->line_count) {
        last = tokens->line_count;
    }
    for (int32 line = first; line <= last; line += 1) {
        if (issue_lines[line]
            && !csane_wrap_blank_between(tokens, span->start_line, line)
            && !csane_wrap_blank_between(tokens, span->end_line, line)) {
            return true;
        }
    }
    return false;
}

static void
csane_wrap_report(CsaneFile *file, CsaneWrapSpan *items,
                  int32 count, enum CsaneWrapKind kind, bool *issues) {
    bool all_fit = true;
    for (int32 i = 0; i < count; i += 1) {
        all_fit = all_fit && items[i].fits
                  && !csane_wrap_uncertain(file->tokens, &items[i], issues);
    }
    if (!all_fit) {
        return;
    }
    for (int32 i = 0; i < count; i += 1) {
        if (!items[i].wrapped) {
            continue;
        }
        if (kind == CSANE_WRAP_CALL) {
            csane_rule_issue(file, items[i].start, "FMT-CALL-WRAP",
                             "call fits on one line but is split "
                             "across multiple lines");
        } else {
            csane_rule_issue(file, items[i].start, "FMT-ASSIGN-WRAP",
                             "assignment fits on one line "
                             "but is split after =");
        }
    }
    return;
}

static void
csane_wrap_children(CsaneFile *file, int32 first,
                    CsaneWrapSpan *items, enum CsaneWrapKind kind,
                    bool *issues) {
    CAst *ast = file->ast;
    int32 count = 0;
    for (int32 s = first; s >= 0; s = ast->statements[s].next) {
        CsaneWrapSpan span;
        bool candidate = csane_wrap_span(file, s, kind, &span);
        if (candidate && count > 0
            && !csane_wrap_adjacent(file, &items[count - 1], &span, kind)) {
            csane_wrap_report(file, items, count, kind, issues);
            count = 0;
        }
        if (!candidate && count > 0) {
            csane_wrap_report(file, items, count, kind, issues);
            count = 0;
        }
        if (candidate) {
            items[count] = span;
            count += 1;
        }
    }
    if (count > 0) {
        csane_wrap_report(file, items, count, kind, issues);
    }
    return;
}

void
csane_wrap_check(CsaneFile *file) {
    CAst *ast = file->ast;
    if (csane_wrap_generated(file->path)
        || ast->statement_count == 0) {
        return;
    }
    CsaneWrapSpan *items = malloc2((int64)ast->statement_count*SIZEOF(*items));
    Tokenization *tokens = file->tokens;
    /* Source locations have initialized the lazily allocated line index. */
    tokenization_source_location(tokens, 0);
    int32 issue_size = tokens->line_count + 1;
    bool *issues = malloc2((int64)issue_size*SIZEOF(*issues));
    if (items == NULL || issues == NULL) {
        if (items != NULL) {
            free2(items, (int64)ast->statement_count*SIZEOF(*items));
        }
        if (issues != NULL) {
            free2(issues, (int64)issue_size*SIZEOF(*issues));
        }
        return;
    }
    memset64(issues, 0, (int64)issue_size*SIZEOF(*issues));
    for (int32 i = 0; i < ast->issue_count; i += 1) {
        int32 token = ast->issues[i].token;
        if (token < 0 || token >= tokens->token_count) {
            continue;
        }
        int32 line = csane_wrap_location(tokens, token).line;
        if (line > 0 && line < issue_size) {
            issues[line] = true;
        }
    }
    for (int32 i = 0; i < ast->statement_count; i += 1) {
        CStatement *stmt = &ast->statements[i];
        if (stmt->kind != C_STMT_COMPOUND
            && stmt->kind != C_STMT_PP_BRANCH) {
            continue;
        }
        for (int32 mode = CSANE_WRAP_CALL; mode <= CSANE_WRAP_ASSIGN;
             mode += 1) {
            if (file->is_ryu && mode == CSANE_WRAP_ASSIGN) {
                continue;
            }
            csane_wrap_children(file, stmt->first_child, items,
                                (enum CsaneWrapKind)mode, issues);
        }
    }
    free2(items, (int64)ast->statement_count*SIZEOF(*items));
    free2(issues, (int64)issue_size*SIZEOF(*issues));
    return;
}

#endif /* CSANECHECK_WRAP_C */
