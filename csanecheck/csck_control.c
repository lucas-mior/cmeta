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
        int32 full_width;

        while (line_end > line_start
               && (tokens->text[line_end - 1] == '\n'
                   || tokens->text[line_end - 1] == '\r')) {
            line_end -= 1;
        }
        full_width = csane_wrap_flat_length(tokens->text, line_start,
                                            line_end, file->column_limit);

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

/* A flag assigned immediately before breaking out of a one-shot loop,
 * then tested solely to return, can be handled by returning at the point
 * of the assignment instead. Require a parsed sibling guard and a break
 * that actually exits this do-loop, not an inner loop or switch. */
static bool
csane_control_flag_return_loop(CsaneFile *file, int32 loop_index) {
    CsaneStatements *statements = file->statements;
    CsaneStatement *loop = &statements->items[loop_index];
    CsaneTopItem *function = &file->toplevel->items[loop->function];
    CsaneTokenView view;
    CsaneStatement *guard = NULL;
    CsaneStatement *block = NULL;
    CsaneStatement *returned = NULL;
    Token *name = NULL;
    int32 next;
    int32 opening;
    int32 closing;
    int32 condition;
    int32 first;
    int32 last;

    if (loop->body < 0
        || !csane_tokens_view(&view, file->tokens, function->body)) {
        return false;
    }
    next = csane_tokens_next(&view, loop->tokens.end - 1);
    for (int32 i = 0; i < ARRAY_LEN(statements->items); i += 1) {
        CsaneStatement *candidate = &statements->items[i];
        if (candidate->kind == CSANE_STMT_IF
            && candidate->tokens.first == next
            && candidate->function == loop->function
            && candidate->owner_region == loop->owner_region
            && candidate->parent == loop->parent) {
            guard = candidate;
            break;
        }
    }
    if (guard == NULL || guard->alternative >= 0 || guard->body < 0
        || !csane_tokens_view(&view, file->tokens, guard->header)) {
        return false;
    }
    opening = csane_tokens_next(&view, guard->header.first);
    closing = csane_tokens_match(&view, opening);
    condition = csane_tokens_next(&view, opening);
    if (!TOKEN_IS(&file->tokens->tokens[opening], "(")
        || closing < 0 || closing != csane_tokens_previous(&view,
                                                           view.range.end)) {
        return false;
    }
    if (TOKEN_IS(&file->tokens->tokens[condition], "!")) {
        condition = csane_tokens_next(&view, condition);
    }
    if (condition >= closing
        || file->tokens->tokens[condition].kind != TOKEN_IDENT
        || csane_tokens_next(&view, condition) != closing) {
        return false;
    }
    name = &file->tokens->tokens[condition];
    block = &statements->items[guard->body];
    if (block->kind != CSANE_STMT_BLOCK || block->first_child < 0
        || block->first_child != block->last_child) {
        return false;
    }
    returned = &statements->items[block->first_child];
    if (returned->kind != CSANE_STMT_SIMPLE
        || !csane_tokens_view(&view, file->tokens, returned->tokens)) {
        return false;
    }
    first = csane_tokens_next(&view, view.range.first - 1);
    last = csane_tokens_previous(&view, view.range.end);
    if (!TOKEN_IS(&file->tokens->tokens[first], "return")
        || !TOKEN_IS(&file->tokens->tokens[last], ";")) {
        return false;
    }
    /* Reject side-effecting return expressions: they are real work after
     * the loop. Accept 'return;', 'return result;' and 'return 0;'. */
    next = csane_tokens_next(&view, first);
    if (next != last) {
        if (TOKEN_IS(&file->tokens->tokens[next], "-")
            || TOKEN_IS(&file->tokens->tokens[next], "+")) {
            next = csane_tokens_next(&view, next);
        }
        if (csane_tokens_next(&view, next) != last) {
            return false;
        }
    }
    for (int32 i = 0; i < ARRAY_LEN(statements->items); i += 1) {
        CsaneStatement *item = &statements->items[i];
        CsaneStatement *branch = NULL;
        CsaneStatement *previous = NULL;
        CsaneTokenView assignment;
        int32 parent;
        int32 a;
        int32 b;

        if (item->kind != CSANE_STMT_SIMPLE || item->parent < 0
            || item->function != loop->function
            || item->owner_region != loop->owner_region
            || !csane_tokens_view(&view, file->tokens, item->tokens)) {
            continue;
        }
        first = csane_tokens_next(&view, view.range.first - 1);
        next = csane_tokens_next(&view, first);
        if (!TOKEN_IS(&file->tokens->tokens[first], "break")
            || !TOKEN_IS(&file->tokens->tokens[next], ";")
            || csane_tokens_next(&view, next) != view.range.end) {
            continue;
        }
        parent = item->parent;
        branch = &statements->items[parent];
        if (branch->kind != CSANE_STMT_BLOCK || branch->parent < 0
            || statements->items[branch->parent].kind != CSANE_STMT_IF
            || statements->items[branch->parent].body != parent) {
            continue;
        }
        /* The most recent breakable ancestor must be this do-loop. */
        for (int32 ancestor = parent; ancestor >= 0;
             ancestor = statements->items[ancestor].parent) {
            CsaneStatement *container = &statements->items[ancestor];
            if (ancestor == loop_index) {
                break;
            }
            if (container->kind == CSANE_STMT_DO
                || container->kind == CSANE_STMT_FOR
                || container->kind == CSANE_STMT_WHILE
                || container->kind == CSANE_STMT_SWITCH) {
                parent = -1;
                break;
            }
            if (container->parent < 0) {
                parent = -1;
            }
        }
        if (parent < 0) {
            continue;
        }
        for (int32 child = branch->first_child; child >= 0;
             child = statements->items[child].next_sibling) {
            if (child == i) {
                break;
            }
            previous = &statements->items[child];
        }
        if (previous == NULL || previous->next_sibling != i
            || previous->kind != CSANE_STMT_SIMPLE
            || !csane_tokens_view(&assignment, file->tokens,
                                  previous->tokens)) {
            continue;
        }
        a = csane_tokens_next(&assignment, assignment.range.first - 1);
        b = csane_tokens_next(&assignment, a);
        if (file->tokens->tokens[a].kind == TOKEN_IDENT
            && STREQUAL(file->tokens->tokens[a].text,
                        file->tokens->tokens[a].len, name->text, name->len)
            && TOKEN_IS(&file->tokens->tokens[b], "=")) {
            return true;
        }
    }
    return false;
}

