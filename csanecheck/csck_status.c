// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATUS_C)
#define CSANECHECK_STATUS_C

#include "cbase.h"
#include "csck.h"

static bool
csane_status_is(CsaneFile *file, int32 i, char *text) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], text);
}

/* A status member, label, or preprocessor occurrence is not a reference to
 * the local variable. Keep the source-order references independently of the
 * statement parser: uses in if/return and nested blocks must be visible. */
static bool
csane_status_reference(CsaneFile *file, int32 i, TokenRange body) {
    Token *token = &file->tokens->tokens[i];
    if (token->kind != TOKEN_IDENT || !TOKEN_IS(token, "status")) {
        return false;
    }
    if (file->preprocessor->token_directives != NULL
        && file->preprocessor->token_directives[i] >= 0) {
        return false;
    }
    int32 previous = tokenization_previous_significant(file->tokens, i);
    int32 next = tokenization_next_significant(file->tokens, i);
    if ((previous >= body.first
         && (csane_status_is(file, previous, ".")
             || csane_status_is(file, previous, "->")))
        || (next < body.end && csane_status_is(file, next, ":"))) {
        return false;
    }
    return true;
}

static bool
csane_status_declaration(CsaneFile *file, int32 token,
                         TokenRange body) {
    int32 previous = tokenization_previous_significant(file->tokens, token);
    int32 next = tokenization_next_significant(file->tokens, token);
    return previous >= body.first && next < body.end
           && csane_status_is(file, previous, "int32")
           && csane_status_is(file, next, ";");
}

static bool
csane_status_pair(CsaneFile *file, int32 assignment, int32 use,
                  TokenRange body) {
    int32 next = tokenization_next_significant(file->tokens, assignment);
    if (next >= body.end || !csane_status_is(file, next, "=")) {
        return false;
    }
    int32 terminators = 0;
    for (int32 i = next + 1; i < use; i += 1) {
        terminators += csane_status_is(file, i, ";");
    }
    /* A second completed statement can have side effects that would be
     * reordered by inlining the assignment into the following use. */
    if (terminators != 1) {
        return false;
    }
    SourceLocation a = tokenization_token_location(file->tokens, assignment);
    SourceLocation b = tokenization_token_location(file->tokens, use);
    return b.line == a.line + 1;
}

static void
csane_status_function(CsaneFile *file, CsaneTopItem *top) {
    if (top->kind != CSANE_TOP_FUNCTION_DEFINITION) {
        return;
    }
    TokenRange body = top->body;
    int32 count = 0;
    for (int32 i = body.first; i < body.end; i += 1) {
        count += csane_status_reference(file, i, body);
    }
    if (count < 3 || !(count & 1)) {
        return;
    }
    int32 *refs = malloc2((int64)count*SIZEOF(*refs));
    if (refs == NULL) {
        return;
    }
    int32 used = 0;
    for (int32 i = body.first; i < body.end; i += 1) {
        if (csane_status_reference(file, i, body)) {
            refs[used++] = i;
        }
    }
    int32 declarations = 0;
    for (int32 i = 0; i < count; i += 1) {
        declarations += csane_status_declaration(file, refs[i], body);
    }
    bool matches = declarations == 1
                   && csane_status_declaration(file, refs[0], body);
    for (int32 i = 1; matches && i < count; i += 2) {
        matches = csane_status_pair(file, refs[i], refs[i + 1], body);
    }
    if (matches) {
        csane_rule_issue(file, file->tokens->tokens[refs[0]].offset,
                         "STYLE-STATUS-TEMP",
                         "inline int32 status assignment/use pairs");
    }
    free2(refs, (int64)count*SIZEOF(*refs));
}

void
csane_status_check(CsaneFile *file) {
    if (file->toplevel == NULL) {
        return;
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        csane_status_function(file, &file->toplevel->items[i]);
    }
}

#endif /* CSANECHECK_STATUS_C */
