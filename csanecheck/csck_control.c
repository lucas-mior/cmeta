// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONTROL_C)
#define CSANECHECK_CONTROL_C

#include "cbase.h"
#include "csck.h"

static bool
csane_control_generated(char *path, int32 path_len) {
    return ENDS_WITH(path, path_len, ".meta.h");
}

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

static void
csane_control_braces(CsaneFile *file, CsaneStatement *stmt) {
    bool control = stmt->kind == CSANE_STMT_IF
                   || stmt->kind == CSANE_STMT_FOR
                   || stmt->kind == CSANE_STMT_WHILE
                   || stmt->kind == CSANE_STMT_DO
                   || stmt->kind == CSANE_STMT_SWITCH;
    CsaneStatement *alternative;
    CsaneStatement *body;

    /* Even an opaque body is definitely unbraced if its first token is
     * not '{'. An unrecognized expression must not hide this violation. */
    if (control && stmt->body >= 0
        && !csane_control_block(file, stmt->body)
        && !(stmt->kind == CSANE_STMT_FOR
             && csane_control_for_empty(file, stmt->body))) {
        csane_control_issue(file, stmt, STRLIT("AST-BRACES"),
                            "control body must use curly braces");
    }
    if (stmt->kind != CSANE_STMT_IF || stmt->alternative < 0) {
        return;
    }
    alternative = &file->statements->items[stmt->alternative];
    if (alternative->kind != CSANE_STMT_ELSE || alternative->body < 0) {
        return;
    }
    body = &file->statements->items[alternative->body];
    if (csane_control_block(file, alternative->body)
        || body->kind == CSANE_STMT_IF) {
        return;
    }
    csane_control_issue(file, alternative, STRLIT("AST-BRACES"),
                        "else body must use curly braces");
}

/* Case labels in #if branches are parsed in a separate source stream.
 * Locate the nearest containing switch by original source span when no
 * statement-parent relationship is available. */
static int32
csane_control_enclosing_switch(CsaneFile *file, CsaneStatement *label) {
    int32 closest = -1;
    for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
        CsaneStatement *candidate = &file->statements->items[i];
        if (candidate->kind != CSANE_STMT_SWITCH
            || candidate->function != label->function
            || candidate->source.start >= label->source.start
            || candidate->source.end <= label->source.start) {
            continue;
        }
        if (closest < 0
            || candidate->source.start
               > file->statements->items[closest].source.start) {
            closest = i;
        }
    }
    return closest;
}

static void
csane_control_switch(CsaneFile *file, CsaneStatement *stmt) {
    int32 parent;
    int32 label_token;
    CsaneStatement *sw;
    int32 switch_token;
    int32 label_column;
    int32 switch_column;

    if (stmt->kind != CSANE_STMT_CASE
        && stmt->kind != CSANE_STMT_DEFAULT) {
        return;
    }
    parent = csane_control_enclosing_switch(file, stmt);
    if (parent < 0) {
        return;
    }
    label_token = csane_control_first(file, stmt);
    sw = &file->statements->items[parent];
    switch_token = csane_control_first(file, sw);
    if (label_token < 0 || switch_token < 0) {
        return;
    }
    label_column = tokenization_token_location(file->tokens,
                                               label_token).column;
    switch_column = tokenization_token_location(file->tokens,
                                                switch_token).column;
    if (label_column != switch_column) {
        csane_control_issue(file, stmt, STRLIT("AST-SWITCH-ALIGN"),
                            "case/default must align with switch");
    }
}

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
static bool
csane_control_after_endif(CsaneFile *file, CsaneStatement *stmt) {
    int32 first = csane_control_first(file, stmt);
    char *source;
    int32 pos;
    int32 line;

    if (first < 0) {
        return false;
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
        return false;
    }
    for (int32 i = first - 1; i >= 0; i -= 1) {
        Token *token = &file->tokens->tokens[i];
        int32 start = file->toplevel->items[stmt->function].source.start;
        int32 region;

        if (token->offset < start) {
            break;
        }
        region = csane_regions_at_token(file->regions, i);
        if (region < 0
            || file->regions->items[region].kind != CSANE_REGION_CODE
            || token_is_trivia(token)) {
            continue;
        }
        return TOKEN_IS(token, ")") || TOKEN_IS(token, "else")
               || TOKEN_IS(token, "do");
    }
    return false;
}

/* For a block detached from its surrounding statements by a directive,
 * recover only the preceding header/initializer clue. No macro expansion or
 * translation-unit type information is required. */
