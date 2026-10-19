// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOKENS_H)
#define CSANECHECK_TOKENS_H

#include "meta.h"

/* Borrowed, bounded token navigation. No tokenizer ownership or mutation. */
typedef struct CsaneTokenView {
    Tokenization *tokens;
    TokenRange range;
} CsaneTokenView;

bool csane_tokens_view(CsaneTokenView *, Tokenization *, TokenRange);
CsaneTokenView csane_tokens_full(Tokenization *);
int32 csane_tokens_next(CsaneTokenView *, int32);
int32 csane_tokens_previous(CsaneTokenView *, int32);
int32 csane_tokens_match(CsaneTokenView *, int32);
bool csane_tokens_balanced(CsaneTokenView *);
/* Finds an exact token at depth zero, skipping balanced nested groups.
 * Returns range.end if absent, -1 if nesting is malformed.
 */
int32 csane_tokens_top_level(CsaneTokenView *, int32, char *, int32);
/* Separates on depth-zero delimiters. The result is borrowed token ranges.
 * Returns -1 for malformed nesting or invalid arguments.
 */
int32 csane_tokens_split(CsaneTokenView *, char *, int32,
                         TokenRange *, int32);
SourceRange csane_tokens_source(CsaneTokenView *);

#endif /* CSANECHECK_TOKENS_H */
