// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATUS_C)
#define CSANECHECK_STATUS_C

#include "cbase.h"
#include "csck.h"

static int32
csane_status_scan(CsaneFile *file, TokenRange range, int32 *first) {
    int32 count = 0;
    if (first != NULL) {
        *first = -1;
    }
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (token->kind != TOKEN_IDENT || !TOKEN_IS(token, "status")) {
            continue;
        }
        if (file->preprocessor->token_directives != NULL
            && file->preprocessor->token_directives[i] >= 0) {
            continue;
        }
        int32 previous = tokenization_previous_significant(file->tokens, i);
        if (previous >= range.first
            && (TOKEN_IS(&file->tokens->tokens[previous], ".")
                || TOKEN_IS(&file->tokens->tokens[previous], "->"))) {
            continue;
        }
        if (first != NULL && *first < 0) {
            *first = i;
        }
        count += 1;
    }
    return count;
}

static bool
csane_status_is(CsaneFile *file, int32 i, char *text) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], text);
}

static bool
csane_status_declaration(CsaneFile *file, CsaneStatement *statement) {
    if (statement->kind != CSANE_STMT_SIMPLE) {
        return false;
    }
    TokenRange range = statement->tokens;
    int32 type = token_range_first_significant(file->tokens, range);
    int32 name = tokenization_next_significant(file->tokens, type);
    int32 end = tokenization_next_significant(file->tokens, name);
    return type < range.end && name < range.end && end < range.end
           && csane_status_is(file, type, "int32")
           && csane_status_is(file, name, "status")
           && csane_status_is(file, end, ";")
           && tokenization_next_significant(file->tokens, end) >= range.end;
}

static bool
csane_status_assignment(CsaneFile *file, CsaneStatement *statement) {
    if (statement->kind != CSANE_STMT_SIMPLE
        || csane_status_scan(file, statement->tokens, NULL) != 1) {
        return false;
    }
    TokenRange range = statement->tokens;
    int32 first = token_range_first_significant(file->tokens, range);
    int32 second = tokenization_next_significant(file->tokens, first);
    return first < range.end && second < range.end
           && csane_status_is(file, first, "status")
           && csane_status_is(file, second, "=");
}

static bool
csane_status_adjacent(CsaneFile *file, CsaneStatement *a,
                      CsaneStatement *b) {
    if (a->source.end > b->source.start) {
        return false;
    }
    for (int32 i = a->source.end; i < b->source.start; i += 1) {
        char c = file->tokens->text[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return false;
        }
    }
    int32 assignment = -1;
    int32 use = -1;
    csane_status_scan(file, a->tokens, &assignment);
    csane_status_scan(file, b->tokens, &use);
    if (assignment < 0 || use < 0) {
        return false;
    }
    SourceLocation lhs = tokenization_token_location(file->tokens, assignment);
    SourceLocation rhs = tokenization_token_location(file->tokens, use);
    return rhs.line == lhs.line + 1;
}

/* The statement scanner links children of blocks, but root statements are
 * independent. Find the next direct statement in the same source stream. */
static int32
csane_status_next(CsaneStatements *statements, int32 current) {
    CsaneStatement *a = &statements->items[current];
    int32 best = -1;
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *b = &statements->items[i];
        if (b->function != a->function || b->parent != a->parent
            || b->owner_region != a->owner_region
            || b->source.start < a->source.end) {
            continue;
        }
        if (best < 0 || b->source.start
                        < statements->items[best].source.start) {
            best = i;
        }
    }
    return best;
}

static void
csane_status_function(CsaneFile *file, int32 function) {
    CsaneStatements *statements = file->statements;
    CsaneTopItem *top = &file->toplevel->items[function];
    if (top->kind != CSANE_TOP_FUNCTION_DEFINITION) {
        return;
    }
    int32 refs = csane_status_scan(file, top->body, NULL);
    if (refs < 3 || !(refs & 1)) {
        return;
    }
    int32 declaration = -1;
    int32 pairs = 0;
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *s = &statements->items[i];
        if (s->function != function || s->parent != -1
            || s->owner_region != top->owner_region
            || !csane_status_declaration(file, s)) {
            continue;
        }
        if (declaration >= 0) {
            return;
        }
        declaration = i;
    }
    if (declaration < 0) {
        return;
    }
    int32 i = csane_status_next(statements, declaration);
    while (i >= 0) {
        CsaneStatement *s = &statements->items[i];
        int32 next = csane_status_next(statements, i);
        if (next >= 0 && csane_status_assignment(file, s)) {
            CsaneStatement *use = &statements->items[next];
            if (use->function == function && use->parent == -1
                && use->owner_region == top->owner_region
                && use->kind == CSANE_STMT_SIMPLE
                && csane_status_scan(file, use->tokens, NULL) == 1
                && csane_status_adjacent(file, s, use)) {
                pairs += 1;
                i = csane_status_next(statements, next);
                continue;
            }
        }
        i = next;
    }
    if (pairs > 0 && refs == 1 + 2*pairs) {
        CsaneStatement *s = &statements->items[declaration];
        int32 token = token_range_first_significant(file->tokens, s->tokens);
        csane_rule_issue(file, file->tokens->tokens[token].offset,
                         "STYLE-STATUS-TEMP",
                         "inline int32 status assignment/use pairs");
    }
}

void
csane_status_check(CsaneFile *file) {
    if (file->statements == NULL || file->toplevel == NULL) {
        return;
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        csane_status_function(file, i);
    }
}

#endif /* CSANECHECK_STATUS_C */
