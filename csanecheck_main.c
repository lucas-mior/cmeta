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
#include "csanecheck.h"
#include "csanecheck_files.c"
#include "csanecheck_config.c"
#include "csanecheck_structure.c"
#include "csanecheck_context.c"
#include "csanecheck_layout.c"
#include "csanecheck_calls.c"
#include "csanecheck_wrap.c"
#include "csanecheck_rules.c"
#include "csanecheck.c"

#if CC_CLANG
#pragma clang diagnostic pop
#endif

int
main(int argc, char **argv) {
    return csane_run(argc, argv);
}
