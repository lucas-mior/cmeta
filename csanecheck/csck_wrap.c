// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_WRAP_C)
#define CSANECHECK_WRAP_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

/* Source-preserving spans for locally recognized calls and assignments.
 * Nested expressions are not standalone statement candidates.
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

/* A call-wrap candidate is one complete source statement whose value is a
 * direct, named invocation. No translation-unit AST is required. */
static int32
csane_wrap_local_call(CsaneFile *file, TokenRange range) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    int32 first = csane_tokens_next(&view, range.first - 1);
    int32 last = token_range_last_significant(tokens, range);
    if (first < 0 || last < first
        || !csane_wrap_token_is(&tokens->tokens[last], ";")) {
        return -1;
    }
    int32 name = first;
    if (csane_wrap_token_is(&tokens->tokens[name], "return")) {
        name = csane_tokens_next(&view, name);
    } else {
        /* A declaration initializer or plain assignment may precede the
         * invocation. Ignore operators nested within delimiters. */
        int32 equals = -1;
        for (int32 i = first; i < last; i += 1) {
            if (csane_wrap_token_is(&tokens->tokens[i], "=")) {
                equals = i;
            } else if (csane_wrap_token_is(&tokens->tokens[i], "(")
                       || csane_wrap_token_is(&tokens->tokens[i], "[")) {
                int32 close = csane_tokens_match(&view, i);
                if (close > i && close < last) {
                    i = close;
                }
            }
        }
        if (equals >= 0) {
            name = csane_tokens_next(&view, equals);
        }
    }
    if (name < 0 || name >= last
        || tokens->tokens[name].kind != TOKEN_IDENT
        || csane_wrap_token_is(&tokens->tokens[name], "error_impl")
        || csane_wrap_token_is(&tokens->tokens[name], "assert_error")) {
        return -1;
    }
    int32 open = csane_tokens_next(&view, name);
    if (open < 0 || open >= last
        || !csane_wrap_token_is(&tokens->tokens[open], "(")) {
        return -1;
    }
    int32 close = csane_tokens_match(&view, open);
    if (close < 0 || close >= last
        || csane_tokens_next(&view, close) != last) {
        return -1;
    }
    enum CsaneLocalKind kind = csane_classify_invocation(tokens, range, name);
    return kind == CSANE_LOCAL_CALL
           || kind == CSANE_LOCAL_MACRO_INVOCATION ? name : -1;
}

static bool
csane_wrap_local_span(CsaneFile *file, TokenRange range,
                      CsaneWrapSpan *span, bool require_statement) {
    Tokenization *tokens = file->tokens;
    int32 first = token_range_first_significant(tokens, range);
    int32 last = token_range_last_significant(tokens, range);
    if (first < 0 || last < first) {
        return false;
    }
    int32 name = csane_wrap_local_call(file, range);
    if (name < 0 || csane_wrap_location(tokens, name).line
        != csane_wrap_location(tokens, first).line) {
        return false;
    }
    char *text = tokens->text;
    int32 first_offset = tokens->tokens[first].offset;
    int32 line_start = csane_wrap_line_start(text, first_offset);
    if (require_statement && line_start == first_offset) {
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
    span->start_line = csane_wrap_location(tokens, first).line;
    span->end_line = csane_wrap_location(tokens, last).line;
    span->wrapped = span->end_line > span->start_line;
    if (csane_name_kind(&tokens->tokens[name]) == CSANE_NAME_MACRO) {
        CsaneTokenView view = csane_tokens_full(tokens);
        int32 open = csane_tokens_next(&view, name);
        int32 first_arg = csane_tokens_next(&view, open);
        span->wrapped = first_arg >= 0
                        && csane_wrap_location(tokens, first_arg).line
                           > csane_wrap_location(tokens, open).line;
    }
    span->fits = false;
    if (csane_wrap_safe(file, first, last)) {
        int32 width = csane_wrap_flat_length(text, span->start, span->end);
        span->fits = width >= 0 && (file->column_limit <= 0
                                    || width <= file->column_limit);
    }
    return true;
}

/* File-scope declarations and incomplete legacy fragments have no
 * statement-tree entries. Reuse the local call recognizer on their ranges.
 */
static void
csane_wrap_toplevel_calls(CsaneFile *file) {
    if (file->toplevel == NULL) {
        return;
    }
    CsaneTopLevels *top = file->toplevel;
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->kind != CSANE_TOP_DECLARATION
            && item->kind != CSANE_TOP_OPAQUE) {
            continue;
        }
        CsaneWrapSpan span;
        if (csane_wrap_local_span(file, item->tokens, &span, false)
            && span.wrapped && span.fits) {
            csane_rule_issue(file, span.start, "FMT-CALL-WRAP",
                             "call fits on one line but is "
                             "split across multiple lines");
        }
    }
}

