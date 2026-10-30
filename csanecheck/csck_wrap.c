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
csane_wrap_token_is(Token *token, char *word, int32 word_len) {
    return token->len == word_len
           && !memcmp64(token->text, word, word_len);
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
csane_wrap_generated(char *path, int32 path_len) {
    return ENDS_WITH(path, path_len, ".meta.h");
}

/* A run of adjacent string fragments remains deliberately split when
 * every fragment ends in a literal newline escape. Count the backslashes
 * preceding 'n' to distinguish escaped backslashes from newline escapes.
 */
static bool
csane_wrap_literal_newline(Token *token) {
    int32 backslashes = 0;

    if (token->len < 3 || token->text[token->len - 1] != '"'
        || token->text[token->len - 2] != 'n') {
        return false;
    }
    for (int32 i = token->len - 3; i >= 0 && token->text[i] == '\\';
         i -= 1) {
        backslashes += 1;
    }
    return (backslashes % 2) != 0;
}

/* Preserve macro continuations, comments, initializers, and statement
 * boundaries. Unlike ordinary multiline strings, a run of literal fragments
 * with individual newline escapes intentionally retains physical breaks.
 */
static bool
csane_wrap_safe(CsaneFile *file, int32 first, int32 last) {
    Tokenization *tokens = file->tokens;
    int32 previous_string = -1;
    int32 run_count = 0;
    bool run_newlines = true;
    for (int32 i = first; i <= last; i += 1) {
        Token *token = &tokens->tokens[i];
        enum CPreprocessorTokenRole role = C_PP_TOKEN_CODE;
        int32 quote = -1;

        if (file->preprocessor->token_roles != NULL) {
            role = file->preprocessor->token_roles[i];
        }
        if (role != C_PP_TOKEN_CODE || token->kind == TOKEN_COMMENT
            || csane_wrap_token_is(token, STRLIT("{"))
            || csane_wrap_token_is(token, STRLIT("}"))) {
            return false;
        }
        if (csane_wrap_token_is(token, STRLIT(";")) && i != last) {
            return false;
        }
        if (token->kind == TOKEN_LITERAL) {
            quote = literal_quote_index(token->text, token->len, 0);
        }
        if (quote >= 0 && token->text[quote] == '"') {
            if (previous_string >= 0) {
                SourceLocation a;
                SourceLocation b;

                a = csane_wrap_location(tokens, previous_string);
                b = csane_wrap_location(tokens, i);
                if (a.line != b.line) {
                    run_count += 1;
                    run_newlines = run_newlines
                                   && csane_wrap_literal_newline(token);
                } else {
                    if (run_count > 1 && run_newlines) {
                        return false;
                    }
                    run_count = 1;
                    run_newlines = true;
                }
            } else {
                run_count = 1;
                run_newlines = true;
            }
            run_newlines = run_newlines && csane_wrap_literal_newline(token);
            previous_string = i;
        } else if (!token_is_trivia(token)) {
            if (run_count > 1 && run_newlines) {
                return false;
            }
            previous_string = -1;
            run_count = 0;
            run_newlines = true;
        }
    }
    return run_count <= 1 || !run_newlines;
}

/* Compute the width of joining the original lines, preserving their actual
 * spelling. A break before a closing delimiter needs no added space.
 */
int32
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
    int32 name;
    int32 open;
    int32 close;
    enum CsaneLocalKind kind;

    if (first < 0 || last < first
        || !csane_wrap_token_is(&tokens->tokens[last], STRLIT(";"))) {
        return -1;
    }
    name = first;
    if (csane_wrap_token_is(&tokens->tokens[name], STRLIT("return"))) {
        name = csane_tokens_next(&view, name);
    } else {
        /* A declaration initializer or plain assignment may precede the
         * invocation. Ignore operators nested within delimiters. */
        int32 equals = -1;
        for (int32 i = first; i < last; i += 1) {
            if (csane_wrap_token_is(&tokens->tokens[i], STRLIT("="))) {
                equals = i;
            } else if (csane_wrap_token_is(&tokens->tokens[i], STRLIT("("))
                       || csane_wrap_token_is(&tokens->tokens[i],
                                              STRLIT("["))) {
                int32 match = csane_tokens_match(&view, i);
                if (match > i && match < last) {
                    i = match;
                }
            }
        }
        if (equals >= 0) {
            name = csane_tokens_next(&view, equals);
        }
    }
    if (name < 0 || name >= last
        || tokens->tokens[name].kind != TOKEN_IDENT
        || csane_wrap_token_is(&tokens->tokens[name], STRLIT("error_impl"))
        || csane_wrap_token_is(&tokens->tokens[name], STRLIT("assert_error"))) {
        return -1;
    }
    open = csane_tokens_next(&view, name);
    if (open < 0 || open >= last
        || !csane_wrap_token_is(&tokens->tokens[open], STRLIT("("))) {
        return -1;
    }
    close = csane_tokens_match(&view, open);
    if (close < 0 || close >= last
        || csane_tokens_next(&view, close) != last) {
        return -1;
    }
    kind = csane_classify_invocation(tokens, range, name);
    /* Capitalized function names resemble type names to the heuristic
     * classifier. A complete call statement has no type declarator here.
     */
    if (kind == CSANE_LOCAL_UNKNOWN
        && csane_name_kind(&tokens->tokens[name]) == CSANE_NAME_TYPE) {
        return name;
    }
    return kind == CSANE_LOCAL_CALL
           || kind == CSANE_LOCAL_MACRO_INVOCATION ? name : -1;
}

