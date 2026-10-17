// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_H)
#define CSANECHECK_H

#include "cbase.h"
#include "meta.h"

typedef struct CsanePaths {
    char **items;
    int32 count;
    int32 capacity;
} CsanePaths;

typedef struct CsaneOptions {
    CsanePaths files;
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
} CsaneFile;

typedef struct CsaneRule {
    char *id;
    void (*check)(CsaneFile *);
    bool include_fixtures;
} CsaneRule;

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
