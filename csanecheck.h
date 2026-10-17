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
} CsaneStats;

bool csane_paths_add(CsanePaths *, char *);
void csane_paths_sort_unique(CsanePaths *);
void csane_paths_free(CsanePaths *);
bool csane_collect_root(CsanePaths *, char *, int32);
bool csane_collect_explicit(CsanePaths *, char *);
bool csane_collect_file_list(CsanePaths *, char *);
int32 csane_run(int32, char **);

#endif /* CSANECHECK_H */
