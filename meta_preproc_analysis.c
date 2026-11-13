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

static int32
preproc_analysis_append_branch(CPreprocessorAnalysis *analysis,
                               int32 group_index, int32 directive_index) {
    CPreprocessorGroup *group;
    CPreprocessorBranch branch = {0};
    int32 index = ARRAY_LEN(analysis->branches);

    group = &analysis->groups[group_index];
    branch.group_index = group_index;
    branch.parent_branch = group->parent_branch;
    branch.directive_index = directive_index;
    branch.next_branch = -1;
    branch.body_source.start = analysis->directives[directive_index].source.end;
    branch.body_source.end = analysis->tokenization->text_len;

    ARRAY_PUSH(analysis->branches, branch);
    if (group->last_branch >= 0) {
        analysis->branches[group->last_branch].next_branch = index;
    } else {
        group->first_branch = index;
    }
    group->last_branch = index;

    return index;
}

static void
preproc_analysis_add_issue(CPreprocessorAnalysis *analysis,
                           enum CPreprocessorIssueKind kind,
                           int32 directive_index) {
    ARRAY_PUSH(analysis->issues, (CPreprocessorIssue){kind, directive_index});
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
        || (ARRAY_LEN(tokenization->tokens) < 0)) {
        return false;
    }
    analysis->tokenization = tokenization;
    count = ARRAY_LEN(tokenization->tokens);
    if (count > 0) {
        analysis->token_roles = malloc2(count*SIZEOF(*analysis->token_roles));
        analysis->token_directives =
            malloc2(count*SIZEOF(*analysis->token_directives));
        analysis->token_branches =
            malloc2(count*SIZEOF(*analysis->token_branches));
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
        directive_index = ARRAY_LEN(analysis->directives);
        ARRAY_PUSH(analysis->directives, directive);
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
            CPreprocessorDirective *define_directive;
            CPreprocessorDefine define;
            int32 content_end;

            define_directive = &analysis->directives[directive_index];
            if (!c_preprocessor_define_info(tokenization, define_directive,
                                            &define)) {
                preproc_analysis_add_issue(analysis, C_PP_ISSUE_BAD_DEFINE,
                                           directive_index);
            } else {
                if (define.function_like) {
                    for (int32 j = define.parameter_list.first;
                         j < define.parameter_list.end; j += 1) {
                        analysis->token_roles[j] = C_PP_TOKEN_MACRO_PARAMETERS;
                    }
                }
                content_end = define.replacement_source.end;
                for (int32 j = define.name_token + 1;
                     j < define_directive->tokens.end; j += 1) {
                    Token *token = &tokenization->tokens[j];

                    if (token->offset >= content_end) {
                        break;
                    }
                    if (token->offset >= define.replacement_source.start) {
                        analysis->token_roles[j] = C_PP_TOKEN_MACRO_REPLACEMENT;
                    }
                }
            }
        }
        if ((directive.kind == C_PREPROCESSOR_DIRECTIVE_IF)
            || (directive.kind == C_PREPROCESSOR_DIRECTIVE_IFDEF)
            || (directive.kind == C_PREPROCESSOR_DIRECTIVE_IFNDEF)) {
            CPreprocessorGroup group = {0};

            current_group = ARRAY_LEN(analysis->groups);
            group.source.start =
                analysis->directives[directive_index].source.start;
            group.source.end = analysis->tokenization->text_len;
            group.parent_branch = current_branch;
            group.first_branch = -1;
            group.last_branch = -1;
            group.endif_directive = -1;
            ARRAY_PUSH(analysis->groups, group);
            current_branch =
                preproc_analysis_append_branch(analysis, current_group,
                                               directive_index);
        } else if ((directive.kind == C_PREPROCESSOR_DIRECTIVE_ELIF)
                   || (directive.kind == C_PREPROCESSOR_DIRECTIVE_ELSE)
                   || (directive.kind == C_PREPROCESSOR_DIRECTIVE_ENDIF)) {
            if (current_group < 0) {
                enum CPreprocessorIssueKind issue;

                issue = C_PP_ISSUE_STRAY_ENDIF;
                if (directive.kind == C_PREPROCESSOR_DIRECTIVE_ELIF) {
                    issue = C_PP_ISSUE_STRAY_ELIF;
                } else if (directive.kind == C_PREPROCESSOR_DIRECTIVE_ELSE) {
                    issue = C_PP_ISSUE_STRAY_ELSE;
                }
                preproc_analysis_add_issue(analysis, issue, directive_index);
            } else {
                CPreprocessorGroup *group = &analysis->groups[current_group];

                if (group->has_else
                    && (directive.kind != C_PREPROCESSOR_DIRECTIVE_ENDIF)) {
                    enum CPreprocessorIssueKind issue;

                    issue = (directive.kind
                             == C_PREPROCESSOR_DIRECTIVE_ELIF)
                            ? C_PP_ISSUE_ELIF_AFTER_ELSE
                            : C_PP_ISSUE_DUPLICATE_ELSE;
                    preproc_analysis_add_issue(analysis, issue,
                                               directive_index);
                } else {
                    ASSERT_GE(current_branch, 0);
                    analysis->branches[current_branch].body_source.end =
                        analysis->directives[directive_index].source.start;
                    if (directive.kind == C_PREPROCESSOR_DIRECTIVE_ENDIF) {
                        int32 parent = group->parent_branch;

                        group->endif_directive = directive_index;
                        group->source.end =
                            analysis->directives[directive_index].source.end;
                        current_group =
                            (parent >= 0)
                            ? analysis->branches[parent].group_index : -1;
                        current_branch = parent;
                    } else {
                        if (directive.kind == C_PREPROCESSOR_DIRECTIVE_ELSE) {
                            group->has_else = true;
                        }
                        current_branch =
                            preproc_analysis_append_branch(analysis,
                                                           current_group,
                                                           directive_index);
                    }
                }
            }
        }
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
    for (int32 i = 0; i < ARRAY_LEN(analysis->branches); i += 1) {
        CPreprocessorBranch *branch = &analysis->branches[i];

        branch->body_tokens.first =
            tokenization_token_at_or_after_offset(tokenization,
                                                  branch->body_source.start);
        branch->body_tokens.end =
            tokenization_token_at_or_after_offset(tokenization,
                                                  branch->body_source.end);
    }
    return ARRAY_LEN(analysis->issues) == 0;
}

