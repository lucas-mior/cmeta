// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_PREPROC_ANALYSIS_C)
#define META_PREPROC_ANALYSIS_C

#if !defined(TESTING_meta_preproc_analysis)
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_meta_preproc_analysis 1
#else
#define TESTING_meta_preproc_analysis 0
#endif
#endif

#include "cbase.h"
#include "meta.h"

static void *
preproc_analysis_grow(void *items, int32 *capacity, int32 count,
                      int32 item_size) {
    int32 next;
    void *result;

    if (count < *capacity) {
        return items;
    }
    ASSERT_LT(*capacity, INT32_MAX/2);
    next = (*capacity == 0) ? 16 : *capacity*2;
    ASSERT_GT_VAR(next, *capacity);
    if (items == NULL) {
        result = malloc2((int64)next*item_size);
    } else {
        result = realloc2(items, *capacity, next, item_size);
    }
    *capacity = next;
    return result;
}

static int32
preproc_analysis_append_directive(CPreprocessorAnalysis *analysis,
                                  CPreprocessorDirective directive) {
    int32 index;

    index = analysis->directive_count;
    analysis->directives =
        preproc_analysis_grow(analysis->directives,
                              &analysis->directive_capacity, index,
                              SIZEOF(directive));
    analysis->directives[index] = directive;
    analysis->directive_count += 1;
    return index;
}

static int32
preproc_analysis_append_group(CPreprocessorAnalysis *analysis,
                              int32 parent_branch, int32 directive_index) {
    CPreprocessorGroup group = {0};
    int32 index;

    index = analysis->group_count;
    analysis->groups = preproc_analysis_grow(analysis->groups,
                                             &analysis->group_capacity,
                                             index, SIZEOF(group));
    group.source.start = analysis->directives[directive_index].source.start;
    group.source.end = analysis->tokenization->text_len;
    group.parent_branch = parent_branch;
    group.first_branch = -1;
    group.last_branch = -1;
    group.endif_directive = -1;
    analysis->groups[index] = group;
    analysis->group_count += 1;
    return index;
}

static int32
preproc_analysis_append_branch(CPreprocessorAnalysis *analysis,
                               int32 group_index, int32 directive_index) {
    CPreprocessorGroup *group;
    CPreprocessorBranch branch = {0};
    int32 index;

    index = analysis->branch_count;
    analysis->branches = preproc_analysis_grow(analysis->branches,
                                               &analysis->branch_capacity,
                                               index, SIZEOF(branch));
    group = &analysis->groups[group_index];
    branch.group_index = group_index;
    branch.parent_branch = group->parent_branch;
    branch.directive_index = directive_index;
    branch.next_branch = -1;
    branch.body_source.start =
        analysis->directives[directive_index].source.end;
    branch.body_source.end = analysis->tokenization->text_len;
    analysis->branches[index] = branch;
    if (group->last_branch >= 0) {
        analysis->branches[group->last_branch].next_branch = index;
    } else {
        group->first_branch = index;
    }
    group->last_branch = index;
    analysis->branch_count += 1;
    return index;
}

static void
preproc_analysis_add_issue(CPreprocessorAnalysis *analysis,
                           enum CPreprocessorIssueKind kind,
                           int32 directive_index) {
    int32 index;

    index = analysis->issue_count;
    analysis->issues = preproc_analysis_grow(analysis->issues,
                                             &analysis->issue_capacity,
                                             index, SIZEOF(*analysis->issues));
    analysis->issues[index] = (CPreprocessorIssue){kind, directive_index};
    analysis->issue_count += 1;
    return;
}

static void
preproc_analysis_mark_tokens(CPreprocessorAnalysis *analysis,
                             TokenRange range,
                             enum CPreprocessorTokenRole role,
                             int32 directive_index, int32 branch_index) {
    for (int32 i = range.first; i < range.end; i += 1) {
        analysis->token_roles[i] = role;
        analysis->token_directives[i] = directive_index;
        analysis->token_branches[i] = branch_index;
    }
    return;
}

