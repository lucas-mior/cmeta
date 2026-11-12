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
    bool compound;
    int32 call_name;
    TokenRange first_arg;
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
    for (int32 i = token->len - 3; i >= 0 && token->text[i] == '\\'; i -= 1) {
        backslashes += 1;
    }
    return (backslashes % 2) != 0;
}

/* Preserve macro continuations, comments, initializers, and statement
 * boundaries. Unlike ordinary multiline strings, a run of literal fragments
 * with individual newline escapes intentionally retains physical breaks.
 */
bool
csane_wrap_safe(CsaneFile *file, int32 first, int32 last,
                bool allow_semicolons) {
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
        if (!allow_semicolons && csane_wrap_token_is(token, STRLIT(";"))
            && i != last) {
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
 * spelling. A break before a closing delimiter needs no added space. A
 * positive maximum permits an early result once the text cannot fit.
 */
int32
csane_wrap_flat_length(char *text, int32 start, int32 end, int32 max_width) {
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
            if (max_width > 0 && width > max_width) {
                return width;
            }
            i = j;
            continue;
        }
        width += 1;
        i += 1;
        if (max_width > 0 && width > max_width
            && text[i - 1] != ' ' && text[i - 1] != '\t'
            && text[i - 1] != '\r') {
            return width;
        }
    }
    return width;
}

static bool
csane_wrap_adjacent(CsaneFile *file, CsaneWrapSpan *a,
                    CsaneWrapSpan *b, enum CsaneWrapKind kind) {
                        bool _i63_result;
    if (a->indent != b->indent || b->start_line <= a->end_line) {
        return false;
    }
    if (b->start_line == a->end_line + 1) {
        return true;
    }
    if (kind != CSANE_WRAP_ASSIGN) {
        return false;
    }
    do {
        Tokenization *tokens = file->tokens;
        char *text = tokens->text;
        int32 position = a->end;
        while (position < b->start && text[position] != '\n') {
            position += 1;
        }
        if (position >= b->start) {
            _i63_result = false;
            goto _i63_done;
        }
        position += 1;
        for (int32 line = a->end_line + 1; line < b->start_line; line += 1) {
            int32 end = position;
            bool found_comment = false;
            int32 first;
            while (end < b->start && text[end] != '\n') {
                end += 1;
            }
            first = tokenization_token_at_or_after_offset(tokens, position);
            for (int32 i = first; i < ARRAY_LEN(tokens->tokens); i += 1) {
                Token *token = &tokens->tokens[i];
                if (token->offset >= end) {
                    break;
                }
                if (token->kind == TOKEN_COMMENT) {
                    found_comment = true;
                } else if (!token_is_trivia(token)) {
                    _i63_result = false;
                    goto _i63_done;
                }
            }
            if (!found_comment) {
                _i63_result = false;
                goto _i63_done;
            }
            position = end + (end < b->start);
        }
        _i63_result = true;
        goto _i63_done;
        _i63_done: ;
    } while (false);
    /* Only comment-only intervening lines keep an assignment group intact.
     * A blank line, directive, or arbitrary code always splits the group.
     */
    return _i63_result;
}

/* A call-wrap candidate is one complete source statement whose value is a
 * direct, named invocation. No translation-unit AST is required. */

/* Save the invoked name and the first argument as token ranges, so
 * whitespace differences do not affect neighboring-call comparisons.
 * For ASSERT(!function(...)), compare the underlying function instead of
 * treating every assertion as a call to the same function.
 */
static bool
csane_wrap_first_arg(CsaneTokenView *view, int32 name, TokenRange *arg) {
    int32 open = csane_tokens_next(view, name);
    int32 close = csane_tokens_match(view, open);
    CsaneTokenView inside;
    TokenRange content = {open + 1, close};
    int32 comma;
    int32 first;

    if (close < 0 || !csane_tokens_subview(&inside, view, content)) {
        return false;
    }
    first = csane_tokens_next(&inside, open);
    if (first >= close) {
        return false;
    }
    comma = csane_tokens_top_level(&inside, open, STRLIT(","));
    if (comma < 0) {
        return false;
    }
    *arg = (TokenRange){first, comma};
    return true;
}

