// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_H)
#define CSANECHECK_H

#include "cbase.h"
#include "meta.h"
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
} CsaneOptions;

typedef struct CsaneStats {
    int32 files;
    int64 tokens;
    int64 externals;
    int64 parse_issues;
    int64 preprocessor_issues;
    int64 violations;
} CsaneStats;

/* A rule can examine any layer independently of successful AST parsing.
 * Every reference is borrowed and valid only during its source-file check.
 */
typedef struct CsaneFile {
    char *path;
    Tokenization *tokens;
    CPreprocessorAnalysis *preprocessor;
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
int32 csane_rules_check(CsaneFile *);
void csane_rule_issue(CsaneFile *, int32, char *, char *);

bool csane_paths_add(CsanePaths *, char *);
void csane_paths_sort_unique(CsanePaths *);
void csane_paths_free(CsanePaths *);
bool csane_collect_root(CsanePaths *, char *, int32);
bool csane_collect_explicit(CsanePaths *, char *);
bool csane_collect_file_list(CsanePaths *, char *);
int32 csane_run(int32, char **);

#endif /* CSANECHECK_H */
