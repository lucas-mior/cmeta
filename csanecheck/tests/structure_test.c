// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"
#include "csanecheck/csck_regions.c"
#include "csanecheck/csck_tokens.c"
#include "csanecheck/csck_toplevel.c"
#include "csanecheck/csck_statements.c"

static int32 failures;

static void
test_check(bool condition, int32 line) {
    if (!condition) {
        fprintf(stderr, "structure_test.c:%d: failed\n", line);
        failures += 1;
    }
    return;
}

#define CHECK(CONDITION) test_check((CONDITION), __LINE__)

typedef struct TestStructure {
    Tokenization tokens;
    CPreprocessorAnalysis preprocessor;
    CsaneRegions regions;
    CsaneTopLevels top;
    CsaneStatements statements;
} TestStructure;

static void
test_init(TestStructure *test, char *text) {
    *test = (TestStructure){0};
    test->tokens = tokenize_copy(text, strlen32(text), TOKENIZE_DEFAULT);
    (void)c_preprocessor_analysis_init(&test->preprocessor, &test->tokens);
    CHECK(csane_regions_init(&test->regions, &test->tokens,
                             &test->preprocessor));
    CHECK(csane_toplevel_init(&test->top, &test->regions));
    CHECK(csane_statements_init(&test->statements, &test->top));
    return;
}

static void
test_free(TestStructure *test) {
    csane_statements_free(&test->statements);
    csane_toplevel_free(&test->top);
    csane_regions_free(&test->regions);
    c_preprocessor_analysis_free(&test->preprocessor);
    free_tokenization(&test->tokens);
    return;
}

static int32
test_function(TestStructure *test, char *name) {
    for (int32 i = 0; i < test->top.count; i += 1) {
        CsaneTopItem *item = &test->top.items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION
            && item->name_token >= 0
            && TOKEN_IS(&test->tokens.tokens[item->name_token], name)) {
            return i;
        }
    }
    return -1;
}

static int32
test_statement(TestStructure *test, int32 function, char *fragment,
               enum CsaneStatementKind kind) {
    for (int32 i = 0; i < test->statements.count; i += 1) {
        CsaneStatement *item = &test->statements.items[i];
        if (item->function != function || item->kind != kind) {
            continue;
        }
        SourceRange span = item->source;
        int32 size = span.end - span.start;
        if (size >= strlen32(fragment)
            && memmem64(test->tokens.text + span.start, size, fragment,
                        strlen32(fragment)) != NULL) {
            return i;
        }
    }
    return -1;
}

static void
test_bad_top_level_expression(void) {
    char *text =
        "int broken = (1;\n"
        "static void first(void) { return; }\n"
        "static void second(void) { return; }\n";
    TestStructure test;
    test_init(&test, text);
    CHECK(test.top.opaque_count == 1);
    CHECK(test_function(&test, "first") >= 0);
    CHECK(test_function(&test, "second") >= 0);
    test_free(&test);
    return;
}

