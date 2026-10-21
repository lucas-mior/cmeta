// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STRUCTURE_C)
#define CSANECHECK_STRUCTURE_C

#include "cbase.h"
#include "csck.h"

CsaneCoverage
csane_coverage_collect(CsaneTopLevels *toplevel,
                       CsaneStatements *statements) {
    CsaneCoverage result = {0};
    for (int32 i = 0; i < toplevel->count; i += 1) {
        CsaneTopItem *item = &toplevel->items[i];
        switch (item->kind) {
        case CSANE_TOP_FUNCTION_DEFINITION:
            result.function_definitions += 1;
            break;
        case CSANE_TOP_FUNCTION_DECLARATION:
            result.function_declarations += 1;
            break;
        case CSANE_TOP_TYPE_DECLARATION:
        case CSANE_TOP_DECLARATION:
            result.declarations += 1;
            break;
        case CSANE_TOP_OPAQUE:
            result.opaque_externals += 1;
            break;
        case CSANE_TOP_MACRO_INVOCATION:
        case CSANE_TOP_LINKAGE_FRAGMENT:
        case CSANE_TOP_MEMBER_DECLARATION:
        case CSANE_TOP_ENUMERATOR:
        case CSANE_TOP_PARAMETER_FRAGMENT:
        case CSANE_TOP_MACRO_INITIALIZER_FRAGMENT:
        case CSANE_TOP_MACRO_MEMBER_FRAGMENT:
        case CSANE_TOP_MACRO_ENUMERATOR_FRAGMENT:
        case CSANE_TOP_MACRO_PARAMETER_FRAGMENT:
        default:
            break;
        }
    }
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *stmt = &statements->items[i];
        if (stmt->kind == CSANE_STMT_OPAQUE) {
            result.opaque_statements += 1;
        } else if (stmt->kind != CSANE_STMT_BLOCK) {
            result.statements += 1;
        }
    }
    return result;
}

#endif /* CSANECHECK_STRUCTURE_C */
