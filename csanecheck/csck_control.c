// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONTROL_C)
#define CSANECHECK_CONTROL_C

#include "cbase.h"
#include "csck.h"

static int32
csane_control_first(CsaneFile *file, CsaneStatement *stmt) {
    int32 index = token_range_first_significant(file->tokens, stmt->tokens);
    return index < stmt->tokens.end ? index : -1;
}

static void
csane_control_issue(CsaneFile *file, CsaneStatement *stmt,
                    char *rule, int32 rule_len, char *message) {
    int32 token = csane_control_first(file, stmt);
    if (token >= 0) {
        csane_rule_issue(file, file->tokens->tokens[token].offset,
                         rule, rule_len, message);
    }
}

static bool
csane_control_block(CsaneFile *file, int32 index) {
    CsaneStatement *stmt;
    int32 first;

    if (index < 0 || index >= ARRAY_LEN(file->statements->items)) {
        return false;
    }
    stmt = &file->statements->items[index];
    if (stmt->kind == CSANE_STMT_BLOCK) {
        return true;
    }
    /* An unmatched closing brace can make a block OPAQUE, but its opening
     * token still establishes that curly braces were present. */
    first = csane_control_first(file, stmt);
    return stmt->kind == CSANE_STMT_OPAQUE && first >= 0
           && TOKEN_IS(&file->tokens->tokens[first], "{");
}

static bool
csane_control_for_empty(CsaneFile *file, int32 index) {
    CsaneStatement *body;
    int32 first;

    if (index < 0 || index >= ARRAY_LEN(file->statements->items)) {
        return false;
    }
    body = &file->statements->items[index];
    first = csane_control_first(file, body);
    return body->kind == CSANE_STMT_SIMPLE && first >= 0
           && TOKEN_IS(&file->tokens->tokens[first], ";");
}


/* Case labels in #if branches are parsed in a separate source stream.
 * Locate the nearest containing switch by original source span when no
 * statement-parent relationship is available. */


/* Skip tokens in mutually exclusive branches when recovering the source
 * context of a standalone block. Tokens in the containing file stream are
 * visible to every branch. */
static int32
csane_control_previous(CsaneFile *file, CsaneStatement *stmt, int32 before) {
    CsaneTopItem *top = &file->toplevel->items[stmt->function];
    int32 base = top->owner_region;
    for (int32 i = before - 1; i >= 0; i -= 1) {
        Token *token = &file->tokens->tokens[i];
        int32 region;

        if (token->offset < top->source.start) {
            break;
        }
        if (token_is_trivia(token) || token->kind == TOKEN_PREPROC) {
            continue;
        }
        region = csane_regions_at_token(file->regions, i);
        if (region < 0
            || file->regions->items[region].kind != CSANE_REGION_CODE) {
            continue;
        }
        while (region >= 0
               && file->regions->items[region].kind != CSANE_REGION_FILE
               && file->regions->items[region].kind
                  != CSANE_REGION_CONDITIONAL_BRANCH) {
            region = file->regions->items[region].parent;
        }
        if (region == stmt->owner_region || region == base) {
            return i;
        }
    }
    return -1;
}

/* A control header can appear in either branch just before #endif,
 * followed by a shared block. Consult that last branch token only for the
 * formatting exception; do not merge branch statement streams. */

/* For a block detached from its surrounding statements by a directive,
 * recover only the preceding header/initializer clue. No macro expansion or
 * translation-unit type information is required. */


/* Only a comparison at the outermost expression level qualifies. Nested
 * comparisons, compound boolean conditions, and ternaries remain outside
 * this deliberately narrow formatting rule. */
static bool
csane_control_comparator(Token *token) {
    return TOKEN_IS(token, "==") || TOKEN_IS(token, "!=")
           || TOKEN_IS(token, "<") || TOKEN_IS(token, "<=")
           || TOKEN_IS(token, ">") || TOKEN_IS(token, ">=");
}


/* A single predicate or assignment can also be joined. Preserve compound
 * boolean conditions and ternaries even when their flattened form fits: those
 * breaks may communicate the logical structure of the expression. */

/* The source line may begin with indentation or with the closing brace and
 * else preceding an else-if. Do not join an unrelated statement or comment. */

/* Require the closing brace on the same physical line as the header, and
 * no trailing comment or other statement after it. */


/* Within an if condition, prefer starting a call on the assignment line
 * and wrapping its arguments. Only require this when the first argument and
 * every aligned continuation fit the configured column limit. */
