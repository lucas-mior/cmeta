// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CLASSIFY_H)
#define CSANECHECK_CLASSIFY_H

#include "csanecheck_statements.h"

enum CsaneNameKind {
    CSANE_NAME_OTHER = 0,
    CSANE_NAME_PRIMITIVE,
    CSANE_NAME_TYPE,
    CSANE_NAME_MACRO,
    CSANE_NAME_IDENTIFIER,
};

enum CsaneLocalKind {
    CSANE_LOCAL_UNKNOWN = 0,
    CSANE_LOCAL_DECLARATION,
    CSANE_LOCAL_CALL,
    CSANE_LOCAL_MACRO_INVOCATION,
};

/* Heuristic names are hints, not a replacement for C's symbol table.
 * All ranges and indices refer to the original, borrowed tokenization.
 */
enum CsaneNameKind csane_name_kind(Token *);
bool csane_type_name(Token *);
enum CsaneLocalKind csane_classify_statement(Tokenization *, TokenRange);
enum CsaneLocalKind csane_classify_invocation(Tokenization *, TokenRange,
                                                int32);

#endif /* CSANECHECK_CLASSIFY_H */
