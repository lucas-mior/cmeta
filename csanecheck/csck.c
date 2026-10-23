// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_C)
#define CSANECHECK_C

#include "cbase.h"
#include "csck.h"

static void
csane_usage(void) {
    fmt_printf("usage: csanecheck [--root DIR] [--file PATH] "
               "[--file-list PATH]\n"
               "                  [--max-depth N] [--report-parse-issues]\n"
               "                  [--strict-parse] [--require-coverage]\n"
               "                  [--stats] [--coverage]\n"
               "                  [--rule-coverage]\n"
               "                  [--config FILE]\n"
               "                  [--source-root DIR] [--skip-dir NAME]\n"
               "                  [--clear-source-roots]\n"
               "                  [--exclude REGEX] [--filter MODULE]\n"
               "                  [--cbase-root DIR] "
               "[--no-column-limit REGEX]\n"
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
csane_depth(char *text, int32 text_len, int32 *value) {
    char *end;
    long result;
    if ((text == NULL) || (text_len == 0)) {
        return false;
    }
    errno = 0;
    result = strtol(text, &end, 10);
    if ((errno != 0) || (end != text + text_len) || (result < 0)
        || (result >= INT32_MAX)) {
        return false;
    }
    *value = (int32)result;
    return true;
}

static void
csane_issue(char *path, int32 path_len, Tokenization *tokens, int32 index,
            char *code, char *message) {
    SourceLocation loc = tokenization_token_location(tokens, index);
    if (index < 0 || index >= tokens->token_count) {
        loc = tokenization_source_location(tokens, tokens->text_len);
    }
    fmt_printf("%.*s:%d:%d: note[%s]: %s\n",
               path_len, path, loc.line, loc.column + 1, code, message);
    return;
}

static int32
csane_analyze(char *path, int32 path_len, CsaneOptions *options,
              CsaneStats *stats) {
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
    int32 incomplete = 0;

    if (len < 0) {
        return 2;
    }
    tokens = tokenize(source, len);
    file = (CsaneFile){
        .path = path,
        .path_len = path_len,
        .tokens = &tokens,
        .preprocessor = &pp,
        .policy = &options->policy,
    };
    csane_rules_prepare(&file);
    /* Source checks are independent of preprocessing and syntax parsing. */
    csane_rules_run(&file, CSANE_PASS_SOURCE);
    pp_ok = c_preprocessor_analysis_init(&pp, &tokens);
    if (!csane_regions_init(&regions, &tokens, &pp)) {
        error2("%.*s: unable to initialize source regions\n", path_len, path);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    if (!csane_toplevel_init(&toplevel, &regions)) {
        error2("%.*s: unable to initialize top-level regions\n",
               path_len, path);
        csane_regions_free(&regions);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    if (!csane_statements_init(&statements, &toplevel)) {
        error2("%.*s: unable to initialize function statements\n",
               path_len, path);
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
    stats->function_decls += coverage.function_decls;
    stats->function_defs += coverage.function_defs;
    stats->statements += coverage.statements;
    stats->opaque_statements += coverage.opaque_statements;
    stats->opaque_externals += coverage.opaque_externals;

    if (options->coverage) {
        fmt_printf("coverage file=%.*s declarations=%d "
                   "function_decls=%d "
                   "function_defs=%d statements=%d "
                   "opaque_statements=%d opaque_externals=%d "
                   "structural_issues=%d preprocessor_issues=%d\n",
                   path_len, path, coverage.declarations,
                   coverage.function_decls,
                   coverage.function_defs, coverage.statements,
                   coverage.opaque_statements, coverage.opaque_externals,
                   structural_issues, pp.issue_count);
    }
    if ((options->coverage || options->require_coverage)
        && structural_issues > 0) {
        csane_coverage_report_structural(path, path_len, &tokens,
                                         &toplevel, &statements);
    }
    if (options->coverage || options->rule_coverage
        || options->require_coverage) {
        incomplete = csane_rules_incomplete(&file, options->rule_coverage,
                                            options->rule_coverage
                                            || options->require_coverage,
                                            &totals);
    }
    if (options->coverage) {
        fmt_printf("coverage_layers file=%.*s eligible=%d checked=%d "
                   "excluded=%d unresolved=%d\n",
                   path_len, path, totals.eligible, totals.checked,
                   totals.excluded,
                   totals.unresolved);
    }
    if (options->require_coverage && incomplete > 0) {
        fmt_printf("%.*s:1:1: error[COVERAGE-INCOMPLETE]: "
                   "%d required rule groups have unresolved targets\n",
                   path_len, path, incomplete);
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
            csane_issue(path, path_len, &tokens, token, "PP-STRUCTURE",
                        "invalid preprocessor structure");
        }
        if (!pp_ok && pp.issue_count == 0) {
            csane_issue(path, path_len, &tokens, -1, "PP-STRUCTURE",
                        "preprocessor analysis failed");
        }
        for (int32 i = 0; i < toplevel.count; i += 1) {
            CsaneTopItem *item = &toplevel.items[i];
            if (item->kind == CSANE_TOP_OPAQUE) {
                csane_issue(path, path_len, &tokens, item->tokens.first,
                            "STRUCTURE-OPAQUE",
                            "unrecognized top-level construct");
            }
        }
        for (int32 i = 0; i < statements.count; i += 1) {
            CsaneStatement *stmt = &statements.items[i];
            if (stmt->kind == CSANE_STMT_OPAQUE) {
                csane_issue(path, path_len, &tokens, stmt->tokens.first,
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
csane_policy_candidate(char *path, int32 path_len) {
    char *name = path;
    for (int32 i = 0; i < path_len; i += 1) {
        if (path[i] == '/') {
            name = path + i + 1;
        }
    }
    return csane_source_allowed(name, path_len - (int32)(name - path));
}

static bool
csane_value_option(char *arg, int32 arg_len) {
    return STREQUAL(arg, arg_len, "--root")
           || STREQUAL(arg, arg_len, "--file")
           || STREQUAL(arg, arg_len, "--file-list")
           || STREQUAL(arg, arg_len, "--max-depth")
           || STREQUAL(arg, arg_len, "--config")
           || STREQUAL(arg, arg_len, "--exclude")
           || STREQUAL(arg, arg_len, "--source-root")
           || STREQUAL(arg, arg_len, "--skip-dir")
           || STREQUAL(arg, arg_len, "--filter")
           || STREQUAL(arg, arg_len, "--cbase-root")
           || STREQUAL(arg, arg_len, "--no-column-limit");
}

int32
csane_run(int32 argc, char **argv) {
    CsaneOptions options = {.max_depth = -1};
    CsaneStats stats = {0};
    bool had_inputs = false;
    bool good = true;
    int32 exit_status = 0;
    char *value;
    int32 value_len = 0;
    int32 *arg_lens = NULL;
#if OS_UNIX
    uint64 cache_fingerprint;
#endif

    if (!csane_policy_init(&options.policy)
        || !csane_discovery_defaults(&options.source_roots,
                                     &options.skip_dirs)) {
        csane_paths_free(&options.source_roots);
        csane_paths_free(&options.skip_dirs);
        csane_policy_free(&options.policy);
        return 2;
    }
    arg_lens = malloc2(argc*SIZEOF(*arg_lens));
    if (arg_lens == NULL) {
        exit_status = 2;
        goto cleanup;
    }
    for (int32 i = 0; i < argc; i += 1) {
        arg_lens[i] = strlen32(argv[i]);
    }
    options.filter = getenv("TEST_FILTER");
    if (options.filter != NULL) {
        options.filter_len = strlen32(options.filter);
    }
    value = getenv("TEST_MAXDEPTH");
    if (value != NULL) {
        value_len = strlen32(value);
    }
    if (value != NULL && value_len > 0
        && !csane_depth(value, value_len, &options.max_depth)) {
        error2("invalid TEST_MAXDEPTH: %.*s\n", value_len, value);
        exit_status = 2;
        goto cleanup;
    }
    value = getenv("TEST_EXCLUDE_PATTERN");
    if (value != NULL) {
        value_len = strlen32(value);
    }
    if (value != NULL && value[0] != '\0'
        && !csane_policy_add_regex(&options.policy.excludes, value, value_len,
                                   NULL, 0, NULL, 0, 0)) {
        exit_status = 2;
        goto cleanup;
    }

    /* Discover repository policy roots first. Explicit configuration can
     * then extend or override the imported project's default name lists.
     */
    if (!csane_policy_project(&options.policy, STRLIT("."))) {
        exit_status = 2;
        goto cleanup;
    }
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        int32 arg_len = arg_lens[i];
        if (csane_value_option(arg, arg_len)) {
            if (i + 1 < argc) {
                if (STREQUAL(arg, arg_len, "--root")) {
                    good = csane_policy_project(&options.policy, argv[i + 1],
                                                arg_lens[i + 1]);
                } else if (STREQUAL(arg, arg_len, "--file")) {
                    if (csane_policy_candidate(argv[i + 1], arg_lens[i + 1])) {
                        good = csane_policy_source(&options.policy,
                                                   argv[i + 1],
                                                   arg_lens[i + 1]);
                    }
                } else if (STREQUAL(arg, arg_len, "--file-list")) {
                    CsanePaths sources = {0};
                    good = csane_collect_file_list(&sources, argv[i + 1],
                                                   arg_lens[i + 1]);
                    for (int32 j = 0; good && j <
                         strflex_list_len(&sources.strings); j += 1) {
                        StrFlex *source = sources.strings.items[j];
                        if (csane_policy_candidate(source->data, source->len)) {
                            good = csane_policy_source(&options.policy,
                                                       source->data,
                                                       source->len);
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
                if (!csane_policy_project(&options.policy, arg, arg_lens[i])) {
                    exit_status = 2;
                    goto cleanup;
                }
            }
        }
    }

    /* All options are resolved before walking roots, independent of order. */
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        int32 arg_len = arg_lens[i];
        if (STREQUAL(arg, arg_len, "--help") || STREQUAL(arg, arg_len, "-h")) {
            csane_usage();
            goto cleanup;
        }
        if (STREQUAL(arg, arg_len, "--version")) {
            fmt_printf("csanecheck token-local\n");
            goto cleanup;
        }
        if (STREQUAL(arg, arg_len, "--stats")) {
            options.statistics = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--coverage")) {
            options.coverage = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--rule-coverage")) {
            options.rule_coverage = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--report-parse-issues")) {
            options.report_parse = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--require-coverage")) {
            options.require_coverage = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--strict-parse")) {
            options.strict_parse = true;
            continue;
        }
        if (STREQUAL(arg, arg_len, "--clear-source-roots")) {
            csane_paths_free(&options.source_roots);
            continue;
        }
        if (csane_value_option(arg, arg_len)) {
            if (i + 1 >= argc) {
                error2("%.*s requires a value\n", arg_len, arg);
                exit_status = 2;
                goto cleanup;
            }
            value = argv[i + 1];
            value_len = arg_lens[i + 1];
            if (STREQUAL(arg, arg_len, "--max-depth")) {
                good = csane_depth(value, value_len, &options.max_depth);
            } else if (STREQUAL(arg, arg_len, "--config")) {
                good = csane_policy_config(&options.policy, value, value_len);
            } else if (STREQUAL(arg, arg_len, "--exclude")) {
                good = csane_policy_add_regex(&options.policy.excludes,
                                              value, value_len, NULL, 0,
                                              NULL, 0, 0);
            } else if (STREQUAL(arg, arg_len, "--source-root")) {
                good = csane_paths_add(&options.source_roots, value, value_len);
            } else if (STREQUAL(arg, arg_len, "--skip-dir")) {
                good = csane_paths_add(&options.skip_dirs, value, value_len);
            } else if (STREQUAL(arg, arg_len, "--filter")) {
                options.filter = value;
                options.filter_len = value_len;
            } else if (STREQUAL(arg, arg_len, "--cbase-root")) {
                good = csane_policy_add_root(&options.policy, value, value_len);
            } else if (STREQUAL(arg, arg_len, "--no-column-limit")) {
                good = (value_len <= 0)
                       || csane_policy_add_regex(&options.policy.no_columns,
                                                 value, value_len, NULL, 0,
                                                 NULL, 0, 0);
            } else if (STREQUAL(arg, arg_len, "--root")
                       || STREQUAL(arg, arg_len, "--file")
                       || STREQUAL(arg, arg_len, "--file-list")) {
                had_inputs = true;
            }
            if (!good) {
                error2("invalid value for %.*s: %.*s\n",
                       arg_len, arg, value_len, value);
                exit_status = 2;
                goto cleanup;
            }
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            error2("unknown option: %.*s\n", arg_len, arg);
            exit_status = 2;
            goto cleanup;
        }
        had_inputs = true;
    }
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        int32 arg_len = arg_lens[i];
        if (csane_value_option(arg, arg_len)) {
            char *path = argv[i + 1];
            if (STREQUAL(arg, arg_len, "--root")) {
                good = csane_collect_root(&options.files, path, arg_lens[i + 1],
                                          options.max_depth,
                                          &options.source_roots,
                                          &options.skip_dirs);
            } else if (STREQUAL(arg, arg_len, "--file")) {
                good = csane_collect_explicit(&options.files, path,
                                              arg_lens[i + 1]);
            } else if (STREQUAL(arg, arg_len, "--file-list")) {
                good = csane_collect_file_list(&options.files, path,
                                               arg_lens[i + 1]);
            }
            i += 1;
        } else if (arg[0] != '-') {
            struct stat st;
            if ((stat(arg, &st) == 0) && S_ISDIR(st.st_mode)) {
                good = csane_collect_root(&options.files, arg, arg_lens[i],
                                          options.max_depth,
                                          &options.source_roots,
                                          &options.skip_dirs);
            } else {
                good = csane_collect_explicit(&options.files, arg, arg_lens[i]);
            }
        }
        if (!good) {
            exit_status = 2;
            goto cleanup;
        }
    }
    if (!had_inputs
        && !csane_collect_root(&options.files, STRLIT("."), options.max_depth,
                               &options.source_roots,
                               &options.skip_dirs)) {
        exit_status = 2;
        goto cleanup;
    }
    csane_paths_sort_unique(&options.files);
    csane_paths_sort_unique(&options.policy.cbase_roots);
    csane_paths_sort_unique(&options.policy.project_roots);
#if OS_UNIX
    cache_fingerprint = csane_cache_fingerprint(argc, argv, &options.policy);
    (void)mkdir(".cache", 0700);
    (void)mkdir(".cache/csanecheck", 0700);
#endif
    for (int32 i = 0; i < strflex_list_len(&options.files.strings); i += 1) {
        StrFlex *entry = options.files.strings.items[i];
        char *path = entry->data;
        int32 status;
#if OS_UNIX
        CsaneCacheRecord record = {
            .magic = UINT64_C(0x4353414e45434132),
            .fingerprint = cache_fingerprint,
        };
        uint64 path_hash;
        char cache_name[128];
        bool cacheable;
        FILE *capture = NULL;
        int saved_stdout = -1;
        CsaneStats before;
#endif

        if (!csane_policy_selected(&options.policy, path, entry->len,
                                   options.filter, options.filter_len)) {
            continue;
        }
#if OS_UNIX
        path_hash = csane_cache_hash(UINT64_C(14695981039346656037),
                                     path, entry->len);
        SNPRINTF(cache_name, ".cache/csanecheck/%016llx.bin", path_hash);
        cacheable = csane_cache_signature(path, &record);
        if (cacheable && csane_cache_read(cache_name, &record, &stats,
                                          &status)) {
            if (status > exit_status) {
                exit_status = status;
            }
            continue;
        }
        capture = cacheable ? tmpfile() : NULL;
        before = stats;
        if (capture != NULL) {
            fflush(stdout);
            saved_stdout = dup(STDOUT_FILENO);
            if (saved_stdout < 0
                || dup2(fileno(capture), STDOUT_FILENO) < 0) {
                if (saved_stdout >= 0) {
                    close(saved_stdout);
                }
                fclose(capture);
                capture = NULL;
            }
        }
#endif
        status = csane_analyze(path, entry->len, &options, &stats);
#if OS_UNIX
        if (capture != NULL) {
            long output_len;
            char buffer[4096];
            size_t count;
            CsaneCacheRecord after = {0};

            fflush(stdout);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
            output_len = ftell(capture);
            rewind(capture);
            while ((count = fread(buffer, 1, SIZEOF(buffer), capture)) > 0) {
                fwrite(buffer, 1, count, stdout);
            }
            if (status != 2 && output_len >= 0
                && output_len <= 16*1024*1024
                && csane_cache_signature(path, &after)
                && after.source_size == record.source_size
                && after.source_mtime == record.source_mtime
                && after.source_ctime == record.source_ctime) {
                record.status = status;
                record.output_len = (uint32)output_len;
                record.delta = (CsaneStats){
                    .files = stats.files - before.files,
                    .tokens = stats.tokens - before.tokens,
                    .externals = stats.externals - before.externals,
                    .structural_issues = stats.structural_issues
                                         - before.structural_issues,
                    .preprocessor_issues = stats.preprocessor_issues
                                           - before.preprocessor_issues,
                    .violations = stats.violations - before.violations,
                    .declarations = stats.declarations - before.declarations,
                    .function_decls = stats.function_decls
                                             - before.function_decls,
                    .function_defs = stats.function_defs
                                            - before.function_defs,
                    .statements = stats.statements - before.statements,
                    .opaque_statements = stats.opaque_statements
                                         - before.opaque_statements,
                    .opaque_externals = stats.opaque_externals
                                        - before.opaque_externals,
                };
                csane_cache_write(cache_name, &record, capture);
            }
            fclose(capture);
        }
#endif
        if (status > exit_status) {
            exit_status = status;
        }
    }
    if (options.statistics) {
        fmt_printf("statistics={\n");
        fmt_printf("    files=%d\n", stats.files);
        fmt_printf("    tokens=%lld\n", stats.tokens);
        fmt_printf("    externals=%lld\n", stats.externals);
        fmt_printf("    structural_issues=%lld\n", stats.structural_issues);
        fmt_printf("    preprocessor_issues=%lld\n", stats.preprocessor_issues);
        fmt_printf("    violations=%lld\n", stats.violations);
        fmt_printf("    declarations=%lld\n", stats.declarations);
        fmt_printf("    function_decls=%lld\n",
                   stats.function_decls);
        fmt_printf("    function_defs=%lld\n",
                   stats.function_defs);
        fmt_printf("    statements=%lld\n", stats.statements);
        fmt_printf("    opaque_statements=%lld\n", stats.opaque_statements);
        fmt_printf("    opaque_externals=%lld\n", stats.opaque_externals);
        fmt_printf("}\n");
    }
cleanup:
    free2(arg_lens, argc*SIZEOF(*arg_lens));

    if (stats.violations > 0) {
        fmt_printf("In order to understand and learn how to fix the problems "
                   "above, read cbase/c-guidelines.md and cbase/c-format.md\n");
        fmt_printf("If you think any of them are a false positive"
                   ", put them in a file called false-positives.txt"
                   ", and add an exception for it in cbase/csanecheck.conf.\n");
    }

    csane_paths_free(&options.files);
    csane_paths_free(&options.source_roots);
    csane_paths_free(&options.skip_dirs);
    csane_policy_free(&options.policy);

    return exit_status;
}

#endif /* CSANECHECK_C */
