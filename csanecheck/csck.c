// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_C)
#define CSANECHECK_C

#include "cbase.h"
#include "csck.h"

static void
csane_usage(FILE *output) {
    fprintf(output,
            "usage: csanecheck [--root DIR] [--file PATH] [--file-list PATH]\n"
            "                  [--max-depth N] [--report-parse-issues]\n"
            "                  [--strict-parse] [--require-coverage]\n"
            "                  [--stats] [--coverage]\n"
            "                  [--rule-coverage]\n"
            "                  [--config FILE]\n"
            "                  [--source-root DIR] [--skip-dir NAME]\n"
            "                  [--clear-source-roots]\n"
            "                  [--exclude REGEX] [--filter MODULE]\n"
            "                  [--cbase-root DIR] [--no-column-limit REGEX]\n"
            "                  [--help]\n"
            "       csanecheck PATH [PATH ...]\n"
            "\n"
            "Token-local source, structural, and contextual checks.\n"
            "Without paths, search the current project directory.\n"
            "Exit: 0 clean, 1 violations/required-coverage failure,\n"
            "      2 usage/I/O error.\n");
    return;
}

static bool
csane_depth(char *text, int32 *value) {
    char *end;
    long result;
    if ((text == NULL) || (text[0] == '\0')) {
        return false;
    }
    errno = 0;
    result = strtol(text, &end, 10);
    if ((errno != 0) || (*end != '\0') || (result < 0)
        || (result >= INT32_MAX)) {
        return false;
    }
    *value = (int32)result;
    return true;
}

static void
csane_issue(char *path, Tokenization *tokens, int32 index,
            char *code, char *message) {
    SourceLocation loc = tokenization_token_location(tokens, index);
    if (index < 0 || index >= tokens->token_count) {
        loc = tokenization_source_location(tokens, tokens->text_len);
    }
    fprintf(stdout, "%s:%d:%d: note[%s]: %s\n",
                    path, loc.line, loc.column + 1, code, message);
    return;
}

