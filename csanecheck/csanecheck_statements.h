// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATEMENTS_H)
#define CSANECHECK_STATEMENTS_H

#include "csanecheck_toplevel.h"

enum CsaneStatementKind {
    CSANE_STMT_BLOCK = 1,
    CSANE_STMT_IF,
    CSANE_STMT_ELSE,
    CSANE_STMT_FOR,
    CSANE_STMT_WHILE,
    CSANE_STMT_DO,
    CSANE_STMT_SWITCH,
    CSANE_STMT_CASE,
    CSANE_STMT_DEFAULT,
    CSANE_STMT_LABEL,
    CSANE_STMT_SIMPLE,
    CSANE_STMT_OPAQUE,
};

typedef struct CsaneStatement {
    enum CsaneStatementKind kind;
    SourceRange source;
    TokenRange tokens; /* Original tokens, possibly spanning directives. */
    TokenRange header; /* A control header, otherwise empty. */
    int32 function; /* Index in CsaneTopLevels.items. */
    int32 owner_region; /* File or conditional-branch source stream. */
    int32 parent; /* Index in CsaneStatements.items, -1 for root. */
    int32 first_child;
    int32 last_child;
    int32 next_sibling;
    int32 body; /* Direct control-flow body, -1 if unavailable. */
    int32 alternative; /* Else branch of an IF, -1 if unavailable. */
} CsaneStatement;

typedef struct CsaneStatements {
    CsaneTopLevels *toplevel; /* Borrowed. */
    CsaneStatement *items;
    int32 count;
    int32 capacity;
    int32 opaque_count;
} CsaneStatements;

/* Parses each function in independent original-source streams. Conditional
 * branches are examined separately, never concatenated into one C stream.
 * Unrecognized fragments remain explicitly visible as OPAQUE statements.
 */
bool csane_statements_init(CsaneStatements *, CsaneTopLevels *);
void csane_statements_free(CsaneStatements *);
int32 csane_statements_at_token(CsaneStatements *, int32, int32);

#endif /* CSANECHECK_STATEMENTS_H */
