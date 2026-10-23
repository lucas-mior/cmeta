// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_REGIONS_H)
#define CSANECHECK_REGIONS_H

#include "meta.h"

/* Source regions are borrowed views of the original, unexpanded text.
 * Sibling regions partition their parent's source range. Conditional
 * alternatives are separate children of a group; none is presumed active.
 */
enum CsaneRegionKind {
    CSANE_REGION_FILE = 1,
    CSANE_REGION_CODE,
    CSANE_REGION_DIRECTIVE,
    CSANE_REGION_MACRO_DEFINE,
    CSANE_REGION_DIRECTIVE_TEXT,
    CSANE_REGION_MACRO_PARAMETERS,
    CSANE_REGION_MACRO_REPLACEMENT,
    CSANE_REGION_CONDITIONAL_GROUP,
    CSANE_REGION_CONDITIONAL_BRANCH,
};

typedef struct CsaneSourceRegion {
    enum CsaneRegionKind kind;
    SourceRange source;
    TokenRange tokens;
    int32 parent;
    int32 first_child;
    int32 last_child;
    int32 next_sibling;
    int32 directive_index;
    int32 group_index;
    int32 branch_index;
    /* Local structure is sound even if another region has an issue. */
    bool complete;
} CsaneSourceRegion;

typedef struct CsaneRegions {
    Tokenization *tokens; /* Borrowed; outlives the regions. */
    CPreprocessorAnalysis *preprocessor; /* Borrowed; outlives regions. */
    CsaneSourceRegion *items;
    int32 *token_regions; /* Innermost region for each token offset. */
    int32 count;
    int32 capacity;
    bool complete; /* False when any preprocessor issue exists. */
} CsaneRegions;

/* A valid preprocessor analysis may be incomplete; the source is still
 * partitioned into code and directive regions, with complete set to false.
 * Returns false for invalid arguments, not for preprocessor diagnostics.
 */
bool csane_regions_init(CsaneRegions *, Tokenization *,
                        CPreprocessorAnalysis *);
void csane_regions_free(CsaneRegions *);
/* The innermost containing region. Invalid positions return -1. */
int32 csane_regions_at_offset(CsaneRegions *, int32);
int32 csane_regions_at_token(CsaneRegions *, int32);
/* This lookup includes the given region itself. */
int32 csane_regions_enclosing(CsaneRegions *, int32,
                              enum CsaneRegionKind);

#endif /* CSANECHECK_REGIONS_H */
