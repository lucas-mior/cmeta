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

typedef struct CsaneStructuralUnresolved {
    int32 token;
    char *target;
    char *reason;
} CsaneStructuralUnresolved;

static bool
csane_structure_conditional(CsaneRegions *regions, int32 owner) {
    if (owner < 0 || owner >= regions->count) {
        return false;
    }
    return regions->items[owner].kind == CSANE_REGION_CONDITIONAL_BRANCH;
}

static void
csane_structure_add(CsaneStructuralUnresolved *items, int32 *count,
                    int32 token, char *target, char *reason) {
    items[*count] = (CsaneStructuralUnresolved){
        .token = token,
        .target = target,
        .reason = reason,
    };
    *count += 1;
    return;
}

static int
csane_structure_compare(void *a_, void *b_) {
    const CsaneStructuralUnresolved *a = a_;
    const CsaneStructuralUnresolved *b = b_;
    int result;
    if (a->token != b->token) {
        return a->token < b->token ? -1 : 1;
    }
    result = strcmp(a->target, b->target);
    if (result != 0) {
        return result;
    }
    return strcmp(a->reason, b->reason);
}

void
csane_coverage_report_structural(char *path, int32 path_len,
                                 Tokenization *tokens,
                                 CsaneTopLevels *toplevel,
                                 CsaneStatements *statements) {
    int32 capacity = toplevel->opaque_count + statements->opaque_count;
    CsaneStructuralUnresolved *items;
    int32 count = 0;
    if (capacity <= 0) {
        return;
    }
    items = malloc2((int64)capacity*SIZEOF(*items));
    for (int32 i = 0; i < toplevel->count; i += 1) {
        CsaneTopItem *item = &toplevel->items[i];
        char *reason;
        if (item->kind != CSANE_TOP_OPAQUE) {
            continue;
        }
        reason = csane_structure_conditional(toplevel->regions,
                                             item->owner_region)
                 ? "conditional construct could not be merged"
                 : "ambiguous top-level construct";
        csane_structure_add(items, &count, item->tokens.first,
                            "top-level", reason);
    }
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *item = &statements->items[i];
        char *reason;
        if (item->kind != CSANE_STMT_OPAQUE) {
            continue;
        }
        reason = csane_structure_conditional(toplevel->regions,
                                             item->owner_region)
                 ? "conditional construct could not be merged"
                 : "unknown statement fragment";
        csane_structure_add(items, &count, item->tokens.first,
                            "statement", reason);
    }
    qsort64(items, count, SIZEOF(*items), csane_structure_compare);
    for (int32 i = 0; i < count; i += 1) {
        SourceLocation location = tokenization_token_location(tokens,
                                                              items[i].token);
        fmt_printf("coverage_unresolved file=%.*s line=%d column=%d "
                   "target=%s reason=\"%s\"\n",
                   path_len, path, location.line, location.column + 1,
                   items[i].target, items[i].reason);
    }
    free2(items, (int64)capacity*SIZEOF(*items));
    return;
}

#endif /* CSANECHECK_STRUCTURE_C */
