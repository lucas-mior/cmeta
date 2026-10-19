// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_H)
#define CSANECHECK_H

#include "cbase.h"
#include "meta.h"
#include "csanecheck_regions.h"
#include "csanecheck_toplevel.h"
#include "csanecheck_classify.h"
#if OS_UNIX
#include <regex.h>
#endif

typedef struct CsanePaths {
    char **items;
    int32 count;
    int32 capacity;
} CsanePaths;

typedef struct CsanePattern {
#if OS_UNIX
    regex_t compiled;
#endif
    char *text;
    char *rule_id;
    int32 columns;
} CsanePattern;

typedef struct CsanePatterns {
    CsanePattern *items;
    int32 count;
    int32 capacity;
} CsanePatterns;

typedef struct CsanePolicy {
    CsanePaths never_names;
    CsanePaths cbase_names;
    CsanePaths cbase_roots;
    CsanePatterns excludes;
    CsanePatterns no_columns;
    CsanePatterns column_overrides;
    CsanePatterns rule_exemptions;
    int32 default_columns;
} CsanePolicy;

typedef struct CsaneOptions {
    CsanePaths files;
    CsanePolicy policy;
    char *filter;
    int32 max_depth;
    bool report_parse;
    bool strict_parse;
    bool statistics;
    bool coverage;
    bool rule_coverage;
} CsaneOptions;

/* Syntax-only macro projection. Source text and token indices stay borrowed
 * from the original tokenization; only selected token spellings differ.
 */
typedef struct CsaneMacroView {
    Tokenization tokens;
    Token *copy;
    char **owned;
    int32 owned_count;
    int32 projected_macros;
    int32 projected_tokens;
} CsaneMacroView;

typedef struct CsaneCoverage {
    int32 declarations;
    int32 function_declarations;
    int32 function_definitions;
    int32 statements;
    int32 expressions;
    int32 unsupported_statements;
    int32 unsupported_externals;
    int32 malformed_issues;
    int32 unsupported_issues;
} CsaneCoverage;

/* Conservative, file-level coverage estimate for AST-dependent rules.
 * Unresolved syntax and macro replacement tokens are never counted as
 * structurally checked merely because another pass inspected the file.
 */
typedef struct CsaneAnalysisCoverage {
    int32 eligible_tokens;
    int32 parsed_tokens;
    int32 unresolved_tokens;
    int32 macro_tokens;
    int32 parse_issues;
    int32 preprocessor_issues;
    bool preprocessor_ready;
    bool ast_ready;
} CsaneAnalysisCoverage;

enum CsanePass {
    CSANE_PASS_SOURCE = 0,
    CSANE_PASS_TOKEN,
    CSANE_PASS_SYNTAX,
    CSANE_PASS_CONTEXT,
};

typedef struct CsaneStats {
    int32 files;
    int64 tokens;
    int64 externals;
    int64 parse_issues;
    int64 preprocessor_issues;
    int64 violations;
    int64 declarations;
    int64 function_declarations;
    int64 function_definitions;
    int64 statements;
    int64 expressions;
    int64 unsupported_statements;
    int64 unsupported_externals;
    int64 malformed_issues;
    int64 unsupported_issues;
} CsaneStats;

/* A rule can examine any layer independently of successful AST parsing.
 * Every reference is borrowed and valid only during its source-file check.
 */
typedef struct CsaneFile {
    char *path;
    Tokenization *tokens;
    CPreprocessorAnalysis *preprocessor;
    CsaneRegions *regions;
    CsaneTopLevels *toplevel;
    CsaneStatements *statements;
    CAst *ast;
    int32 violations;
    int32 column_limit;
    bool in_cbase;
    bool is_ryu;
    CsanePolicy *policy;
} CsaneFile;

typedef struct CsaneRule {
    char *id;
    void (*check)(CsaneFile *);
    enum CsanePass pass;
    bool include_fixtures;
} CsaneRule;

bool csane_policy_init(CsanePolicy *);
void csane_policy_free(CsanePolicy *);
bool csane_policy_config(CsanePolicy *, char *);
bool csane_policy_add_regex(CsanePatterns *, char *, char *, int32);
bool csane_policy_add_root(CsanePolicy *, char *);
bool csane_policy_project(CsanePolicy *, char *);
bool csane_policy_source(CsanePolicy *, char *);
bool csane_policy_in_cbase(CsanePolicy *, char *);
bool csane_policy_in_ryu(CsanePolicy *, char *);
bool csane_policy_match(CsanePatterns *, char *);
bool csane_policy_rule_allowed(CsanePolicy *, char *, char *);
int32 csane_policy_columns(CsanePolicy *, char *);
bool csane_policy_filter(char *, char *);
bool csane_policy_selected(CsanePolicy *, char *, char *);
CsaneCoverage csane_coverage_collect(CAst *);
void csane_declarations_check(CsaneFile *);
void csane_declarations_headers(CsaneFile *);
void csane_control_check(CsaneFile *);
void csane_context_check(CsaneFile *);
void csane_layout_check(CsaneFile *);
void csane_calls_token_check(CsaneFile *);
void csane_wrap_check(CsaneFile *);
void csane_wrap_call_check(CsaneFile *);
void csane_rules_prepare(CsaneFile *);
void csane_rules_run(CsaneFile *, enum CsanePass);
void csane_rules_report(CsaneFile *, CsaneAnalysisCoverage *);
CsaneAnalysisCoverage csane_analysis_coverage(CsaneFile *, bool, bool);
void csane_rule_issue(CsaneFile *, int32, char *, char *);

bool csane_paths_add(CsanePaths *, char *);
void csane_paths_sort_unique(CsanePaths *);
void csane_paths_free(CsanePaths *);
bool csane_collect_root(CsanePaths *, char *, int32);
bool csane_collect_explicit(CsanePaths *, char *);
bool csane_collect_file_list(CsanePaths *, char *);
int32 csane_run(int32, char **);

#endif /* CSANECHECK_H */
