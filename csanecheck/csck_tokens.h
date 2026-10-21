// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOKENS_H)
#define CSANECHECK_TOKENS_H

#include "meta.h"

/* A path owns only an ordered list of borrowed token ranges. It never owns,
 * copies, expands, or mutates source tokens. Gaps between parts are invisible
 * to views of the path, which lets one conditional alternative be composed
 * from common prefix/suffix code and selected branch bodies.
 */
typedef struct CsaneTokenPath {
    Tokenization *tokens;
    TokenRange bounds;
    TokenRange *parts;
    int32 count;
    int32 capacity;
} CsaneTokenPath;

/* Borrowed, bounded token navigation. path == NULL means an ordinary
 * contiguous TokenRange. A non-NULL path keeps original token indices while
 * omitting ranges that do not belong to the selected logical source path.
 */
typedef struct CsaneTokenView {
    Tokenization *tokens;
    TokenRange range;
    CsaneTokenPath *path;
} CsaneTokenView;

bool csane_token_path_init(CsaneTokenPath *, Tokenization *, TokenRange);
bool csane_token_path_append(CsaneTokenPath *, TokenRange);
void csane_token_path_free(CsaneTokenPath *);
bool csane_tokens_path_view(CsaneTokenView *, CsaneTokenPath *);
bool csane_tokens_view(CsaneTokenView *, Tokenization *, TokenRange);
/* A child view cannot escape the parent (for example, a macro branch). */
bool csane_tokens_subview(CsaneTokenView *, CsaneTokenView *, TokenRange);
CsaneTokenView csane_tokens_full(Tokenization *);
int32 csane_tokens_next(CsaneTokenView *, int32);
int32 csane_tokens_previous(CsaneTokenView *, int32);
int32 csane_tokens_match(CsaneTokenView *, int32);
/* Returns the bounded interior of (), [] or {} and optionally its closer. */
bool csane_tokens_group(CsaneTokenView *, int32, TokenRange *, int32 *);
/* Discard surrounding parentheses, keeping the original borrowed indices. */
TokenRange csane_tokens_unwrap(CsaneTokenView *, TokenRange);
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
/* Split arguments in a bounded parenthesized group. Empty () has zero
 * arguments. Returns -1 for malformed groups/arguments or overflow.
 */
int32 csane_tokens_arguments(CsaneTokenView *, int32, TokenRange *, int32);
/* Recognize an outermost assignment, without building an expression AST.
 * This is purely syntactic: it does not classify declarations or lvalues.
 */
bool csane_tokens_assignment(CsaneTokenView *, TokenRange *, TokenRange *,
                             int32 *);
SourceRange csane_tokens_source(CsaneTokenView *);

#endif /* CSANECHECK_TOKENS_H */