static void
preproc_analysis_mark_define(CPreprocessorAnalysis *analysis,
                             int32 directive_index) {
    CPreprocessorDirective *directive;
    CPreprocessorDefine define;
    Tokenization *tokenization;
    int32 content_end;

    tokenization = analysis->tokenization;
    directive = &analysis->directives[directive_index];
    if (!c_preprocessor_define_info(tokenization, directive, &define)) {
        preproc_analysis_add_issue(analysis, C_PP_ISSUE_BAD_DEFINE,
                                   directive_index);
        return;
    }
    if (define.function_like) {
        for (int32 i = define.parameter_list.first;
             i < define.parameter_list.end; i += 1) {
            analysis->token_roles[i] = C_PP_TOKEN_MACRO_PARAMETERS;
        }
    }
    content_end = define.replacement_source.end;
    for (int32 i = define.name_token + 1; i < directive->tokens.end;
         i += 1) {
        Token *token = &tokenization->tokens[i];

        if (token->offset >= content_end) {
            break;
        }
        if (token->offset >= define.replacement_source.start) {
            analysis->token_roles[i] = C_PP_TOKEN_MACRO_REPLACEMENT;
        }
    }
    return;
}

static int32
preproc_analysis_advance_conditional(CPreprocessorAnalysis *analysis,
                                     int32 directive_index,
                                     int32 *current_group,
                                     int32 current_branch) {
    CPreprocessorDirective *directive;
    CPreprocessorGroup *group;
    enum CPreprocessorDirectiveKind kind;
    int32 group_index;

    directive = &analysis->directives[directive_index];
    kind = directive->kind;
    if ((kind == C_PREPROCESSOR_DIRECTIVE_IF)
        || (kind == C_PREPROCESSOR_DIRECTIVE_IFDEF)
        || (kind == C_PREPROCESSOR_DIRECTIVE_IFNDEF)) {
        group_index = preproc_analysis_append_group(analysis, current_branch,
                                                    directive_index);
        *current_group = group_index;
        return preproc_analysis_append_branch(analysis, group_index,
                                              directive_index);
    }
    if ((kind != C_PREPROCESSOR_DIRECTIVE_ELIF)
        && (kind != C_PREPROCESSOR_DIRECTIVE_ELSE)
        && (kind != C_PREPROCESSOR_DIRECTIVE_ENDIF)) {
        return current_branch;
    }
    if (*current_group < 0) {
        enum CPreprocessorIssueKind issue;

        issue = C_PP_ISSUE_STRAY_ENDIF;
        if (kind == C_PREPROCESSOR_DIRECTIVE_ELIF) {
            issue = C_PP_ISSUE_STRAY_ELIF;
        } else if (kind == C_PREPROCESSOR_DIRECTIVE_ELSE) {
            issue = C_PP_ISSUE_STRAY_ELSE;
        }
        preproc_analysis_add_issue(analysis, issue, directive_index);
        return current_branch;
    }

    group_index = *current_group;
    group = &analysis->groups[group_index];
    if (group->has_else && (kind != C_PREPROCESSOR_DIRECTIVE_ENDIF)) {
        enum CPreprocessorIssueKind issue;

        issue = (kind == C_PREPROCESSOR_DIRECTIVE_ELIF)
                ? C_PP_ISSUE_ELIF_AFTER_ELSE : C_PP_ISSUE_DUPLICATE_ELSE;
        preproc_analysis_add_issue(analysis, issue, directive_index);
        return current_branch;
    }
    ASSERT_GE(current_branch, 0);
    analysis->branches[current_branch].body_source.end =
        directive->source.start;
    if (kind == C_PREPROCESSOR_DIRECTIVE_ENDIF) {
        int32 parent;

        parent = group->parent_branch;
        group->endif_directive = directive_index;
        group->source.end = directive->source.end;
        *current_group = (parent >= 0)
                         ? analysis->branches[parent].group_index : -1;
        return parent;
    }
    if (kind == C_PREPROCESSOR_DIRECTIVE_ELSE) {
        group->has_else = true;
    }
    return preproc_analysis_append_branch(analysis, group_index,
                                          directive_index);
}