void
csane_wrap_call_check(CsaneFile *file) {
    if (csane_wrap_generated(file->path) || file->statements == NULL) {
        return;
    }
    CsaneStatements *tree = file->statements;
    int32 count = tree->count;
    CsaneWrapSpan *items = malloc2((int64)count*SIZEOF(*items));
    if (items == NULL) {
        return;
    }
    for (int32 parent = -1; parent < count; parent += 1) {
        if (parent >= 0
            && tree->items[parent].kind != CSANE_STMT_BLOCK) {
            continue;
        }
        int32 used = 0;
        int32 child = parent < 0 ? 0 : tree->items[parent].first_child;
        for (; child >= 0 && child < count;) {
            if (parent < 0 && tree->items[child].parent != -1) {
                child += 1;
                continue;
            }
            CsaneWrapSpan span;
            bool candidate = tree->items[child].kind == CSANE_STMT_SIMPLE
                             && csane_wrap_local_span(file,
                                                      tree->items[child].tokens,
                                                      &span, true);
            if (used > 0 && (!candidate
                || !csane_wrap_adjacent(file, &items[used - 1], &span,
                                        CSANE_WRAP_CALL))) {
                /* No AST parse-issue suppression: local statement boundaries
                 * determine the complete neighboring group. */
                bool all_fit = true;
                for (int32 i = 0; i < used; i += 1) {
                    all_fit = all_fit && items[i].fits;
                }
                if (all_fit) {
                    for (int32 i = 0; i < used; i += 1) {
                        if (items[i].wrapped) {
                            csane_rule_issue(file, items[i].start,
                                             "FMT-CALL-WRAP",
                                             "call fits on one line but is "
                                             "split across multiple lines");
                        }
                    }
                }
                used = 0;
            }
            if (candidate) {
                items[used] = span;
                used += 1;
            }
            child = parent < 0 ? child + 1
                    : tree->items[child].next_sibling;
        }
        if (used > 0) {
            bool all_fit = true;
            for (int32 i = 0; i < used; i += 1) {
                all_fit = all_fit && items[i].fits;
            }
            if (all_fit) {
                for (int32 i = 0; i < used; i += 1) {
                    if (items[i].wrapped) {
                        csane_rule_issue(file, items[i].start,
                                         "FMT-CALL-WRAP",
                                         "call fits on one line but is "
                                         "split across multiple lines");
                    }
                }
            }
        }
    }
    free2(items, (int64)count*SIZEOF(*items));
    csane_wrap_toplevel_calls(file);
}

/* Assignment wrapping uses only direct, top-level '=' tokens in a simple
 * statement. Nested assignments in conditions and compound operators are
 * excluded by the statement boundary and token-spelling checks. */
static int32
csane_wrap_local_assignment(CsaneFile *file, TokenRange range) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    int32 first = token_range_first_significant(tokens, range);
    int32 last = token_range_last_significant(tokens, range);
    if (first < 0 || last <= first
        || !csane_wrap_token_is(&tokens->tokens[last], ";")) {
        return -1;
    }
    int32 equals = -1;
    for (int32 i = first; i < last; i += 1) {
        Token *token = &tokens->tokens[i];
        if (csane_wrap_token_is(token, "=")) {
            if (equals >= 0) {
                return -1;
            }
            equals = i;
        } else if (csane_wrap_token_is(token, "(")
                   || csane_wrap_token_is(token, "[")
                   || csane_wrap_token_is(token, "{")) {
            int32 close = csane_tokens_match(&view, i);
            if (close > i && close < last) {
                i = close;
            }
        }
    }
    return equals;
}

