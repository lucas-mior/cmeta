// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck_classify.c"

static bool
check_case(char *source, enum CsaneLocalKind expected) {
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    TokenRange range = {0, tokens.token_count};
    bool ok = csane_classify_statement(&tokens, range) == expected;
    if (!ok) { fprintf(stderr, "failed: %s\n", source); }
    free_tokenization(&tokens);
    return ok;
}

static bool
check_call(char *source, char *name, enum CsaneLocalKind expected) {
    Tokenization tokens = tokenize_copy(source, strlen32(source), 0);
    TokenRange range = {0, tokens.token_count};
    bool ok = false;
    for (int32 i = 0; i < tokens.token_count; i += 1) {
        if (TOKEN_IS(&tokens.tokens[i], name)) {
            ok = csane_classify_invocation(&tokens, range, i) == expected;
            break;
        }
    }
    if (!ok) { fprintf(stderr, "failed: %s\n", source); }
    free_tokenization(&tokens);
    return ok;
}

int
main(void) {
    bool ok = true;
    ok &= check_case("MyType *item;", CSANE_LOCAL_DECLARATION);
    ok &= check_case("const uint64 count = 1;", CSANE_LOCAL_DECLARATION);
    ok &= check_case("DWORD flag;", CSANE_LOCAL_DECLARATION);
    ok &= check_case("foo(bar);", CSANE_LOCAL_UNKNOWN);
    ok &= check_case("MyType(value);", CSANE_LOCAL_UNKNOWN);
    ok &= check_call("foo(bar);", "foo", CSANE_LOCAL_CALL);
    ok &= check_call("HASH_CREATE(x);", "HASH_CREATE",
                     CSANE_LOCAL_MACRO_INVOCATION);
    ok &= check_call("error(\"bad\");", "error",
                     CSANE_LOCAL_MACRO_INVOCATION);
    ok &= check_call("MyType f(int32 x);", "f", CSANE_LOCAL_UNKNOWN);
    ok &= check_call("x = foo(bar);", "foo", CSANE_LOCAL_CALL);
    if (!ok) {
        fprintf(stderr, "classification regression failed\n");
    }
    return ok ? 0 : 1;
}