static bool
csane_control_header_before(CsaneFile *file, CsaneStatement *stmt) {
    int32 first = csane_control_first(file, stmt);
    int32 prev = csane_control_previous(file, stmt, first);
    Token *token;
    int32 before;

    if (prev < 0) {
        return false;
    }
    token = &file->tokens->tokens[prev];
    if (TOKEN_IS(token, ")") || TOKEN_IS(token, "else")
        || TOKEN_IS(token, "do") || TOKEN_IS(token, "=")
        || TOKEN_IS(token, ",") || TOKEN_IS(token, "(")) {
        return true;
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
            || BEGINS_WITH(source + line_start, length, "default:")) {
            return true;
        }
    }
    before = csane_control_previous(file, stmt, prev);
    if (before >= 0 && (TOKEN_IS(&file->tokens->tokens[before], "struct")
                        || TOKEN_IS(&file->tokens->tokens[before], "union")
                        || TOKEN_IS(&file->tokens->tokens[before], "enum"))) {
        return true;
    }
    return TOKEN_IS(token, "struct") || TOKEN_IS(token, "union")
           || TOKEN_IS(token, "enum");
}

static void
csane_control_artificial(CsaneFile *file, CsaneStatement *stmt) {
    int32 token;
    SourceLocation loc;
    int32 offset;
    char *text;
    int32 start;

    if (stmt->kind != CSANE_STMT_BLOCK) {
        return;
    }
    if (stmt->parent >= 0) {
        CsaneStatement *parent = &file->statements->items[stmt->parent];
        if (parent->kind != CSANE_STMT_BLOCK) {
            return;
        }
        /* A block immediately after a case/default label is its body. */
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
                return;
            }
            break;
        }
    } else {
        /* A root block may be a function-body fragment from a conditional
         * branch. The ordinary function-body stream has no such roots. */
        int32 base = file->toplevel->items[stmt->function].owner_region;
        if (stmt->owner_region == base) {
            return;
        }
    }
    if (csane_control_header_before(file, stmt)
        || csane_control_after_endif(file, stmt)) {
        return;
    }
    token = csane_control_first(file, stmt);
    if (token < 0) {
        return;
    }
    loc = tokenization_token_location(file->tokens, token);
    if (loc.column < 12) {
        return;
    }
    offset = file->tokens->tokens[token].offset;
    text = file->tokens->text;
    start = offset;
    while (start > 0 && text[start - 1] != '\n') {
        start -= 1;
    }
    if (offset - start < 12) {
        return;
    }
    for (int32 i = 0; i < 12; i += 1) {
        if (text[start + i] != ' ') {
            return;
        }
    }
    csane_control_issue(file, stmt, STRLIT("AST-ARTIFICIAL-BLOCK"),
                        "artificial block at nesting level 3 or more");
}

/* Only a comparison at the outermost expression level qualifies. Nested
 * comparisons, compound boolean conditions, and ternaries remain outside
 * this deliberately narrow formatting rule. */
static bool
csane_control_comparator(Token *token) {
    return TOKEN_IS(token, "==") || TOKEN_IS(token, "!=")
           || TOKEN_IS(token, "<") || TOKEN_IS(token, "<=")
           || TOKEN_IS(token, ">") || TOKEN_IS(token, ">=");
}

static bool
csane_control_single_comparison(CsaneFile *file, int32 open, int32 close) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    TokenRange condition = {open + 1, close};
    int32 comparison = -1;

    if (!csane_tokens_view(&view, tokens, condition)) {
        return false;
    }
    condition = csane_tokens_unwrap(&view, condition);
    for (int32 i = condition.first; i < condition.end; i += 1) {
        Token *token = &tokens->tokens[i];

        if (TOKEN_IS(token, "&&") || TOKEN_IS(token, "||")
            || TOKEN_IS(token, "?") || TOKEN_IS(token, ":")) {
            return false;
        }
        if (csane_control_comparator(token)) {
            if (comparison >= 0) {
                return false;
            }
            comparison = i;
        }
    }
    if (comparison < 0
        || !csane_tokens_view(&view, tokens, condition)) {
        return false;
    }
    for (int32 i = csane_tokens_next(&view, condition.first - 1);
         i < condition.end; i = csane_tokens_next(&view, i)) {
        Token *token = &tokens->tokens[i];

        if (i == comparison) {
            return true;
        }
        if (token_is_open_delimiter(token)) {
            int32 match = csane_tokens_match(&view, i);

            if (match < 0) {
                return false;
            }
            i = match;
        }
    }
    return false;
}

/* A single predicate or assignment can also be joined. Preserve compound
 * boolean conditions and ternaries even when their flattened form fits: those
 * breaks may communicate the logical structure of the expression. */
