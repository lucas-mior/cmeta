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
        error2("structure_test.c:%d: failed\n", line);
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
test_init(TestStructure *test, char *text, int32 text_len) {
    *test = (TestStructure){0};
    test->tokens = tokenize_copy(text, text_len, TOKENIZE_DEFAULT);
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
test_function(TestStructure *test, char *name, int32 name_len) {
    for (int32 i = 0; i < test->top.count; i += 1) {
        CsaneTopItem *item = &test->top.items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION
            && item->name_token >= 0
            && STREQUAL(test->tokens.tokens[item->name_token].text,
                        test->tokens.tokens[item->name_token].len,
                        name, name_len)) {
            return i;
        }
    }
    return -1;
}

static int32
test_statement(TestStructure *test, int32 function,
               char *fragment, int32 fragment_len,
               enum CsaneStatementKind kind) {
    for (int32 i = 0; i < test->statements.count; i += 1) {
        CsaneStatement *item = &test->statements.items[i];
        if (item->function != function || item->kind != kind) {
            continue;
        }
        SourceRange span = item->source;
        int32 size = span.end - span.start;
        if (size >= fragment_len
            && memmem64(test->tokens.text + span.start, size,
                        fragment, fragment_len) != NULL) {
            return i;
        }
    }
    return -1;
}

static int32
test_top_item(TestStructure *test,
              char *fragment, int32 fragment_len, enum CsaneTopKind kind) {
    for (int32 i = 0; i < test->top.count; i += 1) {
        CsaneTopItem *item = &test->top.items[i];
        SourceRange span;
        int32 size;
        if (item->kind != kind) {
            continue;
        }
        span = item->source;
        size = span.end - span.start;
        if (size >= fragment_len
            && memmem64(test->tokens.text + span.start, size,
                        fragment, fragment_len) != NULL) {
            return i;
        }
    }
    return -1;
}

/* hash.c has conditional fragments that contribute arguments to an open
 * call. A selected branch path must reconnect the argument to the common
 * call prefix and suffix without macro expansion. */