static bool
csane_wrap_local_span(CsaneFile *file, TokenRange range, CsaneWrapSpan *span) {
    int32 in_result_call;
    Tokenization *wrap_tokens = file->tokens;
    int32 first = token_range_first_significant(wrap_tokens, range);
    int32 last = token_range_last_significant(wrap_tokens, range);
    int32 name;
    char *text;
    int32 first_offset;
    int32 line_start;
    int32 call_close_2;
    int32 line_end;

    if (first < 0 || last < first) {
        return false;
    }
    do {
        Tokenization *tokens = file->tokens;
        CsaneTokenView view = csane_tokens_full(tokens);
        int32 nested_first = csane_tokens_next(&view, range.first - 1);
        int32 nested_last = token_range_last_significant(tokens, range);
        int32 nested_name;
        int32 open;
        int32 close;
        enum CsaneLocalKind kind;

        if (nested_first < 0 || nested_last < nested_first
            || !csane_wrap_token_is(&tokens->tokens[nested_last],
                                    STRLIT(";"))) {
            in_result_call = -1;
            break;
        }
        nested_name = nested_first;
        if (csane_wrap_token_is(&tokens->tokens[nested_name],
                                STRLIT("return"))) {
            nested_name = csane_tokens_next(&view, nested_name);
        } else {
            /* A declaration initializer or plain assignment may precede the
             * invocation. Ignore operators nested within delimiters. */
            int32 equals = -1;
            for (int32 i = nested_first; i < nested_last; i += 1) {
                if (csane_wrap_token_is(&tokens->tokens[i], STRLIT("="))) {
                    equals = i;
                } else if (csane_wrap_token_is(&tokens->tokens[i], STRLIT("("))
                           || csane_wrap_token_is(&tokens->tokens[i],
                                                  STRLIT("["))) {
                    int32 match = csane_tokens_match(&view, i);
                    if (match > i && match < nested_last) {
                        i = match;
                    }
                }
            }
            if (equals >= 0) {
                nested_name = csane_tokens_next(&view, equals);
            }
        }
        if (nested_name < 0 || nested_name >= nested_last
            || tokens->tokens[nested_name].kind != TOKEN_IDENT
            || csane_wrap_token_is(&tokens->tokens[nested_name],
                                   STRLIT("error_impl"))
            || csane_wrap_token_is(&tokens->tokens[nested_name],
                                   STRLIT("assert_error"))) {
            in_result_call = -1;
            break;
        }
        open = csane_tokens_next(&view, nested_name);
        if (open < 0 || open >= nested_last
            || !csane_wrap_token_is(&tokens->tokens[open], STRLIT("("))) {
            in_result_call = -1;
            break;
        }
        close = csane_tokens_match(&view, open);
        if (close < 0 || close >= nested_last
            || csane_tokens_next(&view, close) != nested_last) {
            in_result_call = -1;
            break;
        }
        kind = csane_classify_invocation(tokens, range, nested_name);
        /* Capitalized function names resemble type names to the heuristic
         * classifier. A complete call statement has no type declarator here.
         */
        if (kind == CSANE_LOCAL_UNKNOWN
            && csane_name_kind(&tokens->tokens[nested_name])
                == CSANE_NAME_TYPE) {
            in_result_call = nested_name;
            break;
        }
        in_result_call = kind == CSANE_LOCAL_CALL
               || kind == CSANE_LOCAL_MACRO_INVOCATION ? nested_name : -1;
    } while (false);
    name = in_result_call;
    if (name < 0 || csane_wrap_location(wrap_tokens, name).line
        != csane_wrap_location(wrap_tokens, first).line) {
        return false;
    }
    text = wrap_tokens->text;
    first_offset = wrap_tokens->tokens[first].offset;
    line_start = csane_wrap_line_start(text, first_offset);
    if (line_start == first_offset) {
        return false;
    }
    for (int32 i = line_start; i < first_offset; i += 1) {
        if (text[i] != ' ') {
            return false;
        }
    }
    do {
        Tokenization *tokens = file->tokens;
        CsaneTokenView view = csane_tokens_full(tokens);
        TokenRange first_arg = {0};

        span->call_name = name;
        if (!csane_wrap_first_arg(&view, name, &first_arg)) {
            span->first_arg = first_arg;
            break;
        }
        if (csane_wrap_token_is(&tokens->tokens[name], STRLIT("ASSERT"))) {
            for (int32 i = first_arg.first; i < first_arg.end;
                 i = csane_tokens_next(&view, i)) {
                Token *token = &tokens->tokens[i];
                int32 next = csane_tokens_next(&view, i);
                int32 close;

                if (token->kind != TOKEN_IDENT || next >= first_arg.end
                    || !csane_wrap_token_is(&tokens->tokens[next],
                                            STRLIT("("))) {
                    continue;
                }
                close = csane_tokens_match(&view, next);
                if (close < 0 || close >= first_arg.end) {
                    continue;
                }
                span->call_name = i;
                (void)csane_wrap_first_arg(&view, i, &first_arg);
                break;
            }
        }
        span->first_arg = first_arg;
    } while (false);
    span->start = line_start;
    span->end =
        wrap_tokens->tokens[last].offset + wrap_tokens->tokens[last].len;
    span->indent = first_offset - line_start;
    span->start_line = csane_wrap_location(wrap_tokens, first).line;
    span->end_line = csane_wrap_location(wrap_tokens, last).line;
    /* Perl inspects the entire physical call statement and requires the
     * semicolon to be on the same line as its closing parenthesis. A second
     * statement or trailing comment also makes the candidate ineligible.
     */
    call_close_2 = tokenization_previous_significant(wrap_tokens, last);
    if (call_close_2 < 0 || csane_wrap_location(wrap_tokens, call_close_2).line
        != span->end_line) {
        return false;
    }
    line_end = span->end;
    while (line_end < wrap_tokens->text_len && text[line_end] != '\n') {
        if (text[line_end] != ' ' && text[line_end] != '\t'
            && text[line_end] != '\r') {
            return false;
        }
        line_end += 1;
    }
    span->end = line_end;
    span->wrapped = span->end_line > span->start_line;
    span->fits = false;
    if (csane_wrap_safe(file, first, last, false)) {
        int32 width = csane_wrap_flat_length(text, span->start, span->end,
                                             file->column_limit);
        span->fits = width >= 0 && (file->column_limit <= 0
                                    || width <= file->column_limit);
    }
    return true;
}