static bool
csane_wrap_local_assignment_span(CsaneFile *file, TokenRange range,
                                 CsaneWrapSpan *span, bool require_indent) {
    Tokenization *tokens = file->tokens;
    int32 first = token_range_first_significant(tokens, range);
    int32 last = token_range_last_significant(tokens, range);
    int32 op = csane_wrap_local_assignment(file, range);
    if (op < 0 || first < 0 || last < first) {
        return false;
    }
    char *text = tokens->text;
    int32 offset = tokens->tokens[first].offset;
    int32 start = csane_wrap_line_start(text, offset);
    if (require_indent && start == offset) {
        return false;
    }
    for (int32 i = start; i < offset; i += 1) {
        if (text[i] != ' ') {
            return false;
        }
    }
    span->start = start;
    span->end = tokens->tokens[last].offset + tokens->tokens[last].len;
    span->indent = offset - start;
    span->start_line = csane_wrap_location(tokens, first).line;
    span->end_line = csane_wrap_location(tokens, last).line;
    int32 after = tokens->tokens[op].offset + tokens->tokens[op].len;
    while (after < tokens->text_len
           && (text[after] == ' ' || text[after] == '\t'
               || text[after] == '\r')) {
        after += 1;
    }
    span->wrapped = after < tokens->text_len && text[after] == '\n';
    span->fits = false;
    /* A locally recognized type name need not be declared in this file.
     * Preserve the legacy conservative grouping for such declarations. */
    if (csane_name_kind(&tokens->tokens[first]) == CSANE_NAME_TYPE) {
        return true;
    }
    if (csane_wrap_safe(file, first, last)) {
        int32 width = csane_wrap_flat_length(text, start, span->end);
        span->fits = width >= 0 && (file->column_limit <= 0
                                    || width <= file->column_limit);
    }
    return true;
}

static void
csane_wrap_assignment_group(CsaneFile *file, CsaneWrapSpan *items,
                            int32 count) {
    bool all_fit = true;
    for (int32 i = 0; i < count; i += 1) {
        all_fit = all_fit && items[i].fits;
    }
    if (!all_fit) {
        return;
    }
    for (int32 i = 0; i < count; i += 1) {
        if (items[i].wrapped) {
            csane_rule_issue(file, items[i].start, "FMT-ASSIGN-WRAP",
                             "assignment fits on one line "
                             "but is split after =");
        }
    }
}

/* Top-level declarations do not have CsaneStatement records. Use their
 * source-preserving token ranges with the same conservative grouping rules. */
static void
csane_wrap_toplevel_assignments(CsaneFile *file) {
    if (file->toplevel == NULL) {
        return;
    }
    CsaneTopLevels *top = file->toplevel;
    CsaneWrapSpan *items = malloc2((int64)top->count*SIZEOF(*items));
    if (items == NULL) {
        return;
    }
    int32 used = 0;
    int32 owner = -1;
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        CsaneWrapSpan span;
        bool candidate = item->kind == CSANE_TOP_DECLARATION
                         || item->kind == CSANE_TOP_OPAQUE;
        if (candidate) {
            candidate = csane_wrap_local_assignment_span(file, item->tokens,
                                                         &span, false);
        }
        if (used > 0 && (owner != item->owner_region || !candidate
            || !csane_wrap_adjacent(file, &items[used - 1], &span,
                                    CSANE_WRAP_ASSIGN))) {
            csane_wrap_assignment_group(file, items, used);
            used = 0;
        }
        if (candidate) {
            items[used++] = span;
            owner = item->owner_region;
        }
    }
    if (used > 0) {
        csane_wrap_assignment_group(file, items, used);
    }
    free2(items, (int64)top->count*SIZEOF(*items));
}

void
csane_wrap_check(CsaneFile *file) {
    if (file->is_ryu || csane_wrap_generated(file->path)) {
        return;
    }
    csane_wrap_toplevel_assignments(file);
    if (file->statements == NULL || file->statements->count == 0) {
        return;
    }
    CsaneStatements *tree = file->statements;
    int32 count = tree->count;
    CsaneWrapSpan *items = malloc2((int64)count*SIZEOF(*items));
    if (items == NULL) {
        return;
    }
    for (int32 parent = -1; parent < count; parent += 1) {
        if (parent >= 0
            && tree->items[parent].kind != CSANE_STMT_BLOCK) {
            continue;
        }
        int32 used = 0;
        int32 child = parent < 0 ? 0 : tree->items[parent].first_child;
        for (; child >= 0 && child < count;) {
            if (parent < 0 && tree->items[child].parent != -1) {
                child += 1;
                continue;
            }
            CsaneWrapSpan span;
            CsaneStatement *stmt = &tree->items[child];
            bool candidate = stmt->kind == CSANE_STMT_SIMPLE;
            if (candidate) {
                TokenRange range = stmt->tokens;
                candidate = csane_wrap_local_assignment_span(file, range,
                                                             &span, true);
            }
            if (used > 0 && (!candidate
                || !csane_wrap_adjacent(file, &items[used - 1], &span,
                                        CSANE_WRAP_ASSIGN))) {
                csane_wrap_assignment_group(file, items, used);
                used = 0;
            }
            if (candidate) {
                items[used] = span;
                used += 1;
            }
            child = parent < 0 ? child + 1
                    : tree->items[child].next_sibling;
        }
        if (used > 0) {
            csane_wrap_assignment_group(file, items, used);
        }
    }
    free2(items, (int64)count*SIZEOF(*items));
}

#endif /* CSANECHECK_WRAP_C */