static int32
csane_analyze(char *path, CsaneOptions *options, CsaneStats *stats) {
    char *source = NULL;
    int32 len = read_entire_file(path, &source);
    Tokenization tokens;
    CPreprocessorAnalysis pp = {0};
    CsaneRegions regions = {0};
    CsaneTopLevels toplevel = {0};
    CsaneStatements statements = {0};
    CsaneTypeHints type_hints = {0};
    CsaneFile file;
    bool pp_ok;
    CsaneCoverage coverage;
    int32 violations;
    int32 structural_issues;
    int32 issues;
    CsaneRuleCounts totals = {0};
    int32 incomplete;

    if (len < 0) {
        return 2;
    }
    tokens = tokenize(source, len);
    file = (CsaneFile){
        .path = path,
        .tokens = &tokens,
        .preprocessor = &pp,
        .policy = &options->policy,
    };
    csane_rules_prepare(&file);
    /* Source checks are independent of preprocessing and syntax parsing. */
    csane_rules_run(&file, CSANE_PASS_SOURCE);
    pp_ok = c_preprocessor_analysis_init(&pp, &tokens);
    if (!csane_regions_init(&regions, &tokens, &pp)) {
        fprintf(stderr, "%s: unable to initialize source regions\n", path);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    if (!csane_toplevel_init(&toplevel, &regions)) {
        fprintf(stderr, "%s: unable to initialize top-level regions\n", path);
        csane_regions_free(&regions);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    if (!csane_statements_init(&statements, &toplevel)) {
        fprintf(stderr, "%s: unable to initialize function statements\n", path);
        csane_toplevel_free(&toplevel);
        csane_regions_free(&regions);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    csane_type_hints_init(&type_hints, &regions);
    file.statements = &statements;
    file.type_hints = &type_hints;
    file.preprocessor_ok = pp_ok;
    file.regions = &regions;
    file.toplevel = &toplevel;
    csane_rules_run(&file, CSANE_PASS_TOKEN);
    coverage = csane_coverage_collect(&toplevel, &statements);
    violations = file.violations;
    structural_issues = coverage.opaque_externals + coverage.opaque_statements;
    issues = structural_issues + pp.issue_count;
    if (!pp_ok && pp.issue_count == 0) {
        issues += 1;
    }
    stats->violations += violations;
    stats->declarations += coverage.declarations;
    stats->function_declarations += coverage.function_declarations;
    stats->function_definitions += coverage.function_definitions;
    stats->statements += coverage.statements;
    stats->opaque_statements += coverage.opaque_statements;
    stats->opaque_externals += coverage.opaque_externals;
    if (options->coverage) {
        printf("coverage file=%s declarations=%d function_declarations=%d "
               "function_definitions=%d statements=%d "
               "opaque_statements=%d opaque_externals=%d "
               "structural_issues=%d preprocessor_issues=%d\n",
               path, coverage.declarations, coverage.function_declarations,
               coverage.function_definitions, coverage.statements,
               coverage.opaque_statements, coverage.opaque_externals,
               structural_issues, pp.issue_count);
    }
    if ((options->coverage || options->require_coverage)
        && structural_issues > 0) {
        csane_coverage_report_structural(path, &tokens, &toplevel, &statements);
    }
    incomplete = csane_rules_incomplete(&file, options->rule_coverage,
                                        options->rule_coverage
                                        || options->require_coverage, &totals);
    if (options->coverage) {
        printf("coverage_layers file=%s eligible=%d checked=%d "
               "excluded=%d unresolved=%d\n",
               path, totals.eligible, totals.checked, totals.excluded,
               totals.unresolved);
    }
    if (options->require_coverage && incomplete > 0) {
        printf("%s:1:1: error[COVERAGE-INCOMPLETE]: "
               "%d required rule groups have unresolved targets\n",
               path, incomplete);
    }
    stats->files += 1;
    stats->tokens += tokens.token_count;
    stats->externals += toplevel.count;
    stats->structural_issues += structural_issues;
    stats->preprocessor_issues += pp.issue_count;
    if (!pp_ok && pp.issue_count == 0) {
        stats->preprocessor_issues += 1;
    }
    if (options->report_parse || options->strict_parse) {
        for (int32 i = 0; i < pp.issue_count; i += 1) {
            int32 directive = pp.issues[i].directive_index;
            int32 token = -1;
            if ((directive >= 0) && (directive < pp.directive_count)) {
                token = pp.directives[directive].hash_token;
            }
            csane_issue(path, &tokens, token, "PP-STRUCTURE",
                        "invalid preprocessor structure");
        }
        if (!pp_ok && pp.issue_count == 0) {
            csane_issue(path, &tokens, -1, "PP-STRUCTURE",
                        "preprocessor analysis failed");
        }
        for (int32 i = 0; i < toplevel.count; i += 1) {
            CsaneTopItem *item = &toplevel.items[i];
            if (item->kind == CSANE_TOP_OPAQUE) {
                csane_issue(path, &tokens, item->tokens.first,
                            "STRUCTURE-OPAQUE",
                            "unrecognized top-level construct");
            }
        }
        for (int32 i = 0; i < statements.count; i += 1) {
            CsaneStatement *stmt = &statements.items[i];
            if (stmt->kind == CSANE_STMT_OPAQUE) {
                csane_issue(path, &tokens, stmt->tokens.first,
                            "STRUCTURE-OPAQUE",
                            "unrecognized statement");
            }
        }
    }
    csane_type_hints_free(&type_hints);
    csane_statements_free(&statements);
    csane_toplevel_free(&toplevel);
    csane_regions_free(&regions);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)len + 1);
    if (violations > 0 || (options->strict_parse && issues > 0)
        || (options->require_coverage && incomplete > 0)) {
        return 1;
    }
    return 0;
}

/* Policy discovery only needs the filename eligibility check. A parent
 * directory named stc outside the project must not hide its policy lists.
 */
static bool
csane_policy_candidate(char *path) {
    char *name = path;
    for (char *p = path; *p; p += 1) {
        if (*p == '/') {
            name = p + 1;
        }
    }
    return csane_source_allowed(name);
}

static bool
csane_value_option(char *arg) {
    return !strcmp(arg, "--root") || !strcmp(arg, "--file")
           || !strcmp(arg, "--file-list") || !strcmp(arg, "--max-depth")
           || !strcmp(arg, "--config") || !strcmp(arg, "--exclude")
           || !strcmp(arg, "--source-root") || !strcmp(arg, "--skip-dir")
           || !strcmp(arg, "--filter") || !strcmp(arg, "--cbase-root")
           || !strcmp(arg, "--no-column-limit");
}

int32
csane_run(int32 argc, char **argv) {
    CsaneOptions options = {.max_depth = -1};
    CsaneStats stats = {0};
    bool had_inputs = false;
    bool good = true;
    int32 exit_status = 0;
    char *value;

    if (!csane_policy_init(&options.policy)
        || !csane_discovery_defaults(&options.source_roots,
                                     &options.skip_dirs)) {
        csane_paths_free(&options.source_roots);
        csane_paths_free(&options.skip_dirs);
        csane_policy_free(&options.policy);
        return 2;
    }
    options.filter = getenv("TEST_FILTER");
    value = getenv("TEST_MAXDEPTH");
    if (value != NULL && value[0] != '\0'
        && !csane_depth(value, &options.max_depth)) {
        fprintf(stderr, "invalid TEST_MAXDEPTH: %s\n", value);
        exit_status = 2;
        goto cleanup;
    }
    value = getenv("TEST_EXCLUDE_PATTERN");
    if (value != NULL && value[0] != '\0'
        && !csane_policy_add_regex(&options.policy.excludes, value,
                                   NULL, NULL, 0)) {
        exit_status = 2;
        goto cleanup;
    }

    /* Discover repository policy roots first. Explicit configuration can
     * then extend or override the imported project's default name lists.
     */
    if (!csane_policy_project(&options.policy, ".")) {
        exit_status = 2;
        goto cleanup;
    }
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (csane_value_option(arg)) {
            if (i + 1 < argc) {
                if (!strcmp(arg, "--root")) {
                    good = csane_policy_project(&options.policy, argv[i + 1]);
                } else if (!strcmp(arg, "--file")) {
                    if (csane_policy_candidate(argv[i + 1])) {
                        good = csane_policy_source(&options.policy,
                                                   argv[i + 1]);
                    }
                } else if (!strcmp(arg, "--file-list")) {
                    CsanePaths sources = {0};
                    good = csane_collect_file_list(&sources, argv[i + 1]);
                    for (int32 j = 0; good && j < strflex_list_len(&sources.strings); j += 1) {
                        if (csane_policy_candidate(sources.strings.items[j]->data)) {
                            good = csane_policy_source(&options.policy,
                                                       sources.strings.items[j]->data);
                        }
                    }
                    csane_paths_free(&sources);
                }
                if (!good) {
                    exit_status = 2;
                    goto cleanup;
                }
                i += 1;
            }
            continue;
        }
        if (arg[0] != '-') {
            struct stat st;
            if (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
                if (!csane_policy_project(&options.policy, arg)) {
                    exit_status = 2;
                    goto cleanup;
                }
            }
        }
    }

    /* All options are resolved before walking roots, independent of order. */
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            csane_usage(stdout);
            goto cleanup;
        }
        if (!strcmp(arg, "--version")) {
            puts("csanecheck token-local");
            goto cleanup;
        }
        if (!strcmp(arg, "--stats")) {
            options.statistics = true;
            continue;
        }
        if (!strcmp(arg, "--coverage")) {
            options.coverage = true;
            continue;
        }
        if (!strcmp(arg, "--rule-coverage")) {
            options.rule_coverage = true;
            continue;
        }
        if (!strcmp(arg, "--report-parse-issues")) {
            options.report_parse = true;
            continue;
        }
        if (!strcmp(arg, "--require-coverage")) {
            options.require_coverage = true;
            continue;
        }
        if (!strcmp(arg, "--strict-parse")) {
            options.strict_parse = true;
            continue;
        }
        if (!strcmp(arg, "--clear-source-roots")) {
            csane_paths_free(&options.source_roots);
            continue;
        }
        if (csane_value_option(arg)) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a value\n", arg);
                exit_status = 2;
                goto cleanup;
            }
            value = argv[i + 1];
            if (!strcmp(arg, "--max-depth")) {
                good = csane_depth(value, &options.max_depth);
            } else if (!strcmp(arg, "--config")) {
                good = csane_policy_config(&options.policy, value);
            } else if (!strcmp(arg, "--exclude")) {
                good = csane_policy_add_regex(&options.policy.excludes,
                                              value, NULL, NULL, 0);
            } else if (!strcmp(arg, "--source-root")) {
                good = csane_paths_add(&options.source_roots, value, strlen32(value));
            } else if (!strcmp(arg, "--skip-dir")) {
                good = csane_paths_add(&options.skip_dirs, value, strlen32(value));
            } else if (!strcmp(arg, "--filter")) {
                options.filter = value;
            } else if (!strcmp(arg, "--cbase-root")) {
                good = csane_policy_add_root(&options.policy, value);
            } else if (!strcmp(arg, "--no-column-limit")) {
                good = csane_policy_add_regex(&options.policy.no_columns,
                                              value, NULL, NULL, 0);
            } else if (!strcmp(arg, "--root") || !strcmp(arg, "--file")
                       || !strcmp(arg, "--file-list")) {
                had_inputs = true;
            }
            if (!good) {
                fprintf(stderr, "invalid value for %s: %s\n", arg, value);
                exit_status = 2;
                goto cleanup;
            }
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg);
            exit_status = 2;
            goto cleanup;
        }
        had_inputs = true;
    }
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (csane_value_option(arg)) {
            char *path = argv[i + 1];
            if (!strcmp(arg, "--root")) {
                good = csane_collect_root(&options.files, path,
                                          options.max_depth,
                                          &options.source_roots,
                                          &options.skip_dirs);
            } else if (!strcmp(arg, "--file")) {
                good = csane_collect_explicit(&options.files, path);
            } else if (!strcmp(arg, "--file-list")) {
                good = csane_collect_file_list(&options.files, path);
            }
            i += 1;
        } else if (arg[0] != '-') {
            struct stat st;
            if ((stat(arg, &st) == 0) && S_ISDIR(st.st_mode)) {
                good = csane_collect_root(&options.files, arg,
                                          options.max_depth,
                                          &options.source_roots,
                                          &options.skip_dirs);
            } else {
                good = csane_collect_explicit(&options.files, arg);
            }
        }
        if (!good) {
            exit_status = 2;
            goto cleanup;
        }
    }
    if (!had_inputs
        && !csane_collect_root(&options.files, ".", options.max_depth,
                               &options.source_roots,
                               &options.skip_dirs)) {
        exit_status = 2;
        goto cleanup;
    }
    csane_paths_sort_unique(&options.files);
    csane_paths_sort_unique(&options.policy.cbase_roots);
    csane_paths_sort_unique(&options.policy.project_roots);
    for (int32 i = 0; i < strflex_list_len(&options.files.strings); i += 1) {
        char *path = options.files.strings.items[i]->data;
        int32 status;
        if (!csane_policy_selected(&options.policy, path, options.filter)) {
            continue;
        }
        status = csane_analyze(path, &options, &stats);
        if (status > exit_status) {
            exit_status = status;
        }
    }
    if (options.statistics) {
        printf("files=%d tokens=%lld externals=%lld "
               "structural_issues=%lld preprocessor_issues=%lld "
               "violations=%lld declarations=%lld "
               "function_declarations=%lld function_definitions=%lld "
               "statements=%lld opaque_statements=%lld "
               "opaque_externals=%lld\n",
               stats.files, (long long)stats.tokens,
               (long long)stats.externals,
               (long long)stats.structural_issues,
               (long long)stats.preprocessor_issues,
               (long long)stats.violations,
               (long long)stats.declarations,
               (long long)stats.function_declarations,
               (long long)stats.function_definitions,
               (long long)stats.statements,
               (long long)stats.opaque_statements,
               (long long)stats.opaque_externals);
    }
cleanup:
    if (stats.violations > 0) {
        printf("In order to understand and learn how to fix the problems "
               "above, read cbase/c-guidelines.md and cbase/c-format.md\n"
               "if you think any of them are a false positive, stop "
               "what you are doing and report.\n");
    }
    csane_paths_free(&options.files);
    csane_paths_free(&options.source_roots);
    csane_paths_free(&options.skip_dirs);
    csane_policy_free(&options.policy);
    return exit_status;
}

#endif /* CSANECHECK_C */