bool
c_preprocessor_analysis_init(CPreprocessorAnalysis *analysis,
                             Tokenization *tokenization) {
    int32 current_branch;
    int32 current_group;
    int32 cursor;
    int32 count;

    if (analysis == NULL) {
        return false;
    }
    *analysis = (CPreprocessorAnalysis){0};
    if ((tokenization == NULL) || (tokenization->text == NULL)
        || (tokenization->text_len < 0)
        || (tokenization->token_count < 0)) {
        return false;
    }
    analysis->tokenization = tokenization;
    count = tokenization->token_count;
    if (count > 0) {
        analysis->token_roles = malloc2((int64)count
                                        *SIZEOF(*analysis->token_roles));
        analysis->token_directives =
            malloc2((int64)count*SIZEOF(*analysis->token_directives));
        analysis->token_branches = malloc2((int64)count
                                           *SIZEOF(*analysis->token_branches));
    }
    current_branch = -1;
    current_group = -1;
    cursor = 0;
    for (int32 i = 0; i < count; i += 1) {
        CPreprocessorDirective directive;
        int32 directive_index;
        int32 directive_branch;

        if (!c_preprocessor_directive_at(tokenization, i, &directive)) {
            continue;
        }
        preproc_analysis_mark_tokens(analysis, (TokenRange){cursor, i},
                                     C_PP_TOKEN_CODE, -1, current_branch);
        directive_index = preproc_analysis_append_directive(analysis,
                                                            directive);
        directive_branch = current_branch;
        if (((directive.kind == C_PREPROCESSOR_DIRECTIVE_ELIF)
             || (directive.kind == C_PREPROCESSOR_DIRECTIVE_ELSE)
             || (directive.kind == C_PREPROCESSOR_DIRECTIVE_ENDIF))
            && (current_group >= 0)) {
            directive_branch = analysis->groups[current_group].parent_branch;
        }
        preproc_analysis_mark_tokens(analysis, directive.tokens,
                                     C_PP_TOKEN_DIRECTIVE, directive_index,
                                     directive_branch);
        if (directive.kind == C_PREPROCESSOR_DIRECTIVE_DEFINE) {
            preproc_analysis_mark_define(analysis, directive_index);
        }
        current_branch =
            preproc_analysis_advance_conditional(analysis, directive_index,
                                                 &current_group,
                                                 current_branch);
        cursor = directive.tokens.end;
        i = cursor - 1;
    }
    preproc_analysis_mark_tokens(analysis, (TokenRange){cursor, count},
                                 C_PP_TOKEN_CODE, -1, current_branch);
    while (current_group >= 0) {
        CPreprocessorGroup *group = &analysis->groups[current_group];
        int32 parent;

        preproc_analysis_add_issue(analysis, C_PP_ISSUE_UNCLOSED_IF,
                                   analysis->branches[group->first_branch]
                                       .directive_index);
        parent = group->parent_branch;
        current_group = (parent >= 0)
                         ? analysis->branches[parent].group_index : -1;
    }
    for (int32 i = 0; i < analysis->branch_count; i += 1) {
        CPreprocessorBranch *branch = &analysis->branches[i];

        branch->body_tokens.first =
            tokenization_token_at_or_after_offset(tokenization,
                                                  branch->body_source.start);
        branch->body_tokens.end =
            tokenization_token_at_or_after_offset(tokenization,
                                                  branch->body_source.end);
    }
    return analysis->issue_count == 0;
}

void
c_preprocessor_analysis_free(CPreprocessorAnalysis *analysis) {
    if (analysis == NULL) {
        return;
    }
    free2(analysis->directives,
          (int64)analysis->directive_capacity*SIZEOF(*analysis->directives));
    free2(analysis->branches,
          (int64)analysis->branch_capacity*SIZEOF(*analysis->branches));
    free2(analysis->groups,
          (int64)analysis->group_capacity*SIZEOF(*analysis->groups));
    free2(analysis->issues,
          (int64)analysis->issue_capacity*SIZEOF(*analysis->issues));
    if (analysis->tokenization != NULL) {
        int32 count = analysis->tokenization->token_count;

        free2(analysis->token_directives,
              (int64)count*SIZEOF(*analysis->token_directives));
        free2(analysis->token_branches,
              (int64)count*SIZEOF(*analysis->token_branches));
        free2(analysis->token_roles,
              (int64)count*SIZEOF(*analysis->token_roles));
    }
    *analysis = (CPreprocessorAnalysis){0};
    return;
}