static bool
csane_control_simple_condition(CsaneFile *file, int32 open, int32 close) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view;
    TokenRange condition = {open + 1, close};
    int32 assignments = 0;

    if (csane_control_single_comparison(file, open, close)) {
        return true;
    }
    if (!csane_tokens_view(&view, tokens, condition)) {
        return false;
    }
    condition = csane_tokens_unwrap(&view, condition);
    for (int32 i = condition.first; i < condition.end; i += 1) {
        Token *token = &tokens->tokens[i];

        if (csane_control_comparator(token) || TOKEN_IS(token, "&&")
            || TOKEN_IS(token, "||") || TOKEN_IS(token, "?")
            || TOKEN_IS(token, ":")) {
            return false;
        }
    }
    if (!csane_tokens_view(&view, tokens, condition)) {
        return false;
    }
    for (int32 i = csane_tokens_next(&view, condition.first - 1);
         i < condition.end; i = csane_tokens_next(&view, i)) {
        Token *token = &tokens->tokens[i];

        if (TOKEN_IS(token, ",")) {
            return false;
        }
        if (TOKEN_IS(token, "=")) {
            assignments += 1;
            if (assignments > 1) {
                return false;
            }
        }
        if (token_is_open_delimiter(token)) {
            int32 match = csane_tokens_match(&view, i);

            if (match < 0) {
                return false;
            }
            i = match;
        }
    }
    return true;
}

/* The source line may begin with indentation or with the closing brace and
 * else preceding an else-if. Do not join an unrelated statement or comment. */
static bool
csane_control_if_prefix(char *text, int32 start, int32 keyword) {
    int32 i = start;

    while (i < keyword && text[i] == ' ') {
        i += 1;
    }
    if (i == keyword) {
        return true;
    }
    if (text[i] == '}') {
        i += 1;
        while (i < keyword && text[i] == ' ') {
            i += 1;
        }
    }
    if (i + 4 >= keyword || memcmp64(text + i, "else", 4)
        || text[i + 4] != ' ') {
        return false;
    }
    i += 4;
    while (i < keyword && text[i] == ' ') {
        i += 1;
    }
    return i == keyword;
}

/* Require the closing brace on the same physical line as the header, and
 * no trailing comment or other statement after it. */
static bool
csane_control_if_suffix(char *text, int32 close_end, int32 end) {
    int32 i = close_end;

    while (i < end && (text[i] == ' ' || text[i] == '\r')) {
        i += 1;
    }
    if (i >= end || text[i] != '{') {
        return false;
    }
    i += 1;
    while (i < end && (text[i] == ' ' || text[i] == '\r')) {
        i += 1;
    }
    return i == end;
}

static void
csane_control_if_wrap(CsaneFile *file, CsaneStatement *stmt, int32 index) {
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
        return;
    }
    for (int32 i = 0; i < index; i += 1) {
        CsaneStatement *previous = &file->statements->items[i];

        if (previous->kind == CSANE_STMT_IF
            && previous->header.first == stmt->header.first
            && previous->header.end == stmt->header.end) {
            return;
        }
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
    if (close != last
        || tokenization_token_location(tokens, open).line
           == tokenization_token_location(tokens, close).line
        || !csane_wrap_safe(file, first, last, true)
        || !csane_control_simple_condition(file, open, close)) {
        return;
    }
    start = tokens->tokens[first].offset;
    while (start > 0 && tokens->text[start - 1] != '\n') {
        start -= 1;
    }
    if (!csane_control_if_prefix(tokens->text, start,
                                 tokens->tokens[first].offset)) {
        return;
    }
    close_location = tokenization_token_location(tokens, close);
    end = tokenization_physical_line_end_offset(tokens, close_location.line);
    while (end > 0 && (tokens->text[end - 1] == '\n'
                       || tokens->text[end - 1] == '\r')) {
        end -= 1;
    }
    if (!csane_control_if_suffix(tokens->text,
                                 tokens->tokens[close].offset
                                 + tokens->tokens[close].len, end)) {
        return;
    }
    width = csane_wrap_flat_length(tokens->text, start, end,
                                   file->column_limit);
    if (width >= 0 && (file->column_limit <= 0
                       || width <= file->column_limit)) {
        csane_rule_issue(file, tokens->tokens[first].offset,
                         STRLIT("FMT-IF-WRAP"),
                         "simple if header fits on one line");
    }
}

void
csane_control_check(CsaneFile *file) {
    if (file->statements == NULL || file->is_ryu
        || csane_control_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->statements->items); i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        csane_control_braces(file, stmt);
        csane_control_switch(file, stmt);
        csane_control_artificial(file, stmt);
        csane_control_if_wrap(file, stmt, i);
    }
    csane_pointer_check(file);
    return;
}

#endif /* CSANECHECK_CONTROL_C */
