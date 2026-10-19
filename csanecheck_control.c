// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONTROL_C)
#define CSANECHECK_CONTROL_C

#include "cbase.h"
#include "csanecheck.h"

static bool
csane_control_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static int32
csane_control_first(CsaneFile *file, CsaneStatement *stmt) {
    int32 index = token_range_first_significant(file->tokens, stmt->tokens);
    return index < stmt->tokens.end ? index : -1;
}

static void
csane_control_issue(CsaneFile *file, CsaneStatement *stmt, char *rule,
                    char *message) {
    int32 token = csane_control_first(file, stmt);
    if (token >= 0) {
        csane_rule_issue(file, file->tokens->tokens[token].offset,
                         rule, message);
    }
}

static bool
csane_control_block(CsaneFile *file, int32 index) {
    if (index < 0 || index >= file->statements->count) {
        return false;
    }
    return file->statements->items[index].kind == CSANE_STMT_BLOCK;
}

static bool
csane_control_for_empty(CsaneFile *file, int32 index) {
    if (index < 0 || index >= file->statements->count) {
        return false;
    }
    CsaneStatement *body = &file->statements->items[index];
    int32 first = csane_control_first(file, body);
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
    if (control && stmt->body >= 0
        && !csane_control_block(file, stmt->body)
        && file->statements->items[stmt->body].kind != CSANE_STMT_OPAQUE
        && !(stmt->kind == CSANE_STMT_FOR
             && csane_control_for_empty(file, stmt->body))) {
        csane_control_issue(file, stmt, "AST-BRACES",
                            "control body must use curly braces");
    }
    if (stmt->kind != CSANE_STMT_IF || stmt->alternative < 0) {
        return;
    }
    CsaneStatement *alternative = &file->statements->items[stmt->alternative];
    if (alternative->kind != CSANE_STMT_ELSE || alternative->body < 0) {
        return;
    }
    CsaneStatement *body = &file->statements->items[alternative->body];
    if (body->kind == CSANE_STMT_BLOCK || body->kind == CSANE_STMT_IF
        || body->kind == CSANE_STMT_OPAQUE) {
        return;
    }
    csane_control_issue(file, alternative, "AST-BRACES",
                        "else body must use curly braces");
}

static void
csane_control_switch(CsaneFile *file, CsaneStatement *stmt) {
    if (stmt->kind != CSANE_STMT_CASE
        && stmt->kind != CSANE_STMT_DEFAULT) {
        return;
    }
    int32 parent = stmt->parent;
    while (parent >= 0
           && file->statements->items[parent].kind != CSANE_STMT_SWITCH) {
        parent = file->statements->items[parent].parent;
    }
    if (parent < 0) {
        return;
    }
    int32 label_token = csane_control_first(file, stmt);
    CsaneStatement *sw = &file->statements->items[parent];
    int32 switch_token = csane_control_first(file, sw);
    if (label_token < 0 || switch_token < 0) {
        return;
    }
    int32 label_column = tokenization_token_location(file->tokens,
                                                     label_token).column;
    int32 switch_column = tokenization_token_location(file->tokens,
                                                      switch_token).column;
    if (label_column != switch_column) {
        csane_control_issue(file, stmt, "AST-SWITCH-ALIGN",
                            "case/default must align with switch");
    }
}

static void
csane_control_artificial(CsaneFile *file, CsaneStatement *stmt) {
    if (stmt->kind != CSANE_STMT_BLOCK || stmt->parent < 0) {
        return;
    }
    CsaneStatement *parent = &file->statements->items[stmt->parent];
    if (parent->kind != CSANE_STMT_BLOCK) {
        return;
    }
    /* A block immediately following a case/default label is its body. */
    for (int32 i = parent->first_child; i >= 0;
         i = file->statements->items[i].next_sibling) {
        if (file->statements->items[i].next_sibling !=
            (int32)(stmt - file->statements->items)) {
            continue;
        }
        enum CsaneStatementKind kind = file->statements->items[i].kind;
        if (kind == CSANE_STMT_CASE || kind == CSANE_STMT_DEFAULT
            || kind == CSANE_STMT_LABEL) {
            return;
        }
        break;
    }
    int32 token = csane_control_first(file, stmt);
    if (token < 0) {
        return;
    }
    SourceLocation loc = tokenization_token_location(file->tokens, token);
    if (loc.column < 12) {
        return;
    }
    int32 offset = file->tokens->tokens[token].offset;
    char *text = file->tokens->text;
    int32 start = offset;
    while (start > 0 && text[start - 1] != '\n') {
        start -= 1;
    }
    /* A control header may end in #endif, leaving its body as an
     * apparently standalone block in the original branch stream. */
    int32 prev = start;
    while (prev > 0 && (text[prev - 1] == '\n'
                        || text[prev - 1] == ' '
                        || text[prev - 1] == '\t')) {
        prev -= 1;
    }
    int32 line = prev;
    while (line > 0 && text[line - 1] != '\n') {
        line -= 1;
    }
    while (line < prev && (text[line] == ' ' || text[line] == '\t')) {
        line += 1;
    }
    if (prev - line >= 6 && !memcmp64(text + line, "#endif", 6)) {
        return;
    }
    if (offset - start < 12) {
        return;
    }
    for (int32 i = 0; i < 12; i += 1) {
        if (text[start + i] != ' ') {
            return;
        }
    }
    csane_control_issue(file, stmt, "AST-ARTIFICIAL-BLOCK",
                        "artificial block at nesting level 3 or more");
}

void
csane_control_check(CsaneFile *file) {
    if (file->statements == NULL || file->is_ryu
        || csane_control_generated(file->path)) {
        return;
    }
    for (int32 i = 0; i < file->statements->count; i += 1) {
        CsaneStatement *stmt = &file->statements->items[i];
        csane_control_braces(file, stmt);
        csane_control_switch(file, stmt);
        csane_control_artificial(file, stmt);
    }
}

#endif /* CSANECHECK_CONTROL_C */