bool
c_preprocessor_token_context(CPreprocessorAnalysis *analysis,
                             int32 token_index,
                             enum CPreprocessorTokenRole *role,
                             int32 *directive_index, int32 *branch_index) {
    if ((analysis == NULL) || (analysis->tokenization == NULL)
        || (token_index < 0)
        || (token_index >= analysis->tokenization->token_count)) {
        return false;
    }
    if (role != NULL) {
        *role = analysis->token_roles[token_index];
    }
    if (directive_index != NULL) {
        *directive_index = analysis->token_directives[token_index];
    }
    if (branch_index != NULL) {
        *branch_index = analysis->token_branches[token_index];
    }
    return true;
}

#if TESTING_meta_preproc_analysis
#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"

static int32
preproc_test_find(Tokenization *tokenization, char *word, int32 occurrence) {
    TokenRange all = {0, tokenization->token_count};
    int32 cursor = -1;

    for (int32 i = 0; i <= occurrence; i += 1) {
        cursor = token_range_next_text(tokenization, all, cursor,
                                       word, strlen32(word));
    }
    ASSERT_LT_VAR(cursor, tokenization->token_count);
    return cursor;
}

static void
preproc_test_nested_conditionals(void) {
    char *text = "#if X\n"
                 "int first;\n"
                 "#if Y\n"
                 "int nested;\n"
                 "#else\n"
                 "int alternate;\n"
                 "#endif\n"
                 "#elif Z\n"
                 "int second;\n"
                 "#else\n"
                 "int third;\n"
                 "#endif\n"
                 "int outside;\n";
    Tokenization tokens = tokenize(text, strlen32(text));
    CPreprocessorAnalysis analysis;
    int32 index;
    int32 branch;
    int32 directive;
    enum CPreprocessorTokenRole role;

    ASSERT(c_preprocessor_analysis_init(&analysis, &tokens));
    ASSERT_EQ(analysis.directive_count, 7);
    ASSERT_EQ(analysis.group_count, 2);
    ASSERT_EQ(analysis.branch_count, 5);
    ASSERT_ZERO(analysis.issue_count);
    ASSERT_EQ(analysis.groups[0].first_branch, 0);
    ASSERT_EQ(analysis.groups[0].last_branch, 4);
    ASSERT_EQ(analysis.groups[1].parent_branch, 0);
    ASSERT_EQ(analysis.branches[0].next_branch, 3);
    ASSERT_EQ(analysis.branches[3].next_branch, 4);
    ASSERT_EQ(analysis.branches[1].next_branch, 2);
    ASSERT_EQ(analysis.groups[0].endif_directive, 6);
    ASSERT_EQ(analysis.groups[1].endif_directive, 3);

    index = preproc_test_find(&tokens, "nested", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_CODE);
    ASSERT_EQ(directive, -1);
    ASSERT_EQ(branch, 1);
    index = preproc_test_find(&tokens, "alternate", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(branch, 2);
    index = preproc_test_find(&tokens, "second", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(branch, 3);
    index = preproc_test_find(&tokens, "outside", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(branch, -1);
    index = preproc_test_find(&tokens, "elif", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_DIRECTIVE);
    ASSERT_EQ(branch, -1);
    ASSERT_EQ(directive, 4);
    ASSERT(!c_preprocessor_token_context(&analysis, -1, NULL, NULL, NULL));
    ASSERT(!c_preprocessor_token_context(&analysis, tokens.token_count,
                                         NULL, NULL, NULL));
    c_preprocessor_analysis_free(&analysis);
    free_tokenization(&tokens);
    return;
}

static void
preproc_test_macro_regions(void) {
    char *text = "#define ADD(a, \\\n"
                 " b) ((a) + (b))\n"
                 "#define OBJ (3)\n"
                 "#define EMPTY\n"
                 "int code;\n";
    Tokenization tokens = tokenize_with_flags(text, strlen32(text),
                                              TOKENIZE_SKIP_WHITESPACE);
    CPreprocessorAnalysis analysis;
    enum CPreprocessorTokenRole role;
    int32 directive;
    int32 branch;
    int32 index;

    ASSERT(c_preprocessor_analysis_init(&analysis, &tokens));
    ASSERT_EQ(analysis.directive_count, 3);
    ASSERT_ZERO(analysis.group_count);
    index = preproc_test_find(&tokens, "b", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_MACRO_PARAMETERS);
    ASSERT_ZERO(directive);
    index = preproc_test_find(&tokens, "+", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_MACRO_REPLACEMENT);
    index = preproc_test_find(&tokens, "OBJ", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_DIRECTIVE);
    index = preproc_test_find(&tokens, "3", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_MACRO_REPLACEMENT);
    ASSERT_EQ(directive, 1);
    index = preproc_test_find(&tokens, "code", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_CODE);
    ASSERT_EQ(directive, -1);
    c_preprocessor_analysis_free(&analysis);
    free_tokenization(&tokens);
    return;
}

static void
preproc_test_malformed(void) {
    char *text = "#else\n"
                 "#endif\n"
                 "#if X\n"
                 "#else\n"
                 "#elif Y\n"
                 "#else\n"
                 "#endif\n"
                 "#if OPEN\n"
                 "#define (oops) x\n";
    Tokenization tokens = tokenize(text, strlen32(text));
    CPreprocessorAnalysis analysis;

    ASSERT(!c_preprocessor_analysis_init(&analysis, &tokens));
    ASSERT_EQ(analysis.issue_count, 6);
    ASSERT_EQ(analysis.issues[0].kind, C_PP_ISSUE_STRAY_ELSE);
    ASSERT_EQ(analysis.issues[1].kind, C_PP_ISSUE_STRAY_ENDIF);
    ASSERT_EQ(analysis.issues[2].kind, C_PP_ISSUE_ELIF_AFTER_ELSE);
    ASSERT_EQ(analysis.issues[3].kind, C_PP_ISSUE_DUPLICATE_ELSE);
    ASSERT_EQ(analysis.issues[4].kind, C_PP_ISSUE_BAD_DEFINE);
    ASSERT_EQ(analysis.issues[5].kind, C_PP_ISSUE_UNCLOSED_IF);
    ASSERT_EQ(analysis.groups[1].source.end, tokens.text_len);
    c_preprocessor_analysis_free(&analysis);
    free_tokenization(&tokens);
    return;
}

static void
preproc_test_comments_and_digraph(void) {
    char *text = "/*\n"
                 "#if WRONG\n"
                 "*/\n"
                 "char *s = \"#if WRONG\";\n"
                 "int a; #if WRONG\n"
                 "%:if defined(X)\r\n"
                 "int good;\r\n"
                 "%:endif\r\n";
    Tokenization tokens = tokenize(text, strlen32(text));
    CPreprocessorAnalysis analysis;

    ASSERT(c_preprocessor_analysis_init(&analysis, &tokens));
    ASSERT_EQ(analysis.directive_count, 2);
    ASSERT_EQ(analysis.group_count, 1);
    ASSERT_EQ(analysis.branch_count, 1);
    ASSERT_EQ(analysis.groups[0].source.end, tokens.text_len);
    c_preprocessor_analysis_free(&analysis);
    free_tokenization(&tokens);
    return;
}

int
main(void) {
    preproc_test_nested_conditionals();
    preproc_test_macro_regions();
    preproc_test_malformed();
    preproc_test_comments_and_digraph();
    return 0;
}
#endif /* TESTING_meta_preproc_analysis */

#endif /* META_PREPROC_ANALYSIS_C */