static void
test_conditional_call_argument_gap(void) {
    char *text =
        "int32 example(int32 key, int32 length) {\n"
        "    return call(key\n"
        "#if WANT_LENGTH\n"
        "                , length\n"
        "#endif\n"
        "                , 1);\n"
        "}\n";
    TestStructure test;
    int32 function;
    test_init(&test, text, strlen32(text));
    function = test_function(&test, STRLIT("example"));
    CHECK(function >= 0);
    CHECK(test.top.opaque_count == 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_statement(&test, function, STRLIT("return call(key"),
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

/* csck_files.c selects one half of an if header with the preprocessor while
 * sharing the body and suffix. Each selected branch path must recover one
 * complete CSANE_STMT_IF over the shared body. */
static void
test_conditional_if_header_gap(void) {
    char *text =
        "bool example(char *path) {\n"
        "    struct stat st;\n"
        "#if OS_WINDOWS\n"
        "    if (stat(path, &st) != 0) {\n"
        "#else\n"
        "    if (lstat(path, &st) != 0) {\n"
        "#endif\n"
        "        return false;\n"
        "    }\n"
        "    return true;\n"
        "}\n";
    TestStructure test;
    int32 function;
    test_init(&test, text, strlen32(text));
    function = test_function(&test, STRLIT("example"));
    CHECK(function >= 0);
    CHECK(test.top.opaque_count == 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_statement(&test, function, STRLIT("if (stat(path"),
                         CSANE_STMT_IF) >= 0);
    CHECK(test_statement(&test, function, STRLIT("if (lstat(path"),
                         CSANE_STMT_IF) >= 0);
    CHECK(test_statement(&test, function, STRLIT("return false;"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_top_item(&test, STRLIT("}"), CSANE_TOP_OPAQUE) < 0);
    test_free(&test);
    return;
}

/* util.c contains a conditional macro invocation inside an initializer list.
 * The containing declaration is recognized, while the branch element is an
 * artificial top-level opaque item. Recovery target: an initializer-element
 * macro fragment owned by the declaration. */
static void
test_initializer_macro_gap(void) {
    char *text =
        "static int32 values[] = {\n"
        "    ITEM(A),\n"
        "#if HAVE_B\n"
        "    ITEM(B),\n"
        "#endif\n"
        "    ITEM(C),\n"
        "};\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test.top.opaque_count == 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_top_item(&test, STRLIT("static int32 values[]"),
                        CSANE_TOP_DECLARATION) >= 0);
    CHECK(test_top_item(&test, STRLIT("ITEM(B),"),
                        CSANE_TOP_MACRO_INITIALIZER_FRAGMENT) >= 0);
    test_free(&test);
    return;
}

/* xenums.c uses a macro-generated enum tag and an ENUM_FIELDS invocation in
 * the enum body. The tag currently looks function-like to the permissive
 * scanner and ENUM_FIELDS is consequently parsed as a statement. Recovery
 * target: a type declaration plus an enum-body macro fragment. */
static void
test_enum_macro_gap(void) {
    char *text =
        "#if ENABLE\n"
        "enum CAT(ENUM_NAME, _Indices) {\n"
        "#define XX(e) CAT(e, _INDEX),\n"
        "#define XX_2(e, x) CAT(e, _INDEX),\n"
        "#define XX(...) SELECT(XX_, __VA_ARGS__)\n"
        "\n"
        "    ENUM_FIELDS\n"
        "\n"
        "#undef XX\n"
        "#undef XX_2\n"
        "};\n"
        "#endif\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test_function(&test, STRLIT("CAT")) < 0);
    CHECK(test.top.opaque_count == 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_top_item(&test, STRLIT("enum CAT(ENUM_NAME, _Indices)"),
                        CSANE_TOP_TYPE_DECLARATION) >= 0);
    CHECK(test_top_item(&test, STRLIT("ENUM_FIELDS"),
                        CSANE_TOP_MACRO_ENUMERATOR_FRAGMENT) >= 0);
    test_free(&test);
    return;
}

/* xenums.c also invokes ENUM_FIELDS where its macro replacement would create
 * declarations/statements inside a function. The following switch is still
 * recognized independently. Recovery target: a macro-generated statement
 * fragment rather than CSANE_STMT_OPAQUE. */
static void
test_generated_statement_macro_gap(void) {
    char *text =
        "int32 example(int32 value) {\n"
        "#if ENABLE\n"
        "#if CHAR_REPR\n"
        "#define XX(e) static char e##_name[] = #e;\n"
        "#define XX_2(e, a) static char e##_name[] = {a, '\\0'};\n"
        "#define XX(...) SELECT(XX_, __VA_ARGS__)\n"
        "\n"
        "    ENUM_FIELDS\n"
        "\n"
        "#undef XX\n"
        "#undef XX_2\n"
        "#endif\n"
        "    switch (value) {\n"
        "    default:\n"
        "        return 0;\n"
        "    }\n"
        "#endif\n"
        "    return value;\n"
        "}\n";
    TestStructure test;
    int32 function;
    test_init(&test, text, strlen32(text));
    function = test_function(&test, STRLIT("example"));
    CHECK(function >= 0);
    CHECK(test.top.opaque_count == 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_statement(&test, function, STRLIT("ENUM_FIELDS"),
                         CSANE_STMT_MACRO_FRAGMENT) >= 0);
    CHECK(test_statement(&test, function, STRLIT("switch (value)"),
                         CSANE_STMT_SWITCH) >= 0);
    CHECK(test_statement(&test, function, STRLIT("return value;"),
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_bad_top_level_expression(void) {
    char *text =
        "int broken = (1;\n"
        "static void first(void) { return; }\n"
        "static void second(void) { return; }\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test.top.opaque_count == 1);
    CHECK(test_function(&test, STRLIT("first")) >= 0);
    CHECK(test_function(&test, STRLIT("second")) >= 0);
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
    test_init(&test, text, strlen32(text));
    int32 recovered = test_function(&test, STRLIT("recovered"));
    CHECK(recovered >= 0);
    CHECK(test.top.opaque_count >= 1);
    CHECK(test_statement(&test, recovered, STRLIT("recovered_call()"),
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
    test_init(&test, text, strlen32(text));
    int32 damaged = test_function(&test, STRLIT("damaged"));
    int32 unaffected = test_function(&test, STRLIT("unaffected"));
    CHECK(damaged >= 0 && unaffected >= 0);
    CHECK(test_statement(&test, damaged, STRLIT("if (bad"),
                         CSANE_STMT_OPAQUE) >= 0);
    CHECK(test_statement(&test, damaged, STRLIT("following_call()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, unaffected, STRLIT("good_call()"),
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
    test_init(&test, text, strlen32(text));
    int32 function = test_function(&test, STRLIT("example"));
    CHECK(function >= 0);
    CHECK(test_statement(&test, function, STRLIT("Thing x ="),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, STRLIT("switch (x.value)"),
                         CSANE_STMT_SWITCH) >= 0);
    int32 label = test_statement(&test, function, STRLIT("case 1 ? 2 : 3:"),
                                 CSANE_STMT_CASE);
    CHECK(label >= 0);
    if (label >= 0) {
        SourceRange span = test.statements.items[label].source;
        CHECK(STREQUAL(test.tokens.text + span.start,
                       span.end - span.start, "case 1 ? 2 : 3:"));
    }
    CHECK(test_statement(&test, function, STRLIT("tail()"),
                         CSANE_STMT_SIMPLE) >= 0);
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
    test_init(&test, text, strlen32(text));
    int32 annotated = test_function(&test, STRLIT("annotated"));
    CHECK(annotated >= 0);
    CHECK(test_function(&test, STRLIT("ordinary")) >= 0);
    CHECK(test_statement(&test, annotated, STRLIT("annotated_call()"),
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
    test_init(&test, text, strlen32(text));
    int32 function = test_function(&test, STRLIT("example"));
    int32 another = test_function(&test, STRLIT("another"));
    CHECK(function >= 0 && another >= 0);
    CHECK(test_statement(&test, function, STRLIT("first_branch()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, STRLIT("second_branch()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, STRLIT("afterwards()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, another, STRLIT("call_other()"),
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
    test_init(&test, text, strlen32(text));
    int32 function = test_function(&test, STRLIT("example"));
    CHECK(function >= 0);
    CHECK(test_statement(&test, function, STRLIT("broken = (1;"),
                         CSANE_STMT_OPAQUE) >= 0);
    CHECK(test_statement(&test, function, STRLIT("good()"),
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
    test_init(&test, text, strlen32(text));
    int32 guarded = test_function(&test, STRLIT("guarded"));
    int32 platform = test_function(&test, STRLIT("platform"));
    int32 alternative = test_function(&test, STRLIT("alternative"));
    CHECK(test.top.opaque_count == 0);
    CHECK(guarded >= 0 && platform >= 0 && alternative >= 0);
    CHECK(test.statements.opaque_count == 0);
    CHECK(test_statement(&test, guarded, STRLIT("before()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("while (ready())"),
                         CSANE_STMT_WHILE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("first()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("second()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("third()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("fourth()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("fifth()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, guarded, STRLIT("after()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, platform, STRLIT("platform_call()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, alternative, STRLIT("alternative_call()"),
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
    test_init(&test, text, strlen32(text));
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
    test_init(&test, text, strlen32(text));
    CHECK(test_function(&test, STRLIT("run")) >= 0);
    CHECK(test_function(&test, STRLIT("ordinary")) >= 0);
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
    test_init(&test, text, strlen32(text));
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

/* Function names may be generated by macros without expanding them.
 * Pointer return types and trailing annotation macros do not change the
 * function/statement ownership of the body. */
static void
test_macro_generated_headers(void) {
    char *text =
        "char *ordinary_pointer(int32 x) { return NULL; }\n"
        "void *GENERATED_NAME(void) { return NULL; }\n"
        "void ATTR_GENERATED_NAME(void) { return; }\n"
        "XENUMS_LINKAGE char *\n"
        "CAT(PREFIX_, alias)(int32 x) { return NULL; }\n"
        "static char *\n"
        "CAT(CAT(inner_, SUFFIX), _value)(void) { return NULL; }\n"
        "void *(*callback)(void);\n"
        "void annotated(void) ATTR_LOCK(*lock)\n"
        "    ATTR_NO_THREAD_SAFETY_ANALYSIS { body_call(); }\n"
        "void reverse(void) ATTR_NO_THREAD_SAFETY_ANALYSIS\n"
        "    ATTR_LOCK(*lock) { reverse_call(); }\n"
        "void next(void) { next_call(); }\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    int32 functions = 0;
    int32 macro_names = 0;
    for (int32 i = 0; i < test.top.count; i += 1) {
        CsaneTopItem *item = &test.top.items[i];
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            functions += 1;
            if (item->name_token >= 0
                && TOKEN_IS(&test.tokens.tokens[item->name_token], "CAT")) {
                macro_names += 1;
            }
        }
    }
    CHECK(test.top.opaque_count == 0);
    CHECK(functions == 8);
    CHECK(macro_names == 2);
    CHECK(test_function(&test, STRLIT("ordinary_pointer")) >= 0);
    CHECK(test_function(&test, STRLIT("GENERATED_NAME")) >= 0);
    CHECK(test_function(&test, STRLIT("ATTR_GENERATED_NAME")) >= 0);
    int32 annotated = test_function(&test, STRLIT("annotated"));
    int32 reverse = test_function(&test, STRLIT("reverse"));
    CHECK(annotated >= 0 && reverse >= 0);
    CHECK(test_statement(&test, annotated, STRLIT("body_call()"),
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, reverse, STRLIT("reverse_call()"),
                         CSANE_STMT_SIMPLE) >= 0);
    test_free(&test);
    return;
}

static void
test_guarded_cpp_linkage(void) {
    char *text =
        "#if defined(__cplusplus)\n"
        "extern \"C\" {\n"
        "#endif\n"
        "char *api(void);\n"
        "#if defined(__cplusplus)\n"
        "}\n"
        "#endif\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test.top.opaque_count == 0);
    int32 linkage = 0;
    int32 functions = 0;
    for (int32 i = 0; i < test.top.count; i += 1) {
        linkage += test.top.items[i].kind == CSANE_TOP_LINKAGE_FRAGMENT;
        functions += test.top.items[i].kind == CSANE_TOP_FUNCTION_DECLARATION;
    }
    CHECK(linkage == 2);
    CHECK(functions == 1);
    test_free(&test);
    return;
}

static void
test_unmatched_linkage_stays_opaque(void) {
    char *text =
        "#if defined(__cplusplus)\n"
        "extern \"C\" {\n"
        "#endif\n"
        "char *api(void);\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test.top.opaque_count == 1);
    test_free(&test);
    return;
}

static void
test_unguarded_linkage_stays_opaque(void) {
    char *text =
        "#if OTHER_PLATFORM\n"
        "extern \"C\" {\n"
        "#endif\n"
        "char *api(void);\n"
        "#if OTHER_PLATFORM\n"
        "}\n"
        "#endif\n";
    TestStructure test;
    test_init(&test, text, strlen32(text));
    CHECK(test.top.opaque_count == 2);
    test_free(&test);
    return;
}

static void
test_bounded_names_and_fragments(void) {
    TestStructure test;
    char name[] = {'a', 'l', 'p', 'h', 'a', 'X',};
    char fragment[] = {'a', 'c', 't', 'i', 'o', 'n', '(', ')', 'X',};
    test_init(&test, STRLIT("void alpha(void) { action(); }"));
    int32 function = test_function(&test, name, 5);
    CHECK(function >= 0);
    CHECK(test_function(&test, name, 6) == -1);
    CHECK(test_statement(&test, function, fragment, 8,
                         CSANE_STMT_SIMPLE) >= 0);
    CHECK(test_statement(&test, function, fragment, 9,
                         CSANE_STMT_SIMPLE) == -1);
    CHECK(test_top_item(&test, name, 5,
                        CSANE_TOP_FUNCTION_DEFINITION) >= 0);
    CHECK(test_top_item(&test, name, 6,
                        CSANE_TOP_FUNCTION_DEFINITION) == -1);
    test_free(&test);
    return;
}

int
main(void) {
    test_bounded_names_and_fragments();
    test_conditional_call_argument_gap();
    test_conditional_if_header_gap();
    test_initializer_macro_gap();
    test_enum_macro_gap();
    test_generated_statement_macro_gap();
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
    test_macro_generated_headers();
    test_guarded_cpp_linkage();
    test_unmatched_linkage_stays_opaque();
    test_unguarded_linkage_stays_opaque();
    test_local_bad_statement();
    return failures > 0;
}
