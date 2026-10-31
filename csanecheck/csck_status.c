// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATUS_C)
#define CSANECHECK_STATUS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"

static bool
csane_status_is(CsaneFile *file, int32 i, char *text, int32 text_len) {
    return i >= 0 && i < file->tokens->token_count
           && TOKEN_IS(&file->tokens->tokens[i], text, text_len);
}

/* A status member, label, or preprocessor occurrence is not a reference to
 * the local variable. Keep the source-order references independently of the
 * statement parser: uses in if/return and nested blocks must be visible. */
static bool
csane_status_reference(CsaneFile *file, int32 i, TokenRange body) {
    Token *token = &file->tokens->tokens[i];
    int32 previous;
    int32 next;

    if (token->kind != TOKEN_IDENT || !TOKEN_IS(token, "status")) {
        return false;
    }
    if (file->preprocessor->token_directives != NULL
        && file->preprocessor->token_directives[i] >= 0) {
        return false;
    }
    previous = tokenization_previous_significant(file->tokens, i);
    next = tokenization_next_significant(file->tokens, i);
    if ((previous >= body.first
         && (csane_status_is(file, previous, STRLIT("."))
             || csane_status_is(file, previous, STRLIT("->"))))
        || (next < body.end && csane_status_is(file, next, STRLIT(":")))) {
        return false;
    }
    return true;
}

static bool
csane_status_declaration(CsaneFile *file, int32 token, TokenRange body) {
    int32 previous = tokenization_previous_significant(file->tokens, token);
    int32 next = tokenization_next_significant(file->tokens, token);
    return previous >= body.first && next < body.end
           && csane_status_is(file, previous, STRLIT("int32"))
           && csane_status_is(file, next, STRLIT(";"));
}

static bool
csane_status_pair(CsaneFile *file, int32 assignment, int32 use,
                  TokenRange body) {
    int32 next = tokenization_next_significant(file->tokens, assignment);
    int32 terminators = 0;
    SourceLocation a;
    SourceLocation b;

    if (next >= body.end || !csane_status_is(file, next, STRLIT("="))) {
        return false;
    }
    for (int32 i = next + 1; i < use; i += 1) {
        terminators += csane_status_is(file, i, STRLIT(";"));
    }
    /* A second completed statement can have side effects that would be
     * reordered by inlining the assignment into the following use. */
    if (terminators != 1) {
        return false;
    }
    a = tokenization_token_location(file->tokens, assignment);
    b = tokenization_token_location(file->tokens, use);
    return b.line == a.line + 1;
}

static void
csane_status_function(CsaneFile *file, CsaneTopItem *top) {
    TokenRange body;
    int32 count = 0;
    int32 *refs;
    int32 used = 0;
    int32 declarations = 0;
    bool matches;

    if (top->kind != CSANE_TOP_FUNCTION_DEFINITION) {
        return;
    }
    body = top->body;
    for (int32 i = body.first; i < body.end; i += 1) {
        count += csane_status_reference(file, i, body);
    }
    if (count < 3 || !(count & 1)) {
        return;
    }
    refs = malloc2(count*SIZEOF(*refs));
    if (refs == NULL) {
        return;
    }
    for (int32 i = body.first; i < body.end; i += 1) {
        if (csane_status_reference(file, i, body)) {
            refs[used++] = i;
        }
    }
    for (int32 i = 0; i < count; i += 1) {
        declarations += csane_status_declaration(file, refs[i], body);
    }
    matches = declarations == 1
              && csane_status_declaration(file, refs[0], body);
    for (int32 i = 1; matches && i < count; i += 2) {
        matches = csane_status_pair(file, refs[i], refs[i + 1], body);
    }
    if (matches) {
        csane_rule_issue(file, file->tokens->tokens[refs[0]].offset,
                         STRLIT("STYLE-STATUS-TEMP"),
                         "inline int32 status assignment/use pairs");
    }
    free2(refs, count*SIZEOF(*refs));
}

/* Find the adjacent call/check pair in source tokens. Statement parsing can
 * lose sibling relationships near conditional branches and large functions,
 * so require exact token adjacency and stay inside the same code region. */
void
csane_status_check(CsaneFile *file) {
    CsaneTokenView view = csane_tokens_full(file->tokens);

    if (file->toplevel == NULL || file->regions == NULL) {
        return;
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *top = &file->toplevel->items[i];
        TokenRange body;

        csane_status_function(file, top);
        if (top->kind != CSANE_TOP_FUNCTION_DEFINITION) {
            continue;
        }
        body = top->body;
        for (int32 j = body.first; j < body.end; j += 1) {
            int32 previous;
            int32 eq;
            int32 call;
            int32 open;
            int32 close;
            int32 semicolon;
            int32 check;
            int32 name;
            int32 compare;
            int32 zero;
            int32 region;
            TokenRange condition;

            if (!csane_status_is(file, j, STRLIT("status"))) {
                continue;
            }
            region = csane_regions_at_token(file->regions, j);
            if (region < 0 || file->regions->items[region].kind
                              != CSANE_REGION_CODE) {
                continue;
            }
            previous = tokenization_previous_significant(file->tokens, j);
            if (!csane_status_is(file, previous, STRLIT(";"))
                && !csane_status_is(file, previous, STRLIT("{"))
                && !csane_status_is(file, previous, STRLIT("}"))
                && !csane_status_is(file, previous, STRLIT(":"))
                && !(file->preprocessor != NULL
                     && file->preprocessor->token_directives != NULL
                     && previous >= 0
                     && file->preprocessor->token_directives[previous] >= 0)) {
                continue;
            }
            eq = tokenization_next_significant(file->tokens, j);
            call = tokenization_next_significant(file->tokens, eq);
            open = tokenization_next_significant(file->tokens, call);
            if (!csane_status_is(file, eq, STRLIT("="))
                || call >= body.end
                || file->tokens->tokens[call].kind != TOKEN_IDENT
                || !csane_status_is(file, open, STRLIT("("))) {
                continue;
            }
            close = csane_tokens_match(&view, open);
            if (close < 0 || close >= body.end) {
                continue;
            }
            semicolon = tokenization_next_significant(file->tokens, close);
            if (!csane_status_is(file, semicolon, STRLIT(";"))) {
                continue;
            }
            check = tokenization_next_significant(file->tokens, semicolon);
            if (!csane_status_is(file, check, STRLIT("if"))
                || csane_regions_at_token(file->regions, check) != region) {
                continue;
            }
            open = tokenization_next_significant(file->tokens, check);
            if (!csane_status_is(file, open, STRLIT("("))) {
                continue;
            }
            close = csane_tokens_match(&view, open);
            if (close < 0 || close >= body.end) {
                continue;
            }
            condition = csane_tokens_unwrap(&view,
                                            (TokenRange){open + 1, close});
            name = token_range_first_significant(file->tokens, condition);
            compare = tokenization_next_significant(file->tokens, name);
            zero = tokenization_next_significant(file->tokens, compare);
            if (csane_status_is(file, name, STRLIT("status"))
                && csane_status_is(file, compare, STRLIT("<"))
                && csane_status_is(file, zero, STRLIT("0"))
                && tokenization_next_significant(file->tokens, zero)
                   >= condition.end) {
                csane_rule_issue(file, file->tokens->tokens[j].offset,
                                 STRLIT("STYLE-STATUS-IF"),
                                 "combine status assignment with the following "
                                 "if (status < 0) check");
            }
        }
    }
}

#endif /* CSANECHECK_STATUS_C */