/* A fixed member flag followed by return does not use a result from the
 * do-loop. Keep this narrow: calls, cleanup, and computed assignments can
 * perform actual shared work that must remain after a break. */
static bool
csane_control_final_fixed_member_set(CsaneFile *file, CsaneTokenView *view,
                                     int32 *next) {
    Tokenization *tokens = file->tokens;
    int32 cursor = *next;
    bool has_member = false;

    if (cursor >= view->range.end
        || tokens->tokens[cursor].kind != TOKEN_IDENT) {
        return false;
    }
    cursor = csane_tokens_next(view, cursor);
    while (cursor < view->range.end
           && (TOKEN_IS(&tokens->tokens[cursor], "->")
               || TOKEN_IS(&tokens->tokens[cursor], "."))) {
        has_member = true;
        cursor = csane_tokens_next(view, cursor);
        if (cursor >= view->range.end
            || tokens->tokens[cursor].kind != TOKEN_IDENT) {
            return false;
        }
        cursor = csane_tokens_next(view, cursor);
    }
    if (!has_member || cursor >= view->range.end
        || !TOKEN_IS(&tokens->tokens[cursor], "=")) {
        return false;
    }
    cursor = csane_tokens_next(view, cursor);
    if (cursor >= view->range.end
        || (!TOKEN_IS(&tokens->tokens[cursor], "true")
            && !TOKEN_IS(&tokens->tokens[cursor], "false")
            && !TOKEN_IS(&tokens->tokens[cursor], "0")
            && !TOKEN_IS(&tokens->tokens[cursor], "1"))) {
        return false;
    }
    cursor = csane_tokens_next(view, cursor);
    if (cursor >= view->range.end
        || !TOKEN_IS(&tokens->tokens[cursor], ";")) {
        return false;
    }
    *next = csane_tokens_next(view, cursor);
    return true;
}

