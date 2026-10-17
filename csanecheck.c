// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_C)
#define CSANECHECK_C

#include "cbase.h"
#include "csanecheck.h"

static void
csane_usage(FILE *output) {
    fprintf(output,
            "usage: csanecheck [--root DIR] [--file PATH] [--file-list PATH]\n"
            "                  [--max-depth N] [--report-parse-issues]\n"
            "                  [--strict-parse] [--stats] [--help]\n"
            "       csanecheck PATH [PATH ...]\n"
            "\n"
            "Phase 4.2: guideline checks and optional parser reports.\n"
            "Without paths, search the current project directory.\n"
            "Exit: 0 clean, 1 rule/parse issues, 2 usage/I/O error.\n");
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
    if (len < 0) {
        return 2;
    }
    Tokenization tokens = tokenize(source, len);
    CPreprocessorAnalysis pp = {0};
    CAst ast = {0};
    bool pp_ok = c_preprocessor_analysis_init(&pp, &tokens);
    bool ast_ok = c_ast_init(&ast, &tokens);
    if (!ast_ok) {
        fprintf(stderr, "%s: unable to initialize AST\n", path);
        c_preprocessor_analysis_free(&pp);
        free_tokenization(&tokens);
        free2(source, (int64)len + 1);
        return 2;
    }
    if (pp_ok) {
        c_ast_parse_translation_unit(&ast,
                                     (TokenRange){0, tokens.token_count}, &pp);
    }
    CsaneFile file = {
        .path = path,
        .tokens = &tokens,
        .preprocessor = &pp,
        .ast = &ast,
    };
    int32 violations = csane_rules_check(&file);
    int32 issues = ast.issue_count + pp.issue_count;
    if (!pp_ok && pp.issue_count == 0) {
        issues += 1;
    }
    stats->violations += violations;
    stats->files += 1;
    stats->tokens += tokens.token_count;
    stats->externals += ast.external_count;
    stats->parse_issues += ast.issue_count;
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
        for (int32 i = 0; i < ast.issue_count; i += 1) {
            CParseIssue *issue = &ast.issues[i];
            char *code = "PARSE-UNSUPPORTED";
            char *message = "unsupported C construct";
            if (issue->kind == C_PARSE_ISSUE_MALFORMED) {
                code = "PARSE-MALFORMED";
                message = "malformed C construct";
            } else if (issue->kind == C_PARSE_ISSUE_PREPROCESSOR) {
                code = "PARSE-PREPROCESSOR";
                message = "preprocessor boundary prevents parsing";
            }
            csane_issue(path, &tokens, issue->token, code, message);
        }
    }
    c_ast_free(&ast);
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)len + 1);
    if (violations > 0 || (options->strict_parse && issues > 0)) {
        return 1;
    }
    return 0;
}

int32
csane_run(int32 argc, char **argv) {
    CsaneOptions options = {.max_depth = -1};
    CsaneStats stats = {0};
    bool had_inputs = false;
    bool good = true;
    int32 exit_status = 0;

    /* Read all options before discovering sources: --max-depth must apply
     * regardless of whether it precedes or follows --root.
     */
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
            csane_usage(stdout);
            return 0;
        }
        if (!strcmp(arg, "--version")) {
            puts("csanecheck phase 4.2");
            return 0;
        }
        if (!strcmp(arg, "--stats")) {
            options.statistics = true;
            continue;
        }
        if (!strcmp(arg, "--report-parse-issues")) {
            options.report_parse = true;
            continue;
        }
        if (!strcmp(arg, "--strict-parse")) {
            options.strict_parse = true;
            continue;
        }
        if (!strcmp(arg, "--max-depth")) {
            if ((i + 1 >= argc)
                || !csane_depth(argv[i + 1], &options.max_depth)) {
                fprintf(stderr, "--max-depth requires a nonnegative integer\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (!strcmp(arg, "--root") || !strcmp(arg, "--file")
            || !strcmp(arg, "--file-list")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a path\n", arg);
                return 2;
            }
            had_inputs = true;
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg);
            return 2;
        }
        had_inputs = true;
    }
    for (int32 i = 1; i < argc; i += 1) {
        char *arg = argv[i];
        if (!strcmp(arg, "--max-depth")) {
            i += 1;
            continue;
        }
        if (!strcmp(arg, "--root") || !strcmp(arg, "--file")
            || !strcmp(arg, "--file-list")) {
            char *path = argv[i + 1];
            if (!strcmp(arg, "--root")) {
                good = csane_collect_root(&options.files, path,
                                          options.max_depth);
            } else if (!strcmp(arg, "--file")) {
                good = csane_collect_explicit(&options.files, path);
            } else {
                good = csane_collect_file_list(&options.files, path);
            }
            i += 1;
        } else if (arg[0] != '-') {
            struct stat st;
            if ((stat(arg, &st) == 0) && S_ISDIR(st.st_mode)) {
                good = csane_collect_root(&options.files, arg,
                                          options.max_depth);
            } else {
                good = csane_collect_explicit(&options.files, arg);
            }
        }
        if (!good) {
            break;
        }
    }
    if (good && !had_inputs) {
        good = csane_collect_root(&options.files, ".", options.max_depth);
    }
    if (!good) {
        csane_paths_free(&options.files);
        return 2;
    }
    csane_paths_sort_unique(&options.files);
    for (int32 i = 0; i < options.files.count; i += 1) {
        int32 status = csane_analyze(options.files.items[i], &options, &stats);
        if (status > exit_status) {
            exit_status = status;
        }
    }
    if (options.statistics) {
        printf("files=%d tokens=%lld externals=%lld parse_issues=%lld "
               "preprocessor_issues=%lld violations=%lld\n",
               stats.files, (long long)stats.tokens,
               (long long)stats.externals, (long long)stats.parse_issues,
               (long long)stats.preprocessor_issues,
               (long long)stats.violations);
    }
    csane_paths_free(&options.files);
    return exit_status;
}

#endif /* CSANECHECK_C */