/* Gather candidates once, irrespective of the statement tree's branch
 * representation. Different conditional streams may expose the same original
 * statement more than once; source offsets identify those duplicates.
 */

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

void
csane_wrap_call_check(CsaneFile *file) {
    int32 in_result_calls;
    int32 capacity = 0;
    CsaneWrapSpan *items;
    int32 count;
    int32 unique = 0;

    if (csane_wrap_generated(file->path, file->path_len)) {
        return;
    }
    if (file->statements != NULL) {
        capacity += ARRAY_LEN(file->statements->items);
    }
    if (file->toplevel != NULL) {
        capacity += ARRAY_LEN(file->toplevel->items);
    }
    if (capacity == 0) {
        return;
    }
    items = malloc2(capacity*SIZEOF(*items));
    {
        int32 used = 0;
        if (file->statements != NULL) {
            CsaneStatements *tree = file->statements;
            for (int32 i = 0; i < ARRAY_LEN(tree->items); i += 1) {
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
            for (int32 i = 0; i < ARRAY_LEN(top->items); i += 1) {
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
        in_result_calls = used;
    }
    count = in_result_calls;
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
        while (true) {
            bool _i64_predicate;
            _i64_predicate = end < unique;
            if (_i64_predicate) {
                _i64_predicate = csane_wrap_adjacent(file, &items[end - 1],
                                                     &items[end],
                                                     CSANE_WRAP_CALL);
            }
            if (_i64_predicate) {
                bool _i64_result;
                do {
                    CsaneWrapSpan *_i64_a = &items[end - 1];
                    CsaneWrapSpan *_i64_b = &items[end];

                    Tokenization *tokens = file->tokens;
                    Token *left = &tokens->tokens[_i64_a->call_name];
                    Token *right = &tokens->tokens[_i64_b->call_name];
                    CsaneTokenView view;
                    int32 i;
                    int32 j;

                    if (left->len == right->len
                        && !memcmp64(left->text, right->text, left->len)) {
                        _i64_result = true;
                        goto _i64_done;
                    }
                    if (_i64_a->first_arg.first >= _i64_a->first_arg.end
                        || _i64_b->first_arg.first >= _i64_b->first_arg.end) {
                        _i64_result = false;
                        goto _i64_done;
                    }
                    view = csane_tokens_full(tokens);
                    i = _i64_a->first_arg.first;
                    j = _i64_b->first_arg.first;
                    while (i < _i64_a->first_arg.end
                        && j < _i64_b->first_arg.end) {
                        left = &tokens->tokens[i];
                        right = &tokens->tokens[j];
                        if (left->len != right->len
                            || memcmp64(left->text, right->text, left->len)) {
                            _i64_result = false;
                            goto _i64_done;
                        }
                        i = csane_tokens_next(&view, i);
                        j = csane_tokens_next(&view, j);
                    }
                    _i64_result = i >= _i64_a->first_arg.end
                        && j >= _i64_b->first_arg.end;
                    goto _i64_done;
                    _i64_done: ;
                } while (false);
                _i64_predicate = _i64_result;
            }
            if (!_i64_predicate) {
                break;
            }
            end += 1;
        }
        do {
            CsaneWrapSpan *in_items = items;

            bool all_fit = true;
            for (int32 i = first; i < end; i += 1) {
                all_fit = all_fit && in_items[i].fits;
            }
            if (!all_fit) {
                break;
            }
            for (int32 i = first; i < end; i += 1) {
                if (in_items[i].wrapped) {
                    csane_rule_issue(file, in_items[i].start,
                                     STRLIT("FMT-CALL-WRAP"),
                                     "call fits on one line but is "
                                     "split across multiple lines");
                }
            }
        } while (false);
        first = end;
    }
    if (file->statements != NULL) {
        Tokenization *tokens = file->tokens;
        CsaneTokenView view = csane_tokens_full(tokens);

        for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
            CsaneStatement *stmt = &file->statements->items[i];
            int32 first;
            int32 last;
            int32 open;
            int32 close;
            int32 start;
            int32 end;
            int32 close_end;
            int32 close_line;
            bool duplicate = false;
            bool wrapped_call = false;
            bool suffix_safe = true;
            bool brace = false;

            if (stmt->kind != CSANE_STMT_IF
                && stmt->kind != CSANE_STMT_FOR
                && stmt->kind != CSANE_STMT_WHILE
                && stmt->kind != CSANE_STMT_SWITCH) {
                continue;
            }
            for (int32 j = 0; j < i; j += 1) {
                CsaneStatement *previous = &file->statements->items[j];

                if (previous->header.first == stmt->header.first
                    && previous->header.end == stmt->header.end) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            first = token_range_first_significant(tokens, stmt->header);
            last = token_range_last_significant(tokens, stmt->header);
            if (first < 0 || last <= first) {
                continue;
            }
            open = csane_tokens_next(&view, first);
            close = csane_tokens_match(&view, open);
            if (open < 0 || close < 0 || close != last) {
                continue;
            }
            for (int32 j = csane_tokens_next(&view, open); j < close;
                 j = csane_tokens_next(&view, j)) {
                Token *token = &tokens->tokens[j];
                int32 call_open;
                int32 call_close;
                enum CsaneLocalKind kind;

                if (token->kind != TOKEN_IDENT) {
                    continue;
                }
                call_open = csane_tokens_next(&view, j);
                if (call_open >= close
                    || !csane_wrap_token_is(&tokens->tokens[call_open],
                                            STRLIT("("))) {
                    continue;
                }
                call_close = csane_tokens_match(&view, call_open);
                if (call_close < 0 || call_close > close) {
                    continue;
                }
                kind = csane_classify_invocation(tokens, stmt->header, j);
                if ((kind == CSANE_LOCAL_CALL
                     || kind == CSANE_LOCAL_MACRO_INVOCATION)
                    && csane_wrap_location(tokens, j).line
                       != csane_wrap_location(tokens, call_close).line) {
                    wrapped_call = true;
                    break;
                }
            }
            if (!wrapped_call
                || !csane_wrap_safe(file, first, last, true)) {
                continue;
            }
            start = csane_wrap_line_start(tokens->text,
                                          tokens->tokens[first].offset);
            for (int32 j = start; j < tokens->tokens[first].offset; j += 1) {
                if (tokens->text[j] != ' ') {
                    suffix_safe = false;
                    break;
                }
            }
            close_end = tokens->tokens[close].offset
                        + tokens->tokens[close].len;
            close_line = csane_wrap_location(tokens, close).line;
            end = tokenization_physical_line_end_offset(tokens, close_line);
            if (end > close_end && tokens->text[end - 1] == '\n') {
                end -= 1;
            }
            for (int32 j = close_end; suffix_safe && j < end; j += 1) {
                char c = tokens->text[j];

                if (c == ' ' || c == '\t' || c == '\r') {
                    continue;
                }
                if (c == '{' && !brace) {
                    brace = true;
                    continue;
                }
                suffix_safe = false;
            }
            if (suffix_safe) {
                int32 width = csane_wrap_flat_length(tokens->text, start, end,
                                                     file->column_limit);

                if (width >= 0 && (file->column_limit <= 0
                                   || width <= file->column_limit)) {
                    csane_rule_issue(file, start, STRLIT("FMT-CALL-WRAP"),
                                     "call fits on one line but is "
                                     "split across multiple lines");
                }
            }
        }
    }
    free2(items, capacity*SIZEOF(*items));
    return;
}

/* A break after any assignment operator is unnecessary if the complete
 * statement fits on one line. Comparisons and other operators do not count. */
static bool
csane_wrap_assignment_operator(Token *token) {
    return csane_wrap_token_is(token, STRLIT("="))
           || csane_wrap_token_is(token, STRLIT("+="))
           || csane_wrap_token_is(token, STRLIT("-="))
           || csane_wrap_token_is(token, STRLIT("*="))
           || csane_wrap_token_is(token, STRLIT("/="))
           || csane_wrap_token_is(token, STRLIT("%="))
           || csane_wrap_token_is(token, STRLIT("&="))
           || csane_wrap_token_is(token, STRLIT("|="))
           || csane_wrap_token_is(token, STRLIT("^="))
           || csane_wrap_token_is(token, STRLIT("<<="))
           || csane_wrap_token_is(token, STRLIT(">>="));
}

/* Assignment wrapping uses direct, top-level assignment operators in simple
 * statements. Nested assignments in conditions are excluded by the statement
 * boundary and token-spelling checks. */

static bool
csane_wrap_local_assignment_span(CsaneFile *file, TokenRange range,
                                 CsaneWrapSpan *span, bool require_indent) {
    int32 after;
    bool braced = false;
    bool trailing_comment = false;
    int32 line_end;
    int32 end;
    int32 start;
    int32 offset;
    char *text;
    int32 op;
    Tokenization *tokens = file->tokens;
    int32 first = token_range_first_significant(tokens, range);
    int32 last = token_range_last_significant(tokens, range);
    int32 _i65_result;
    do {
        Tokenization *nested_tokens = file->tokens;
        CsaneTokenView view;
        int32 nested_first_2;
        int32 nested_last_2;
        int32 equals = -1;

        if (!csane_tokens_view(&view, nested_tokens, range)) {
            _i65_result = -1;
            goto _i65_done;
        }
        nested_first_2 = token_range_first_significant(nested_tokens, range);
        nested_last_2 = token_range_last_significant(nested_tokens, range);
        if (nested_first_2 < 0 || nested_last_2 <= nested_first_2
            || !csane_wrap_token_is(&nested_tokens->tokens[nested_last_2],
                                    STRLIT(";"))) {
            _i65_result = -1;
            goto _i65_done;
        }
        for (int32 i = nested_first_2; i < nested_last_2; i += 1) {
            Token *token = &nested_tokens->tokens[i];
            if (csane_wrap_assignment_operator(token)) {
                if (equals >= 0) {
                    _i65_result = -1;
                    goto _i65_done;
                }
                equals = i;
            } else if (csane_wrap_token_is(token, STRLIT("("))
                       || csane_wrap_token_is(token, STRLIT("["))
                       || csane_wrap_token_is(token, STRLIT("{"))) {
                int32 close = csane_tokens_match(&view, i);
                if (close > i && close < nested_last_2) {
                    i = close;
                }
            }
        }
        _i65_result = equals;
        goto _i65_done;
        _i65_done: ;
    } while (false);
    op = _i65_result;

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
        for (int32 j = last + 1; j < ARRAY_LEN(tokens->tokens); j += 1) {
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
    span->compound = !csane_wrap_token_is(&tokens->tokens[op], STRLIT("="));
    span->fits = false;
    /* A locally recognized type name need not be declared in this file.
     * Preserve the legacy conservative grouping for such declarations. */
    if (csane_name_kind(&tokens->tokens[first]) == CSANE_NAME_TYPE) {
        return true;
    }
    if (!braced && !trailing_comment
        && csane_wrap_safe(file, first, last, false)) {
        int32 width = csane_wrap_flat_length(text, start, span->end,
                                             file->column_limit);
        span->fits = width >= 0 && (file->column_limit <= 0
                                    || width <= file->column_limit);
    }
    return true;
}

/* Existing source-preserving statement and top-level scanners supply the
 * majority of assignments. Do not trust their ordering or branch ownership:
 * a conditional source stream may yield the same assignment twice. */

/* Preprocessor branches can interrupt an otherwise incomplete statement.
 * Recover independent assignments after such gaps directly from original
 * tokens; this does not parse or evaluate a conditional branch. */

void
csane_wrap_check(CsaneFile *file) {
    int32 in_result_gaps;
    int32 in_result_assignments;
    int32 capacity;
    CsaneWrapSpan *items;
    int32 count;
    int32 unique = 0;

    if (file->is_ryu || csane_wrap_generated(file->path, file->path_len)) {
        return;
    }
    capacity = ARRAY_LEN(file->tokens->tokens);
    if (file->statements != NULL) {
        capacity += ARRAY_LEN(file->statements->items);
    }
    if (file->toplevel != NULL) {
        capacity += ARRAY_LEN(file->toplevel->items);
    }
    if (capacity == 0) {
        return;
    }
    items = malloc2(capacity*SIZEOF(*items));
    {
        int32 used = 0;
        if (file->toplevel != NULL) {
            CsaneTopLevels *top = file->toplevel;
            for (int32 i = 0; i < ARRAY_LEN(top->items); i += 1) {
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
            for (int32 i = 0; i < ARRAY_LEN(tree->items); i += 1) {
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
        in_result_assignments = used;
    }
    count = in_result_assignments;
    do {
        CsaneFile *in_file = file;
        int32 in_used = count;

        Tokenization *tokens = in_file->tokens;
        for (int32 op = 0; op < ARRAY_LEN(tokens->tokens); op += 1) {
            int32 start;
            int32 first;
            int32 end;
            CsaneWrapSpan span;
            TokenRange range;

            if (!csane_wrap_assignment_operator(&tokens->tokens[op])) {
                continue;
            }
            if (in_file->preprocessor->token_roles != NULL
                && in_file->preprocessor->token_roles[op] != C_PP_TOKEN_CODE) {
                continue;
            }
            start =
                csane_wrap_line_start(tokens->text, tokens->tokens[op].offset);
            first = op;
            while (first > 0 && tokens->tokens[first - 1].offset >= start) {
                first -= 1;
            }
            end = op + 1;
            while (end < ARRAY_LEN(tokens->tokens)) {
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
            if (end <= op + 1 || end > ARRAY_LEN(tokens->tokens)
                || !csane_wrap_token_is(&tokens->tokens[end - 1],
                                        STRLIT(";"))) {
                continue;
            }
            range = (TokenRange){first, end};
            if (csane_wrap_local_assignment_span(in_file, range,
                                                 &span, false)) {
                items[in_used] = span;
                in_used += 1;
            }
        }
        in_result_gaps = in_used;
    } while (false);
    count = in_result_gaps;
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
        do {
            CsaneWrapSpan *in_items = &items[first];
            int32 in_count = end - first;

            bool all_fit = true;
            for (int32 i = 0; i < in_count; i += 1) {
                all_fit = all_fit && in_items[i].fits;
            }
            for (int32 i = 0; i < in_count; i += 1) {
                if (in_items[i].wrapped && in_items[i].fits
                    && (all_fit || in_items[i].compound)) {
                    csane_rule_issue(file, in_items[i].start,
                                     STRLIT("FMT-ASSIGN-WRAP"),
                                     "assignment fits on one line "
                                     "but is split after its operator");
                }
            }
        } while (false);
        first = end;
    }
    free2(items, capacity*SIZEOF(*items));
    return;
}

#endif /* CSANECHECK_WRAP_C */
