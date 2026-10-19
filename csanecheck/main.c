// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"

/* Mixed declarations are valid C11; Clang's pre-C99 warning is irrelevant. */
#if CC_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#endif

#define CMETA_IMPLEMENT
#include "meta.h"
#include "csck.h"
#include "csck_tokens.c"
#include "csck_regions.c"
#include "csck_toplevel.c"
#include "csck_statements.c"
#include "csck_classify.c"
#include "csck_files.c"
#include "csck_config.c"
#include "csck_structure.c"
#include "csck_control.c"
#include "csck_bounds.c"
#include "csck_status.c"
#include "csck_context.c"
#include "csck_expression_style.c"
#include "csck_layout.c"
#include "csck_calls.c"
#include "csck_declarations.c"
#include "csck_wrap.c"
#include "csck_coverage.c"
#include "csck_rules.c"
#include "csck_types.c"
#include "csck_macros.c"
#include "csck.c"

#if CC_CLANG
#pragma clang diagnostic pop
#endif

int
main(int argc, char **argv) {
    return csane_run(argc, argv);
}
