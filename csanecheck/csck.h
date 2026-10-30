// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_H)
#define CSANECHECK_H

#include "cbase.h"
#include "meta.h"
#include "csck_regions.h"
#include "csck_toplevel.h"
#include "csck_classify.h"
#include "csck_type_hints.h"
#if OS_UNIX
#include <regex.h>
#endif

typedef struct CsanePaths {
    StrFlexList strings;
} CsanePaths;

typedef struct CsanePattern {
#if OS_UNIX
    regex_t compiled;
    regex_t function_compiled;
#endif
    char *rule_id;
    int32 rule_id_len;
    bool has_function;
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
    CsanePaths project_roots;
    CsanePaths configs;
    CsanePatterns excludes;
    CsanePatterns no_columns;
    CsanePatterns column_overrides;
    CsanePatterns rule_exemptions;
    CsanePatterns macro_exemptions;
    CsanePatterns typedef_exemptions;
    int32 default_columns;
} CsanePolicy;

typedef struct CsaneOptions {
    CsanePaths files;
    CsanePaths source_roots;
    CsanePaths skip_dirs;
    CsanePolicy policy;
    char *filter;
    int32 filter_len;
    int32 max_depth;
    bool report_parse;
    bool strict_parse;
    bool require_coverage;
    bool statistics;
    bool coverage;
    bool rule_coverage;
} CsaneOptions;

/* Counts describe local source structures, never translation-unit AST nodes. */
typedef struct CsaneCoverage {
    int32 declarations;
    int32 function_decls;
    int32 function_defs;
    int32 statements;
    int32 opaque_statements;
    int32 opaque_externals;
} CsaneCoverage;

/* Coverage targets are scoped to the construct a rule actually inspects.
 * A malformed construct is counted as unresolved only for affected rules.
 */
enum CsaneCoverageState {
    CSANE_COVERAGE_CHECKED = 1,
    CSANE_COVERAGE_EXCLUDED,
    CSANE_COVERAGE_UNRESOLVED,
};

typedef struct CsaneRuleCounts {
    int32 eligible;
    int32 checked;
    int32 excluded;
    int32 unresolved;
} CsaneRuleCounts;

enum CsaneTargetKind {
    CSANE_TARGET_LINES = 1,
    CSANE_TARGET_DIRECTIVES,
    CSANE_TARGET_SIZEOF,
    CSANE_TARGET_APIS,
    CSANE_TARGET_STYLE_CALLS,
    CSANE_TARGET_ASSIGNMENTS,
    CSANE_TARGET_DECLARATIONS,
    CSANE_TARGET_STATEMENTS,
    CSANE_TARGET_FUNCTIONS,
    CSANE_TARGET_CALLS,
    CSANE_TARGET_OPERATORS,
    CSANE_TARGET_DECL_ONE,
    CSANE_TARGET_FUNC_HEADERS,
    CSANE_TARGET_CONTROL,
    CSANE_TARGET_STYLE_STATUS,
    CSANE_TARGET_CALL_WRAP,
    CSANE_TARGET_ASSIGN_WRAP,
};

enum CsanePass {
    CSANE_PASS_SOURCE = 0,
    CSANE_PASS_TOKEN,
};

typedef struct CsaneStats {
    int32 files;
    int64 tokens;
    int64 externals;
    int64 structural_issues;
    int64 preprocessor_issues;
    int64 violations;
    int64 declarations;
    int64 function_decls;
    int64 function_defs;
    int64 statements;
    int64 opaque_statements;
    int64 opaque_externals;
} CsaneStats;

/* Cache entries are private to one invocation's checker and policy state.
 * A failed cache operation simply falls back to normal analysis.
 */
#if OS_UNIX
typedef struct CsaneCacheRecord {
    uint64 magic;
    uint64 fingerprint;
    uint64 source_size;
    uint64 source_mtime;
    uint64 source_ctime;
    CsaneStats delta;
    int32 status;
    uint32 output_len;
} CsaneCacheRecord;
#endif

/* A rule can examine any layer independently of successful AST parsing.
 * Every reference is borrowed and valid only during its source-file check.
 */
typedef struct CsaneFile {
    char *path;
    int32 path_len;
    Tokenization *tokens;
    CPreprocessorAnalysis *preprocessor;
    CsaneRegions *regions;
    CsaneTopLevels *toplevel;
    CsaneStatements *statements;
    CsaneTypeHints *type_hints;
    int32 violations;
    int32 column_limit;
    bool in_cbase;
    bool is_ryu;
    bool preprocessor_ok;
    CsanePolicy *policy;
} CsaneFile;

typedef struct CsaneRule {
    char *id;
    int32 id_len;
    void (*check)(CsaneFile *);
    enum CsanePass pass;
    enum CsaneTargetKind target;
    bool include_fixtures;
} CsaneRule;

