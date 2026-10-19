// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOPLEVEL_H)
#define CSANECHECK_TOPLEVEL_H

#include "csck_regions.h"

enum CsaneTopKind {
    CSANE_TOP_FUNCTION_DEFINITION = 1,
    CSANE_TOP_FUNCTION_DECLARATION,
    CSANE_TOP_TYPE_DECLARATION,
    CSANE_TOP_DECLARATION,
    CSANE_TOP_MACRO_INVOCATION,
    CSANE_TOP_OPAQUE,
};

/* Top-level within a FILE or CONDITIONAL_BRANCH code stream. The source
 * range may cross directives or conditional groups. Token ranges are
 * original, contiguous indices and may therefore include directive tokens;
 * consumers must consult CsaneRegions before inspecting their contents.
 * OPAQUE means boundary/context uncertainty, not invalid C.
 */
typedef struct CsaneTopItem {
    enum CsaneTopKind kind;
    SourceRange source;
    TokenRange tokens;
    TokenRange header; /* Excludes the body opener for definitions. */
    TokenRange body; /* Includes both braces; empty for other kinds. */
    int32 owner_region;
    int32 name_token; /* -1 if no simple function/macro name was found. */
} CsaneTopItem;

typedef struct CsaneTopLevels {
    CsaneRegions *regions; /* Borrowed; outlives this object. */
    CsaneTopItem *items;
    int32 count;
    int32 capacity;
    int32 opaque_count;
} CsaneTopLevels;

/* No preprocessing branch is chosen, and no complete C parsing is required.
 * Incomplete constructs are explicitly retained as OPAQUE regions.
 */
bool csane_toplevel_init(CsaneTopLevels *, CsaneRegions *);
void csane_toplevel_free(CsaneTopLevels *);
/* Returns the first item with the given owner containing the source token. */
int32 csane_toplevel_at_token(CsaneTopLevels *, int32, int32);

#endif /* CSANECHECK_TOPLEVEL_H */