static void
csane_control_if_assignment_wrap(CsaneFile *file, CsaneStatement *stmt) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    CsaneTokenView args;
    int32 first;
    int32 last;
    int32 open;
    int32 close;
    int32 assignment = -1;
    int32 name;
    int32 call_open;
    int32 call_close;
    int32 first_arg;
    int32 arg_column;
    int32 line_start;
    int32 width;

    if (stmt->kind != CSANE_STMT_IF || file->column_limit <= 0
        || !csane_tokens_view(&view, tokens, stmt->header)) {
        return;
    }
    first = token_range_first_significant(tokens, stmt->header);
    last = token_range_last_significant(tokens, stmt->header);
    if (first < 0 || last <= first) {
        return;
    }
    open = csane_tokens_next(&view, first);
    if (open >= last || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return;
    }
    close = csane_tokens_match(&view, open);
    if (close != last || !csane_wrap_safe(file, first, close, true)) {
        return;
    }
    for (int32 i = open + 1; i < close; i += 1) {
        if (!TOKEN_IS(&tokens->tokens[i], "=")) {
            continue;
        }
        if (assignment >= 0) {
            return;
        }
        assignment = i;
    }
    if (assignment < 0) {
        return;
    }
    name = csane_tokens_next(&view, assignment);
    if (name >= close || tokens->tokens[name].kind != TOKEN_IDENT
        || tokenization_token_location(tokens, assignment).line
           == tokenization_token_location(tokens, name).line) {
        return;
    }
    call_open = csane_tokens_next(&view, name);
    if (call_open >= close || !TOKEN_IS(&tokens->tokens[call_open], "(")) {
        return;
    }
    call_close = csane_tokens_match(&view, call_open);
    if (call_close < 0 || call_close >= close
        || !csane_tokens_view(&args, tokens,
                              (TokenRange){call_open + 1, call_close})) {
        return;
    }
    first_arg = csane_tokens_next(&args, call_open);
    if (first_arg >= call_close) {
        return;
    }
    line_start = tokens->tokens[first].offset;
    while (line_start > 0 && tokens->text[line_start - 1] != '\n') {
        line_start -= 1;
    }
    /* A header that fits entirely is already handled by FMT-IF-WRAP. */
    {
        SourceLocation location = tokenization_token_location(tokens, close);
        int32 line_end = tokenization_physical_line_end_offset(tokens,
                                                               location.line);
        while (line_end > line_start
               && (tokens->text[line_end - 1] == '\n'
                   || tokens->text[line_end - 1] == '\r')) {
            line_end -= 1;
        }
        int32 full_width = csane_wrap_flat_length(tokens->text, line_start,
                                                  line_end,
                                                  file->column_limit);

        if (full_width >= 0 && full_width <= file->column_limit) {
            return;
        }
    }
    arg_column = csane_wrap_flat_length(tokens->text, line_start,
                                        tokens->tokens[first_arg].offset,
                                        file->column_limit);
    if (arg_column < 0 || arg_column >= file->column_limit) {
        return;
    }
    for (int32 arg = first_arg; arg < call_close;) {
        int32 comma = csane_tokens_top_level(&args, arg - 1, STRLIT(","));
        int32 end;
        int32 start = arg == first_arg ? line_start
                                      : tokens->tokens[arg].offset;

        if (comma < 0) {
            return;
        }
        end = comma < call_close ? tokens->tokens[comma].offset + 1
                                 : tokens->tokens[close].offset
                                       + tokens->tokens[close].len;
        width = csane_wrap_flat_length(tokens->text, start, end,
                                       file->column_limit);
        if (width < 0 || (arg != first_arg && width + arg_column
                                             > file->column_limit)
            || (arg == first_arg && width > file->column_limit)) {
            return;
        }
        if (comma == call_close) {
            break;
        }
        arg = csane_tokens_next(&args, comma);
        if (arg >= call_close) {
            return;
        }
    }
    csane_rule_issue(file, tokens->tokens[assignment].offset,
                     STRLIT("FMT-IF-ASSIGN-WRAP"),
                     "wrap call arguments before breaking after = in if");
}