static bool
csane_wrap_local_span(CsaneFile *file, TokenRange range,
                      CsaneWrapSpan *span) {
    Tokenization *tokens = file->tokens;
    int32 first = token_range_first_significant(tokens, range);
    int32 last = token_range_last_significant(tokens, range);
    int32 name;
    char *text;
    int32 first_offset;
    int32 line_start;
    int32 close;
    int32 line_end;

    if (first < 0 || last < first) {
        return false;
    }
    name = csane_wrap_local_call(file, range);
    if (name < 0 || csane_wrap_location(tokens, name).line
        != csane_wrap_location(tokens, first).line) {
        return false;
    }
    text = tokens->text;
    first_offset = tokens->tokens[first].offset;
    line_start = csane_wrap_line_start(text, first_offset);
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
    span->start_line = csane_wrap_location(tokens, first).line;
    span->end_line = csane_wrap_location(tokens, last).line;
    /* Perl inspects the entire physical call statement and requires the
     * semicolon to be on the same line as its closing parenthesis. A second
     * statement or trailing comment also makes the candidate ineligible.
     */
    close = tokenization_previous_significant(tokens, last);
    if (close < 0 || csane_wrap_location(tokens, close).line
        != span->end_line) {
        return false;
    }
    line_end = span->end;
    while (line_end < tokens->text_len && text[line_end] != '\n') {
        if (text[line_end] != ' ' && text[line_end] != '\t'
            && text[line_end] != '\r') {
            return false;
        }
        line_end += 1;
    }
    span->end = line_end;
    span->wrapped = span->end_line > span->start_line;
    span->fits = false;
    if (csane_wrap_safe(file, first, last)) {
        int32 width = csane_wrap_flat_length(text, span->start, span->end);
        span->fits = width >= 0 && (file->column_limit <= 0
                                    || width <= file->column_limit);
    }
    return true;
}

/* Gather candidates once, irrespective of the statement tree's branch
 * representation. Different conditional streams may expose the same original
 * statement more than once; source offsets identify those duplicates.
 */
static int32
csane_wrap_collect_calls(CsaneFile *file, CsaneWrapSpan *items) {
    int32 used = 0;
    if (file->statements != NULL) {
        CsaneStatements *tree = file->statements;
        for (int32 i = 0; i < tree->count; i += 1) {
            CsaneStatement *item = &tree->items[i];
            CsaneWrapSpan span;
            if (item->kind == CSANE_STMT_SIMPLE
                && csane_wrap_local_span(file, item->tokens, &span)) {
                items[used] = span;
                used += 1;
            }
        }
    }
    if (file->toplevel != NULL) {
        CsaneTopLevels *top = file->toplevel;
        for (int32 i = 0; i < top->count; i += 1) {
            CsaneTopItem *item = &top->items[i];
            CsaneWrapSpan span;
            if ((item->kind == CSANE_TOP_DECLARATION
                 || item->kind == CSANE_TOP_OPAQUE)
                && csane_wrap_local_span(file, item->tokens, &span)) {
                items[used] = span;
                used += 1;
            }
        }
    }
    return used;
}

static int
csane_wrap_compare_calls(void *left, void *right) {
    const CsaneWrapSpan *a = left;
    const CsaneWrapSpan *b = right;
    if (a->start < b->start) {
        return -1;
    }
    if (a->start > b->start) {
        return 1;
    }
    return 0;
}

static void
csane_wrap_call_group(CsaneFile *file, CsaneWrapSpan *items,
                      int32 first, int32 end) {
    bool all_fit = true;
    for (int32 i = first; i < end; i += 1) {
        all_fit = all_fit && items[i].fits;
    }
    if (!all_fit) {
        return;
    }
    for (int32 i = first; i < end; i += 1) {
        if (items[i].wrapped) {
            csane_rule_issue(file, items[i].start, STRLIT("FMT-CALL-WRAP"),
                             "call fits on one line but is "
                             "split across multiple lines");
        }
    }
    return;
}