bool csane_policy_init(CsanePolicy *);
void csane_policy_free(CsanePolicy *);
bool csane_policy_config(CsanePolicy *, char *, int32);
bool csane_policy_add_regex(CsanePatterns *, char *, int32, char *, int32,
                            char *, int32, int32);
bool csane_policy_add_root(CsanePolicy *, char *, int32);
bool csane_policy_project(CsanePolicy *, char *, int32);
bool csane_policy_source(CsanePolicy *, char *, int32);
bool csane_policy_in_cbase(CsanePolicy *, char *, int32);
bool csane_policy_in_ryu(CsanePolicy *, char *, int32);
bool csane_policy_match(CsanePatterns *, char *, int32);
bool csane_policy_rule_allowed(CsanePolicy *, char *, int32,
                               char *, int32, Token *);
bool csane_policy_macro_allowed(CsanePolicy *, char *, int32, char *,
                                int32, Token *);
bool csane_policy_typedef_allowed(CsanePolicy *, char *, int32,
                                  char *, int32, Token *);
int32 csane_policy_columns(CsanePolicy *, char *, int32);
bool csane_policy_filter(char *, int32, char *, int32);
bool csane_policy_selected(CsanePolicy *, char *, int32, char *, int32);
CsaneCoverage csane_coverage_collect(CsaneTopLevels *,
                                     CsaneStatements *);
void csane_coverage_report_structural(char *, int32, Tokenization *,
                                      CsaneTopLevels *, CsaneStatements *);
void csane_declarations_check(CsaneFile *);
void csane_declarations_headers(CsaneFile *);
void csane_control_check(CsaneFile *);
void csane_bounds_check(CsaneFile *);
void csane_status_check(CsaneFile *);
void csane_expression_style_check(CsaneFile *);
/* Shared, local operator classification for diagnostics and coverage.
 * DECLARATOR is shallow syntactic evidence only; unresolved syntax remains
 * distinct from recognized non-operator uses. */
enum CsaneLayoutOperator {
    CSANE_LAYOUT_SKIP = 0,
    CSANE_LAYOUT_DECLARATOR,
    CSANE_LAYOUT_MULTIPLY,
    CSANE_LAYOUT_BINARY,
    CSANE_LAYOUT_ASSIGN,
    CSANE_LAYOUT_UNRESOLVED,
};
enum CsaneApiUse {
    CSANE_API_NOT_CALL = 0,
    CSANE_API_DIRECT_CALL,
    CSANE_API_UNRESOLVED,
};
enum CsaneApiUse csane_api_classify(CsaneFile *, int32);
enum CsaneLayoutOperator csane_layout_classify(CsaneFile *, int32);
void csane_layout_check(CsaneFile *);
void csane_layout_assignment_check(CsaneFile *);
bool csane_calls_code(CsaneFile *, int32);
bool csane_calls_generated(char *, int32);
SourceLocation csane_calls_location(CsaneFile *, int32);
void csane_calls_token_check(CsaneFile *);
void csane_wrap_check(CsaneFile *);
void csane_wrap_call_check(CsaneFile *);
int32 csane_wrap_flat_length(char *, int32, int32);
void csane_rules_prepare(CsaneFile *);
void csane_rules_run(CsaneFile *, enum CsanePass);
int32 csane_rules_incomplete(CsaneFile *, bool, bool, CsaneRuleCounts *);
CsaneRuleCounts csane_coverage_rule(CsaneFile *, enum CsaneTargetKind,
                                     char *, int32, bool);
bool csane_token_name_in(Token *, CsanePaths *);
void csane_rule_issue(CsaneFile *, int32, char *, int32, char *);

bool csane_paths_add(CsanePaths *, char *, int32);
void csane_paths_sort_unique(CsanePaths *);
void csane_paths_free(CsanePaths *);
bool csane_source_allowed(char *, int32);
bool csane_discovery_defaults(CsanePaths *, CsanePaths *);
bool csane_collect_root(CsanePaths *, char *, int32, int32, CsanePaths *,
                        CsanePaths *);
bool csane_collect_explicit(CsanePaths *, char *, int32);
bool csane_collect_file_list(CsanePaths *, char *, int32);
#if OS_UNIX
uint64 csane_cache_hash(uint64, char *, int64);
uint64 csane_cache_fingerprint(int32, char **, CsanePolicy *);
bool csane_cache_signature(char *, CsaneCacheRecord *);
bool csane_cache_read(char *, CsaneCacheRecord *, CsaneStats *, int32 *);
void csane_cache_write(char *, CsaneCacheRecord *, FILE *);
#endif
int32 csane_run(int32, char **);

#endif /* CSANECHECK_H */