void
csane_control_check(CsaneFile *file) {
    if (file->statements == NULL || file->is_ryu
        || ENDS_WITH(file->path, file->path_len, ".meta.h")) {
        return;
    }
    for (int32 stmt_i =
        0; stmt_i < ARRAY_LEN(file->statements->items); stmt_i += 1) {
        CsaneStatement *stmt = &file->statements->items[stmt_i];
        do {
            CsaneFile *in_file = file;
            CsaneStatement *in_stmt = stmt;

            bool control = in_stmt->kind == CSANE_STMT_IF
                           || in_stmt->kind == CSANE_STMT_FOR
                           || in_stmt->kind == CSANE_STMT_WHILE
                           || in_stmt->kind == CSANE_STMT_DO
                           || in_stmt->kind == CSANE_STMT_SWITCH;
            CsaneStatement *alternative;
            CsaneStatement *body;

            /* Even an opaque body is definitely unbraced if its first token is
             * not '{'. An unrecognized expression must not hide this
             * violation. */
            if (control && in_stmt->body >= 0
                && !csane_control_block(in_file, in_stmt->body)
                && !(in_stmt->kind == CSANE_STMT_FOR
                     && csane_control_for_empty(in_file, in_stmt->body))) {
                csane_control_issue(in_file, in_stmt, STRLIT("AST-BRACES"),
                                    "control body must use curly braces");
            }
            if (in_stmt->kind != CSANE_STMT_IF || in_stmt->alternative < 0) {
                break;
            }
            alternative = &in_file->statements->items[in_stmt->alternative];
            if (alternative->kind != CSANE_STMT_ELSE || alternative->body < 0) {
                break;
            }
            body = &in_file->statements->items[alternative->body];
            if (csane_control_block(in_file, alternative->body)
                || body->kind == CSANE_STMT_IF) {
                break;
            }
            csane_control_issue(in_file, alternative, STRLIT("AST-BRACES"),
                                "else body must use curly braces");
        } while (false);
        do {
            int32 in_result_switch;
            int32 parent;
            int32 label_token;
            CsaneStatement *sw;
            int32 switch_token;
            int32 label_column;
            int32 switch_column;

            if (stmt->kind != CSANE_STMT_CASE
                && stmt->kind != CSANE_STMT_DEFAULT) {
                goto _i16_done;
            }
            do {
                CsaneFile *in_file = file;
                CsaneStatement *in_label = stmt;

                int32 closest = -1;
                for (int32 i =
                    0; i < ARRAY_LEN(in_file->statements->items); i += 1) {
                    CsaneStatement *candidate = &in_file->statements->items[i];
                    if (candidate->kind != CSANE_STMT_SWITCH
                        || candidate->function != in_label->function
                        || candidate->source.start >= in_label->source.start
                        || candidate->source.end <= in_label->source.start) {
                        continue;
                    }
                    if (closest < 0
                        || candidate->source.start
                           > in_file->statements->items[closest].source.start) {
                        closest = i;
                    }
                }
                in_result_switch = closest;
            } while (false);
            parent = in_result_switch;
            if (parent < 0) {
                goto _i16_done;
            }
            label_token = csane_control_first(file, stmt);
            sw = &file->statements->items[parent];
            switch_token = csane_control_first(file, sw);
            if (label_token < 0 || switch_token < 0) {
                goto _i16_done;
            }
            label_column = tokenization_token_location(file->tokens,
                                                       label_token).column;
            switch_column = tokenization_token_location(file->tokens,
                                                        switch_token).column;
            if (label_column != switch_column) {
                csane_control_issue(file, stmt, STRLIT("AST-SWITCH-ALIGN"),
                                    "case/default must align with switch");
            }
            _i16_done: ;
        } while (false);
        do {
            bool _i18_result;
            bool _i17_predicate;
            int32 guard_token;
            SourceLocation loc;
            int32 offset;
            char *text;
            int32 start;

            if (stmt->kind != CSANE_STMT_BLOCK) {
                goto _i19_done;
            }
            if (stmt->parent >= 0) {
                CsaneStatement *parent = &file->statements->items[stmt->parent];
                if (parent->kind != CSANE_STMT_BLOCK) {
                    goto _i19_done;
                }
                /* A block immediately after a case/default label is its body.
                 * */
                for (int32 i = parent->first_child; i >= 0;
                     i = file->statements->items[i].next_sibling) {
                    enum CsaneStatementKind kind;

                    if (file->statements->items[i].next_sibling
                        != (int32)(stmt - file->statements->items)) {
                        continue;
                    }
                    kind = file->statements->items[i].kind;
                    if (kind == CSANE_STMT_CASE || kind == CSANE_STMT_DEFAULT
                        || kind == CSANE_STMT_LABEL) {
                        goto _i19_done;
                    }
                    break;
                }
            } else {
                /* A root block may be a function-body fragment from a
                 * conditional
                  * branch. The ordinary function-body stream has no such roots.
                  * */
                int32 base = file->toplevel->items[stmt->function].owner_region;
                if (stmt->owner_region == base) {
                    goto _i19_done;
                }
            }
            do {
                int32 first = csane_control_first(file, stmt);
                int32 prev = csane_control_previous(file, stmt, first);
                Token *token;
                int32 before;

                if (prev < 0) {
                    _i18_result = false;
                    goto _i18_done;
                }
                token = &file->tokens->tokens[prev];
                if (TOKEN_IS(token, ")") || TOKEN_IS(token, "else")
                    || TOKEN_IS(token, "do") || TOKEN_IS(token, "=")
                    || TOKEN_IS(token, ",") || TOKEN_IS(token, "(")) {
                    _i18_result = true;
                    goto _i18_done;
                }
                if (TOKEN_IS(token, ":")) {
                    int32 line_start = token->offset;
                    char *source = file->tokens->text;
                    int32 length;

                    while (line_start > 0 && source[line_start - 1] != '\n') {
                        line_start -= 1;
                    }
                    while (source[line_start] == ' ') {
                        line_start += 1;
                    }
                    length = token->offset + token->len - line_start;
                    if (BEGINS_WITH(source + line_start, length, "case ")
                        || BEGINS_WITH(source + line_start, length,
                                       "default:")) {
                        _i18_result = true;
                        goto _i18_done;
                    }
                }
                before = csane_control_previous(file, stmt, prev);
                if (before >= 0 && (TOKEN_IS(&file->tokens->tokens[before],
                                             "struct")
                                    || TOKEN_IS(&file->tokens->tokens[before],
                                                "union")
                                    || TOKEN_IS(&file->tokens->tokens[before],
                                                "enum"))) {
                    _i18_result = true;
                    goto _i18_done;
                }
                _i18_result = TOKEN_IS(token, "struct")
                    || TOKEN_IS(token, "union")
                       || TOKEN_IS(token, "enum");
                goto _i18_done;
                _i18_done: ;
            } while (false);
            _i17_predicate = _i18_result;
            if (!_i17_predicate) {
                bool _i17_result;
                do {
                    int32 first = csane_control_first(file, stmt);
                    char *source;
                    int32 pos;
                    int32 line;

                    if (first < 0) {
                        _i17_result = false;
                        goto _i17_done;
                    }
                    source = file->tokens->text;
                    pos = file->tokens->tokens[first].offset;
                    while (pos > 0 && (source[pos - 1] == ' '
                                       || source[pos - 1] == '\t'
                                       || source[pos - 1] == '\n')) {
                        pos -= 1;
                    }
                    line = pos;
                    while (line > 0 && source[line - 1] != '\n') {
                        line -= 1;
                    }
                    while (line < pos && (source[line] == ' '
                                          || source[line] == '\t')) {
                        line += 1;
                    }
                    if (!BEGINS_WITH(source + line, pos - line, "#endif")) {
                        _i17_result = false;
                        goto _i17_done;
                    }
                    for (int32 i = first - 1; i >= 0; i -= 1) {
                        Token *token = &file->tokens->tokens[i];
                        int32 nested_start =
                            file->toplevel->items[stmt->function].source.start;
                        int32 region;

                        if (token->offset < nested_start) {
                            break;
                        }
                        region = csane_regions_at_token(file->regions, i);
                        if (region < 0
                            || file->regions->items[region].kind
                                != CSANE_REGION_CODE
                            || token_is_trivia(token)) {
                            continue;
                        }
                        _i17_result = TOKEN_IS(token, ")")
                            || TOKEN_IS(token, "else")
                               || TOKEN_IS(token, "do");
                        goto _i17_done;
                    }
                    _i17_result = false;
                    goto _i17_done;
                    _i17_done: ;
                } while (false);
                _i17_predicate = _i17_result;
            }
            if (_i17_predicate) {
                goto _i19_done;
            }
            guard_token = csane_control_first(file, stmt);
            if (guard_token < 0) {
                goto _i19_done;
            }
            loc = tokenization_token_location(file->tokens, guard_token);
            if (loc.column < 12) {
                goto _i19_done;
            }
            offset = file->tokens->tokens[guard_token].offset;
            text = file->tokens->text;
            start = offset;
            while (start > 0 && text[start - 1] != '\n') {
                start -= 1;
            }
            if (offset - start < 12) {
                goto _i19_done;
            }
            for (int32 i = 0; i < 12; i += 1) {
                if (text[start + i] != ' ') {
                    goto _i19_done;
                }
            }
            csane_control_issue(file, stmt, STRLIT("AST-ARTIFICIAL-BLOCK"),
                                "artificial block at nesting level 3 or more");
            _i19_done: ;
        } while (false);
        do {
            bool _i23_result;
            bool _i23_predicate;
            bool _i22_result;
            bool _i22_predicate;
            bool _i21_predicate;
            int32 _i24_index = stmt_i;

            Tokenization *tokens = file->tokens;
            CsaneTokenView view;
            int32 first;
            int32 last;
            int32 open;
            int32 close;
            int32 start;
            int32 end;
            int32 width;
            SourceLocation close_location;

            if (stmt->kind != CSANE_STMT_IF
                || !csane_tokens_view(&view, tokens, stmt->header)) {
                goto _i24_done;
            }
            for (int32 i = 0; i < _i24_index; i += 1) {
                CsaneStatement *previous = &file->statements->items[i];

                if (previous->kind == CSANE_STMT_IF
                    && previous->header.first == stmt->header.first
                    && previous->header.end == stmt->header.end) {
                    goto _i24_done;
                }
            }
            csane_control_if_assignment_wrap(file, stmt);
            first = token_range_first_significant(tokens, stmt->header);
            last = token_range_last_significant(tokens, stmt->header);
            if (first < 0 || last <= first) {
                goto _i24_done;
            }
            open = csane_tokens_next(&view, first);
            if (open >= last || !TOKEN_IS(&tokens->tokens[open], "(")) {
                goto _i24_done;
            }
            close = csane_tokens_match(&view, open);
            _i21_predicate = close != last;
            if (!_i21_predicate) {
                _i21_predicate = tokenization_token_location(tokens, open).line
                   == tokenization_token_location(tokens, close).line;
            }
            if (!_i21_predicate) {
                _i21_predicate = !csane_wrap_safe(file, first, last, true);
            }
            if (!_i21_predicate) {
                bool _i21_result;
                do {
                    Tokenization *nested_tokens = file->tokens;
                    CsaneTokenView nested_view;
                    TokenRange condition = {open + 1, close};
                    int32 assignments = 0;

                    bool _i20_predicate;
                    bool _i20_result;
                    do {
                        Tokenization *tokens2 = file->tokens;
                        CsaneTokenView view2;
                        TokenRange condition2 = {open + 1, close};
                        int32 comparison = -1;

                        if (!csane_tokens_view(&view2, tokens2, condition2)) {
                            _i20_result = false;
                            goto _i20_done;
                        }
                        condition2 = csane_tokens_unwrap(&view2, condition2);
                        for (int32 i =
                            condition2.first; i
                                < condition2.end; i += 1) {
                            Token *token = &tokens2->tokens[i];

                            if (TOKEN_IS(token, "&&") || TOKEN_IS(token, "||")
                                || TOKEN_IS(token, "?")
                                    || TOKEN_IS(token, ":")) {
                                _i20_result = false;
                                goto _i20_done;
                            }
                            if (csane_control_comparator(token)) {
                                if (comparison >= 0) {
                                    _i20_result = false;
                                    goto _i20_done;
                                }
                                comparison = i;
                            }
                        }
                        if (comparison < 0
                            || !csane_tokens_view(&view2, tokens2,
                                                  condition2)) {
                            _i20_result = false;
                            goto _i20_done;
                        }
                        for (int32 i = csane_tokens_next(&view2,
                                                         condition2.first - 1);
                             i < condition2.end; i =
                                 csane_tokens_next(&view2, i)) {
                            Token *token = &tokens2->tokens[i];

                            if (i == comparison) {
                                _i20_result = true;
                                goto _i20_done;
                            }
                            if (token_is_open_delimiter(token)) {
                                int32 match = csane_tokens_match(&view2, i);

                                if (match < 0) {
                                    _i20_result = false;
                                    goto _i20_done;
                                }
                                i = match;
                            }
                        }
                        _i20_result = false;
                        goto _i20_done;
                        _i20_done: ;
                    } while (false);
                    _i20_predicate = _i20_result;
                    if (_i20_predicate) {
                        _i21_result = true;
                        goto _i21_done;
                    }
                    if (!csane_tokens_view(&nested_view, nested_tokens,
                                           condition)) {
                        _i21_result = false;
                        goto _i21_done;
                    }
                    condition = csane_tokens_unwrap(&nested_view, condition);
                    for (int32 i = condition.first; i < condition.end; i += 1) {
                        Token *token = &nested_tokens->tokens[i];

                        if (csane_control_comparator(token)
                            || TOKEN_IS(token, "&&")
                            || TOKEN_IS(token, "||") || TOKEN_IS(token, "?")
                            || TOKEN_IS(token, ":")) {
                            _i21_result = false;
                            goto _i21_done;
                        }
                    }
                    if (!csane_tokens_view(&nested_view, nested_tokens,
                                           condition)) {
                        _i21_result = false;
                        goto _i21_done;
                    }
                    for (int32 i = csane_tokens_next(&nested_view,
                                                     condition.first - 1);
                         i < condition.end; i = csane_tokens_next(&nested_view,
                                                                  i)) {
                        Token *token = &nested_tokens->tokens[i];

                        if (TOKEN_IS(token, ",")) {
                            _i21_result = false;
                            goto _i21_done;
                        }
                        if (TOKEN_IS(token, "=")) {
                            assignments += 1;
                            if (assignments > 1) {
                                _i21_result = false;
                                goto _i21_done;
                            }
                        }
                        if (token_is_open_delimiter(token)) {
                            int32 match = csane_tokens_match(&nested_view, i);

                            if (match < 0) {
                                _i21_result = false;
                                goto _i21_done;
                            }
                            i = match;
                        }
                    }
                    _i21_result = true;
                    goto _i21_done;
                    _i21_done: ;
                } while (false);
                _i21_predicate = !_i21_result;
            }
            if (_i21_predicate) {
                goto _i24_done;
            }
            start = tokens->tokens[first].offset;
            while (start > 0 && tokens->text[start - 1] != '\n') {
                start -= 1;
            }
            do {
                char *_i22_text = tokens->text;
                int32 _i22_keyword = tokens->tokens[first].offset;

                int32 i = start;

                while (i < _i22_keyword && _i22_text[i] == ' ') {
                    i += 1;
                }
                if (i == _i22_keyword) {
                    _i22_result = true;
                    goto _i22_done;
                }
                if (_i22_text[i] == '}') {
                    i += 1;
                    while (i < _i22_keyword && _i22_text[i] == ' ') {
                        i += 1;
                    }
                }
                if (i + 4 >= _i22_keyword || memcmp64(_i22_text + i, "else", 4)
                    || _i22_text[i + 4] != ' ') {
                    _i22_result = false;
                    goto _i22_done;
                }
                i += 4;
                while (i < _i22_keyword && _i22_text[i] == ' ') {
                    i += 1;
                }
                _i22_result = i == _i22_keyword;
                goto _i22_done;
                _i22_done: ;
            } while (false);
            _i22_predicate = !_i22_result;
            if (_i22_predicate) {
                goto _i24_done;
            }
            close_location = tokenization_token_location(tokens, close);
            end = tokenization_physical_line_end_offset(tokens,
                                                        close_location.line);
            while (end > 0 && (tokens->text[end - 1] == '\n'
                               || tokens->text[end - 1] == '\r')) {
                end -= 1;
            }
            do {
                char *_i23_text = tokens->text;
                Token *close_token = &tokens->tokens[close];
                int32 _i23_close_end = close_token->offset + close_token->len;

                int32 i = _i23_close_end;

                while (i < end && (_i23_text[i] == ' '
                    || _i23_text[i] == '\r')) {
                    i += 1;
                }
                if (i >= end || _i23_text[i] != '{') {
                    _i23_result = false;
                    goto _i23_done;
                }
                i += 1;
                while (i < end && (_i23_text[i] == ' '
                    || _i23_text[i] == '\r')) {
                    i += 1;
                }
                _i23_result = i == end;
                goto _i23_done;
                _i23_done: ;
            } while (false);
            _i23_predicate = !_i23_result;
            if (_i23_predicate) {
                goto _i24_done;
            }
            width = csane_wrap_flat_length(tokens->text, start, end,
                                           file->column_limit);
            if (width >= 0 && (file->column_limit <= 0
                               || width <= file->column_limit)) {
                csane_rule_issue(file, tokens->tokens[first].offset,
                                 STRLIT("FMT-IF-WRAP"),
                                 "simple if header fits on one line");
            }
            _i24_done: ;
        } while (false);
    }
    csane_pointer_check(file);
    return;
}

#endif /* CSANECHECK_CONTROL_C */