/* A one-shot loop at the end of a function cannot provide shared work
 * after break. Neither a simple return nor a fixed flag set and return
 * require the artificial loop.
 * Inspect the original function tail so a conditional branch cannot make
 * later source statements disappear from the selected statement stream. */
void
csane_control_final_do_check(CsaneFile *file) {
    if (file->statements == NULL || file->toplevel == NULL) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        CsaneTopItem *function = NULL;
        CsaneTokenView view;
        int32 semicolon;
        int32 close;
        int32 zero;
        int32 open;
        int32 keyword;
        int32 end;
        int32 next;
        bool fixed_member_set = false;

        if (stmt->kind != CSANE_STMT_DO || stmt->function < 0
            || stmt->function >= ARRAY_LEN(file->toplevel->items)) {
            continue;
        }
        function = &file->toplevel->items[stmt->function];
        if (function->kind != CSANE_TOP_FUNCTION_DEFINITION
            || stmt->owner_region != function->owner_region
            || !csane_tokens_view(&view, file->tokens, stmt->tokens)) {
            continue;
        }
        semicolon = csane_tokens_previous(&view, view.range.end);
        close = csane_tokens_previous(&view, semicolon);
        zero = csane_tokens_previous(&view, close);
        open = csane_tokens_previous(&view, zero);
        keyword = csane_tokens_previous(&view, open);
        if (keyword < view.range.first
            || !TOKEN_IS(&file->tokens->tokens[semicolon], ";")
            || !TOKEN_IS(&file->tokens->tokens[close], ")")
            || !TOKEN_IS(&file->tokens->tokens[zero], "0")
            || !TOKEN_IS(&file->tokens->tokens[open], "(")
            || !TOKEN_IS(&file->tokens->tokens[keyword], "while")) {
            continue;
        }
        if (stmt->body >= 0) {
            if (csane_control_flag_return_loop(file, i)) {
                csane_control_issue(file, stmt,
                                    STRLIT("STYLE-FINAL-DO-WHILE-ZERO"),
                                    "do { ... } while (0) sets a flag before "
                                    "break solely for a following return; "
                                    "return directly instead");
                continue;
            }
        }
        if (stmt->parent >= 0
            || !csane_tokens_view(&view, file->tokens, function->body)) {
            continue;
        }
        end = csane_tokens_previous(&view, view.range.end);
        if (end < view.range.first
            || !TOKEN_IS(&file->tokens->tokens[end], "}")) {
            continue;
        }
        next = csane_tokens_next(&view, stmt->tokens.end - 1);
        if (next != end) {
            if (next < end
                && !TOKEN_IS(&file->tokens->tokens[next], "return")) {
                if (!csane_control_final_fixed_member_set(file, &view, &next)) {
                    continue;
                }
                fixed_member_set = true;
            }
            if (next >= end
                || !TOKEN_IS(&file->tokens->tokens[next], "return")) {
                continue;
            }
            next = csane_tokens_next(&view, next);
            if (next < end
                && !TOKEN_IS(&file->tokens->tokens[next], ";")) {
                next = csane_tokens_next(&view, next);
            }
            if (next >= end
                || !TOKEN_IS(&file->tokens->tokens[next], ";")
                || csane_tokens_next(&view, next) != end) {
                continue;
            }
        }
        if (fixed_member_set) {
            csane_control_issue(file, stmt, STRLIT("STYLE-FINAL-DO-WHILE-ZERO"),
                                "do { ... } while (0) followed only by a "
                                "fixed member assignment and return; "
                                "use direct control flow");
        } else {
            csane_control_issue(file, stmt, STRLIT("STYLE-FINAL-DO-WHILE-ZERO"),
                                "do { ... } while (0) at function end has no "
                                "shared work after break; "
                                "use direct control flow");
        }
    }
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
        /* A nested block only needs its own scope when it directly
         * declares a variable. Declarations inside control-flow children
         * have their own scope and do not justify this outer block. */
        if (stmt->kind == CSANE_STMT_BLOCK
            && (stmt->parent < 0
                || file->statements->items[stmt->parent].kind
                   == CSANE_STMT_BLOCK)
            && csane_control_first(file, stmt)
               != file->toplevel->items[stmt->function].body.first) {
            bool declares_variable = false;

            for (int32 child = stmt->first_child; child >= 0;
                 child = file->statements->items[child].next_sibling) {
                CsaneStatement *inner = &file->statements->items[child];
                if (inner->kind == CSANE_STMT_SIMPLE
                    && csane_declarations_variable_statement(file, inner)) {
                    declares_variable = true;
                    break;
                }
            }
            if (!declares_variable) {
                int32 first = csane_control_first(file, stmt);
                int32 pos = file->tokens->tokens[first].offset;
                char *source = file->tokens->text;
                int32 line;

                /* A shared block may follow an if/else header selected by
                 * preprocessor branches. It is not an extra scope. */
                while (pos > 0 && is_space(source[pos - 1])) {
                    pos -= 1;
                }
                line = pos;
                while (line > 0 && source[line - 1] != '\n') {
                    line -= 1;
                }
                while (line < pos && char_is_horizontal_space(source[line])) {
                    line += 1;
                }
                if (!BEGINS_WITH(source + line, pos - line, "#endif")) {
                    csane_control_issue(file, stmt,
                                        STRLIT("AST-ARTIFICIAL-BLOCK"),
                                        "standalone block does not declare "
                                        "a local variable");
                }
            }
        }
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
    /* A for header should not span lines when its unchanged text, joined
     * at whitespace boundaries, fits within the configured column limit.
     * Keep comments, directives, macro continuations and unusual trailers.
     */
    for (int32 stmt_i = 0; stmt_i < ARRAY_LEN(file->statements->items);
         stmt_i += 1) {
        CsaneStatement *stmt = &file->statements->items[stmt_i];
        Tokenization *tokens = file->tokens;
        CsaneTokenView view;
        int32 first;
        int32 last;
        int32 open;
        int32 close;
        int32 start;
        int32 end;
        int32 line_end;
        int32 tail;
        int32 width;
        bool prefix_is_indent = true;
        bool duplicate = false;

        if (stmt->kind != CSANE_STMT_FOR
            || !csane_tokens_view(&view, tokens, stmt->header)) {
            continue;
        }
        for (int32 i = 0; i < stmt_i; i += 1) {
            CsaneStatement *previous = &file->statements->items[i];
            if (previous->kind == CSANE_STMT_FOR
                && previous->header.first == stmt->header.first
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
        if (first < 0 || last <= first
            || !TOKEN_IS(&tokens->tokens[first], "for")) {
            continue;
        }
        open = csane_tokens_next(&view, first);
        if (open >= last || !TOKEN_IS(&tokens->tokens[open], "(")) {
            continue;
        }
        close = csane_tokens_match(&view, open);
        if (close != last
            || tokenization_token_location(tokens, first).line
               == tokenization_token_location(tokens, close).line
            || !csane_wrap_safe(file, first, last, true)) {
            continue;
        }
        start = tokens->tokens[first].offset;
        while (start > 0 && tokens->text[start - 1] != '\n') {
            start -= 1;
        }
        for (int32 i = start; i < tokens->tokens[first].offset; i += 1) {
            if (tokens->text[i] != ' ' && tokens->text[i] != '\t') {
                prefix_is_indent = false;
                break;
            }
        }
        if (!prefix_is_indent) {
            continue;
        }
        end = tokens->tokens[close].offset + tokens->tokens[close].len;
        line_end = end;
        while (line_end < tokens->text_len
               && tokens->text[line_end] != '\n') {
            line_end += 1;
        }
        while (line_end > end && (tokens->text[line_end - 1] == ' '
                                  || tokens->text[line_end - 1] == '\r')) {
            line_end -= 1;
        }
        tail = end;
        while (tail < line_end && tokens->text[tail] == ' ') {
            tail += 1;
        }
        if (tail < line_end) {
            if (tokens->text[tail] != '{' || tail + 1 != line_end) {
                continue;
            }
            end = tail + 1;
        }
        width = csane_wrap_flat_length(tokens->text, start, end,
                                       file->column_limit);
        if (width >= 0 && (file->column_limit <= 0
                           || width <= file->column_limit)) {
            csane_rule_issue(file, tokens->tokens[first].offset,
                             STRLIT("FMT-FOR-WRAP"),
                             "for header fits on one line");
        }
    }
    csane_pointer_check(file);
    return;
}

#endif /* CSANECHECK_CONTROL_C */