static void
test_unclosed_function(void) {
    char *text =
        "static void broken(void) {\n"
        "    broken_call();\n"
        "static void recovered(void) {\n"
        "    recovered_call();\n"
        "}\n";
    TestStructure test;
    test_init(&test, text);
    int32 recovered = test_function(&test, "recovered");
    CHECK(recovered >= 0);
    CHECK(test.top.opaque_count >= 1);
    CHECK(test_statement(&test, recovered, "recovered_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_bad_inner_expression(void) {
    char *text =
        "static void damaged(void) {\n"
        "    if (bad {\n"
        "        broken_call();\n"
        "    }\n"
        "    following_call();\n"
        "}\n"
        "static void unaffected(void) {\n"
        "    good_call();\n"
        "}\n";
    TestStructure test;
    test_init(&test, text);
    int32 damaged = test_function(&test, "damaged");
    int32 unaffected = test_function(&test, "unaffected");
    CHECK(damaged >= 0 && unaffected >= 0);
    CHECK(test_statement(&test, damaged, "if (bad",
                         CSANE_STMT_OPAQUE) >= 0);
    CHECK(test_statement(&test, damaged, "following_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, unaffected, "good_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_aggregates_and_labels(void) {
    char *text =
        "void example(void) {\n"
        "    Thing x = (Thing){.value = (int[]){1, 2}[0]};\n"
        "    switch (x.value) {\n"
        "    case 1 ? 2 : 3:\n"
        "        call_case();\n"
        "        break;\n"
        "    default:\n"
        "        call_default();\n"
        "    }\n"
        "    tail();\n"
        "}\n";
    TestStructure test;
    test_init(&test, text);
    int32 function = test_function(&test, "example");
    CHECK(function >= 0);
    CHECK(test_statement(&test, function, "Thing x =",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, "switch (x.value)",
                         CSANE_STMT_SWITCH) >= 0);
    int32 label = test_statement(&test, function, "case 1 ? 2 : 3:",
                                 CSANE_STMT_CASE);
    CHECK(label >= 0);
    if (label >= 0) {
        SourceRange span = test.statements.items[label].source;
        CHECK(span.end - span.start == strlen32("case 1 ? 2 : 3:"));
    }
    CHECK(test_statement(&test, function, "tail()", CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_trailing_function_attribute(void) {
    char *text =
        "static void annotated(void) __attribute__((cold)) {\n"
        "    annotated_call();\n"
        "}\n"
        "static void ordinary(void) { ordinary_call(); }\n";
    TestStructure test;
    test_init(&test, text);
    int32 annotated = test_function(&test, "annotated");
    CHECK(annotated >= 0);
    CHECK(test_function(&test, "ordinary") >= 0);
    CHECK(test_statement(&test, annotated, "annotated_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_conditionals(void) {
    char *text =
        "void example(void) {\n"
        "#if CHOICE\n"
        "    first_branch();\n"
        "#else\n"
        "    second_branch();\n"
        "#endif\n"
        "    afterwards();\n"
        "}\n"
        "void another(void) { call_other(); }\n";
    TestStructure test;
    test_init(&test, text);
    int32 function = test_function(&test, "example");
    int32 another = test_function(&test, "another");
    CHECK(function >= 0 && another >= 0);
    CHECK(test_statement(&test, function, "first_branch()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, "second_branch()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, "afterwards()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, another, "call_other()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_local_bad_statement(void) {
    char *text =
        "void example(void) {\n"
        "    broken = (1;\n"
        "    good();\n"
        "}\n";
    TestStructure test;
    test_init(&test, text);
    int32 function = test_function(&test, "example");
    CHECK(function >= 0);
    CHECK(test_statement(&test, function, "broken = (1;",
                         CSANE_STMT_OPAQUE) >= 0);
    CHECK(test_statement(&test, function, "good()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}


/* Conditional branches contained by a function are statement regions,
 * never independent translation-unit declarations. */
static void
test_nested_function_guards(void) {
    char *text =
        "void guarded(void) {\n"
        "    before();\n"
        "#if PLATFORM_A\n"
        "    while (ready()) {\n"
        "        first();\n"
        "    }\n"
        "#elif PLATFORM_B\n"
        "    second();\n"
        "#if FEATURE\n"
        "    third();\n"
        "#else\n"
        "    fourth();\n"
        "#endif\n"
        "#else\n"
        "    {\n"
        "        fifth();\n"
        "    }\n"
        "#endif\n"
        "    after();\n"
        "}\n"
        "#if PLATFORM_A\n"
        "void platform(void) { platform_call(); }\n"
        "#else\n"
        "void alternative(void) { alternative_call(); }\n"
        "#endif\n";
    TestStructure test;
    test_init(&test, text);
    int32 guarded = test_function(&test, "guarded");
    int32 platform = test_function(&test, "platform");
    int32 alternative = test_function(&test, "alternative");
    CHECK(test.top.opaque_count == 0);
    CHECK(guarded >= 0 && platform >= 0 && alternative >= 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_statement(&test, guarded, "before()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "while (ready())",
                         CSANE_STMT_WHILE) >= 0);
    CHECK(test_statement(&test, guarded, "first()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "second()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "third()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "fourth()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "fifth()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, "after()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, platform, "platform_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, alternative, "alternative_call()",
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

/* Conditional fields and enumerators remain in their aggregate's
 * lexical context, not in a fabricated translation unit. */
static void
test_guarded_aggregate_fragments(void) {
    char *text =
        "typedef struct Bucket {\n"
        "    int32 key;\n"
        "#if FIXED\n"
        "    KEY_TYPE value;\n"
        "#if EXTRA\n"
        "    void (*callback)(int32);\n"
        "#else\n"
        "    KEY_TYPE *pointer;\n"
        "#endif\n"
        "#else\n"
        "    KEY_TYPE *value;\n"
        "    int32 len;\n"
        "#endif\n"
        "} Bucket;\n"
        "enum Flags {\n"
        "#if BITS\n"
        "    CAT(PREFIX_, NONE) = 0,\n"
        "#else\n"
        "    FLAG_NONE,\n"
        "#endif\n"
        "    FLAG_LAST\n"
        "};\n";
    TestStructure test;
    test_init(&test, text);
    int32 members = 0;
    int32 enumerators = 0;
    for (int32 i = 0; i < test.top.count; i += 1) {
        enum CsaneTopKind kind = test.top.items[i].kind;
        members += kind == CSANE_TOP_MEMBER_DECLARATION;
        enumerators += kind == CSANE_TOP_ENUMERATOR;
    }
    CHECK(test.top.opaque_count == 0);
    CHECK(members == 5);
    CHECK(enumerators == 2);
    test_free(&test);
    return;
}

static void
test_guarded_function_parameters(void) {
    char *text =
        "int32 run(int32 first\n"
        "#if WANT_EXTRA\n"
        "          , int32 extra\n"
        "#else\n"
        "          , int32 other\n"
        "#endif\n"
        "          ) { return first; }\n"
        "int32 ordinary(void) { return 0; }\n";
    TestStructure test;
    test_init(&test, text);
    CHECK(test_function(&test, "run") >= 0);
    CHECK(test_function(&test, "ordinary") >= 0);
    CHECK(test.top.opaque_count == 0);
    int32 parameters = 0;
    for (int32 i = 0; i < test.top.count; i += 1) {
        parameters += test.top.items[i].kind ==
                      CSANE_TOP_PARAMETER_FRAGMENT;
    }
    CHECK(parameters == 2);
    test_free(&test);
    return;
}

static void
test_bad_aggregate_fragment_stays_opaque(void) {
    char *text =
        "struct Fields {\n"
        "#if INVALID\n"
        "    int32 broken = (1;\n"
        "#else\n"
        "    int32 okay;\n"
        "#endif\n"
        "};\n";
    TestStructure test;
    test_init(&test, text);
    CHECK(test.top.opaque_count >= 1);
    bool recognized = false;
    for (int32 i = 0; i < test.top.count; i += 1) {
        if (test.top.items[i].kind == CSANE_TOP_MEMBER_DECLARATION) {
            recognized = true;
        }
    }
    CHECK(recognized);
    test_free(&test);
    return;
}

int
main(void) {
    test_bad_top_level_expression();
    test_unclosed_function();
    test_bad_inner_expression();
    test_aggregates_and_labels();
    test_trailing_function_attribute();
    test_conditionals();
    test_nested_function_guards();
    test_guarded_aggregate_fragments();
    test_guarded_function_parameters();
    test_bad_aggregate_fragment_stays_opaque();
    test_local_bad_statement();
    return failures > 0;
}
