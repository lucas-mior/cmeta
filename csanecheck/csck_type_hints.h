// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TYPE_HINTS_H)
#define CSANECHECK_TYPE_HINTS_H

#include "csck_regions.h"

enum CsaneTypeHintKind {
    CSANE_TYPE_HINT_TYPEDEF = 1,
    CSANE_TYPE_HINT_TAG,
};

typedef struct CsaneTypeHint {
    int32 token;
    enum CsaneTypeHintKind kind;
} CsaneTypeHint;

typedef struct CsaneTypeHints {
    Tokenization *tokens;
    CsaneTypeHint *items;
    int32 count;
    int32 capacity;
} CsaneTypeHints;

void csane_type_hints_init(CsaneTypeHints *, CsaneRegions *);
void csane_type_hints_free(CsaneTypeHints *);
bool csane_type_hints_has(CsaneTypeHints *, Token *, enum CsaneTypeHintKind);
bool csane_type_hints_typedef(CsaneTypeHints *, Token *);
bool csane_type_hints_tag(CsaneTypeHints *, Token *);

#endif /* CSANECHECK_TYPE_HINTS_H */