void
csane_wrap_call_check(CsaneFile *file) {
    int32 capacity = 0;
    CsaneWrapSpan *items;
    int32 count;
    int32 unique = 0;

    if (csane_wrap_generated(file->path, file->path_len)) {
        return;
    }
    if (file->statements != NULL) {
        capacity += file->statements->count;
    }
    if (file->toplevel != NULL) {
        capacity += file->toplevel->count;
    }
    if (capacity == 0) {
        return;
    }
    items = malloc2(capacity*SIZEOF(*items));
    count = csane_wrap_collect_calls(file, items);
    qsort64(items, count, SIZEOF(*items), csane_wrap_compare_calls);
    for (int32 i = 0; i < count; i += 1) {
        if (unique > 0 && items[i].start == items[unique - 1].start) {
            continue;
        }
        items[unique] = items[i];
        unique += 1;
    }
    for (int32 first = 0; first < unique;) {
        int32 end = first + 1;
        while (end < unique
               && csane_wrap_adjacent(file, &items[end - 1], &items[end],
                                      CSANE_WRAP_CALL)) {
            end += 1;
        }
        csane_wrap_call_group(file, items, first, end);
        first = end;
    }
    free2(items, capacity*SIZEOF(*items));
    return;
}

/* Assignment wrapping uses only direct, top-level '=' tokens in a simple
 * statement. Nested assignments in conditions and compound operators are
 * excluded by the statement boundary and token-spelling checks. */
static int32
csane_wrap_local_assignment(CsaneFile *file, TokenRange range) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    int32 first;
    int32 last;
    int32 equals = -1;

    if (!csane_tokens_view(&view, tokens, range)) {
        return -1;
    }
    first = token_range_first_significant(tokens, range);
    last = token_range_last_significant(tokens, range);
    if (first < 0 || last <= first
        || !csane_wrap_token_is(&tokens->tokens[last], STRLIT(";"))) {
        return -1;
    }
    for (int32 i = first; i < last; i += 1) {
        Token *token = &tokens->tokens[i];
        if (csane_wrap_token_is(token, STRLIT("="))) {
            if (equals >= 0) {
                return -1;
            }
            equals = i;
        } else if (csane_wrap_token_is(token, STRLIT("("))
                   || csane_wrap_token_is(token, STRLIT("["))
                   || csane_wrap_token_is(token, STRLIT("{"))) {
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
    char *text;
    int32 offset;
    int32 start;
    int32 end;
    int32 line_end;
    bool trailing_comment = false;
    bool braced = false;
    int32 after;

    if (op < 0 || first < 0 || last < first) {
        return false;
    }
    text = tokens->text;
    offset = tokens->tokens[first].offset;
    start = csane_wrap_line_start(text, offset);
    if (require_indent && start == offset) {
        return false;
    }
    for (int32 i = start; i < offset; i += 1) {
        if (text[i] != ' ') {
            return false;
        }
    }
    /* Perl considers only assignments starting on a physical line and
     * ending with its sole semicolon. A later statement on the same line
     * cannot be an independent assignment-wrap candidate. */
    end = tokens->tokens[last].offset + tokens->tokens[last].len;
    line_end = end;
    while (line_end < tokens->text_len && text[line_end] != '\n') {
        line_end += 1;
    }
    for (int32 i = end; i < line_end;) {
        bool comment = false;

        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r') {
            i += 1;
            continue;
        }
        for (int32 j = last + 1; j < tokens->token_count; j += 1) {
            Token *token = &tokens->tokens[j];
            if (token->offset > i) {
                break;
            }
            if (token->kind == TOKEN_COMMENT
                && token->offset <= i && token->offset + token->len > i
                && token->offset + token->len <= line_end) {
                i = token->offset + token->len;
                trailing_comment = true;
                comment = true;
                break;
            }
        }
        if (!comment) {
            return false;
        }
    }
    for (int32 i = first; i <= last; i += 1) {
        if (csane_wrap_token_is(&tokens->tokens[i], STRLIT("{"))
            || csane_wrap_token_is(&tokens->tokens[i], STRLIT("}"))) {
            /* When a multiline initializer introduces braces, the Perl
             * local scan cannot form a complete statement. */
            if (csane_wrap_location(tokens, i).line >
                csane_wrap_location(tokens, first).line) {
                return false;
            }
            braced = true;
        }
    }
    span->start = start;
    span->end = end;
    span->indent = offset - start;
    span->start_line = csane_wrap_location(tokens, first).line;
    span->end_line = csane_wrap_location(tokens, last).line;
    after = tokens->tokens[op].offset + tokens->tokens[op].len;
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
    if (!braced && !trailing_comment && csane_wrap_safe(file, first, last)) {
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
            csane_rule_issue(file, items[i].start, STRLIT("FMT-ASSIGN-WRAP"),
                             "assignment fits on one line "
                             "but is split after =");
        }
    }
    return;
}