void
c_preprocessor_analysis_free(CPreprocessorAnalysis *analysis) {
    if (analysis == NULL) {
        return;
    }
    ARRAY_FREE(analysis->directives);
    ARRAY_FREE(analysis->branches);
    ARRAY_FREE(analysis->groups);
    ARRAY_FREE(analysis->issues);
    if (analysis->tokenization != NULL) {
        int32 count = ARRAY_LEN(analysis->tokenization->tokens);

        free2(analysis->token_directives,
              count*SIZEOF(*analysis->token_directives));
        free2(analysis->token_branches,
              count*SIZEOF(*analysis->token_branches));
        free2(analysis->token_roles,
              count*SIZEOF(*analysis->token_roles));
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
        || (token_index >= ARRAY_LEN(analysis->tokenization->tokens))) {
        return false;
    }
    if (role) {
        *role = analysis->token_roles[token_index];
    }
    if (directive_index) {
        *directive_index = analysis->token_directives[token_index];
    }
    if (branch_index) {
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
    TokenRange all = {0, ARRAY_LEN(tokenization->tokens)};
    int32 cursor = -1;

    for (int32 i = 0; i <= occurrence; i += 1) {
        cursor = token_range_next_text(tokenization, all, cursor,
                                       word, strlen32(word));
    }
    ASSERT_LT_VAR(cursor, ARRAY_LEN(tokenization->tokens));
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
    ASSERT_EQ(ARRAY_LEN(analysis.directives), 7);
    ASSERT_EQ(ARRAY_LEN(analysis.groups), 2);
    ASSERT_EQ(ARRAY_LEN(analysis.branches), 5);
    ASSERT_EQ(ARRAY_LEN(analysis.issues), 0);
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
    ASSERT(!c_preprocessor_token_context(&analysis, ARRAY_LEN(tokens.tokens),
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
    ASSERT_EQ(ARRAY_LEN(analysis.directives), 3);
    ASSERT_EQ(ARRAY_LEN(analysis.groups), 0);
    index = preproc_test_find(&tokens, "b", 0);
    ASSERT(c_preprocessor_token_context(&analysis, index, &role, &directive,
                                        &branch));
    ASSERT_EQ(role, C_PP_TOKEN_MACRO_PARAMETERS);
    ASSERT_EQ(directive, 0);
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
    ASSERT_EQ(ARRAY_LEN(analysis.issues), 6);
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
    ASSERT_EQ(ARRAY_LEN(analysis.directives), 2);
    ASSERT_EQ(ARRAY_LEN(analysis.groups), 1);
    ASSERT_EQ(ARRAY_LEN(analysis.branches), 1);
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