/* Existing source-preserving statement and top-level scanners supply the
 * majority of assignments. Do not trust their ordering or branch ownership:
 * a conditional source stream may yield the same assignment twice. */
static int32
csane_wrap_collect_assignments(CsaneFile *file, CsaneWrapSpan *items) {
    int32 used = 0;
    if (file->toplevel != NULL) {
        CsaneTopLevels *top = file->toplevel;
        for (int32 i = 0; i < top->count; i += 1) {
            CsaneTopItem *item = &top->items[i];
            CsaneWrapSpan span;
            if ((item->kind == CSANE_TOP_DECLARATION
                 || item->kind == CSANE_TOP_OPAQUE)
                && csane_wrap_local_assignment_span(file, item->tokens,
                                                    &span, false)) {
                items[used] = span;
                used += 1;
            }
        }
    }
    if (file->statements != NULL) {
        CsaneStatements *tree = file->statements;
        for (int32 i = 0; i < tree->count; i += 1) {
            CsaneStatement *stmt = &tree->items[i];
            CsaneWrapSpan span;
            if (stmt->kind == CSANE_STMT_SIMPLE
                && csane_wrap_local_assignment_span(file, stmt->tokens,
                                                    &span, true)) {
                items[used] = span;
                used += 1;
            }
        }
    }
    return used;
}

/* Preprocessor branches can interrupt an otherwise incomplete statement.
 * Recover independent assignments after such gaps directly from original
 * tokens; this does not parse or evaluate a conditional branch. */
static int32
csane_wrap_collect_assignment_gaps(CsaneFile *file, CsaneWrapSpan *items,
                                   int32 used) {
    Tokenization *tokens = file->tokens;
    for (int32 op = 0; op < tokens->token_count; op += 1) {
        int32 start;
        int32 first;
        int32 end;
        CsaneWrapSpan span;
        TokenRange range;

        if (!csane_wrap_token_is(&tokens->tokens[op], STRLIT("="))) {
            continue;
        }
        if (file->preprocessor->token_roles != NULL
            && file->preprocessor->token_roles[op] != C_PP_TOKEN_CODE) {
            continue;
        }
        start = csane_wrap_line_start(tokens->text, tokens->tokens[op].offset);
        first = op;
        while (first > 0 && tokens->tokens[first - 1].offset >= start) {
            first -= 1;
        }
        end = op + 1;
        while (end < tokens->token_count) {
            Token *token = &tokens->tokens[end];
            if (csane_wrap_token_is(token, STRLIT("}"))
                || token->kind == TOKEN_PREPROC) {
                break;
            }
            if (csane_wrap_token_is(token, STRLIT(";"))) {
                end += 1;
                break;
            }
            end += 1;
        }
        if (end <= op + 1 || end > tokens->token_count
            || !csane_wrap_token_is(&tokens->tokens[end - 1], STRLIT(";"))) {
            continue;
        }
        range = (TokenRange){first, end};
        if (csane_wrap_local_assignment_span(file, range, &span, false)) {
            items[used] = span;
            used += 1;
        }
    }
    return used;
}

void
csane_wrap_check(CsaneFile *file) {
    int32 capacity;
    CsaneWrapSpan *items;
    int32 count;
    int32 unique = 0;

    if (file->is_ryu || csane_wrap_generated(file->path, file->path_len)) {
        return;
    }
    capacity = file->tokens->token_count;
    if (file->statements != NULL) {
        capacity += file->statements->count;
    }
    if (file->toplevel != NULL) {
        capacity += file->toplevel->count;
    }
    if (capacity == 0) {
        return;
    }
    items = malloc2(capacity*SIZEOF(*items));
    count = csane_wrap_collect_assignments(file, items);
    count = csane_wrap_collect_assignment_gaps(file, items, count);
    qsort64(items, count, SIZEOF(*items), csane_wrap_compare_calls);
    for (int32 i = 0; i < count; i += 1) {
        if (unique > 0 && items[i].start == items[unique - 1].start) {
            continue;
        }
        items[unique] = items[i];
        unique += 1;
    }
    for (int32 first = 0; first < unique;) {
        int32 end = first + 1;
        while (end < unique
               && csane_wrap_adjacent(file, &items[end - 1], &items[end],
                                      CSANE_WRAP_ASSIGN)) {
            end += 1;
        }
        csane_wrap_assignment_group(file, &items[first], end - first);
        first = end;
    }
    free2(items, capacity*SIZEOF(*items));
    return;
}

#endif /* CSANECHECK_WRAP_C */
