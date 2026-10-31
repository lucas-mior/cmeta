// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONFIG_C)
#define CSANECHECK_CONFIG_C

#include "cbase.h"
#include "csck.h"

static StrFlex *csane_default_never[] = {
    SFLIT("stpcpy"), SFLIT("stpncpy"), SFLIT("strcat"), SFLIT("strchr"),
    SFLIT("strcpy"), SFLIT("strcspn"), SFLIT("strdup"), SFLIT("strncat"),
    SFLIT("strncpy"), SFLIT("strndup"), SFLIT("strpbrk"), SFLIT("strrchr"),
    SFLIT("strspn"), SFLIT("strstr"), SFLIT("strtok"), SFLIT("strtok_r"),
    SFLIT("strxfrm"), SFLIT("strxfrm_l"), SFLIT("strsep"), SFLIT("isalnum"),
    SFLIT("isalpha"), SFLIT("isascii"), SFLIT("isblank"), SFLIT("iscntrl"),
    SFLIT("isdigit"), SFLIT("isgraph"), SFLIT("islower"), SFLIT("isprint"),
    SFLIT("ispunct"), SFLIT("isspace"), SFLIT("isupper"), SFLIT("isxdigit"),
};

static StrFlex *csane_default_cbase[] = {
    SFLIT("memccpy"), SFLIT("memchr"), SFLIT("memcmp"), SFLIT("memcpy"),
    SFLIT("memmove"), SFLIT("memset"), SFLIT("strlen"), SFLIT("strncmp"),
    SFLIT("strnlen"),
};

static bool
csane_number(char *text, int32 text_len, int32 *out) {
    char *end;
    long value;
    if (text == NULL || text_len == 0) {
        return false;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end != text + text_len || value < 1
        || value >= INT32_MAX) {
        return false;
    }
    *out = (int32)value;
    return true;
}

bool
csane_policy_add_regex(CsanePatterns *patterns, char *text, int32 text_len,
                       char *id, int32 id_len, char *function,
                       int32 function_len, int32 columns) {
    CsanePattern pattern = {0};
#if OS_UNIX
    int result;
#endif

    if (patterns->count == INT32_MAX
        || (patterns->count == patterns->capacity
            && patterns->capacity > INT32_MAX/2)) {
        return false;
    }
#if OS_UNIX
    result = regcomp(&pattern.compiled, text, REG_EXTENDED | REG_NOSUB);
    if (result != 0) {
        char description[256];
        regerror(result, &pattern.compiled, description, SIZEOF(description));
        error2("invalid regex '%.*s': %s\n", text_len, text, description);
        return false;
    }
#else
    error2("POSIX regular expressions are unavailable: %.*s\n", text_len, text);
    return false;
#endif
    if (function != NULL) {
#if OS_UNIX
        result = regcomp(&pattern.function_compiled, function,
                         REG_EXTENDED | REG_NOSUB);
        if (result != 0) {
            char description[256];
            regerror(result, &pattern.function_compiled, description,
                     SIZEOF(description));
            error2("invalid regex '%.*s': %s\n",
                   function_len, function, description);
            regfree(&pattern.compiled);
            return false;
        }
#endif
        pattern.has_function = true;
    }
    if (id != NULL) {
        pattern.rule_id = xstrndup(id, id_len);
        pattern.rule_id_len = id_len;
    }
    pattern.columns = columns;
    if (patterns->count == patterns->capacity) {
        int32 previous = patterns->capacity;
        int32 next = previous ? previous*2 : 8;
        if (previous > INT32_MAX/2) {
            return false;
        }
        if (previous == 0) {
            patterns->items = malloc2(next*SIZEOF(pattern));
        } else {
            patterns->items = realloc2(patterns->items, previous, next,
                                       SIZEOF(pattern));
        }
        patterns->capacity = next;
    }
    patterns->items[patterns->count] = pattern;
    patterns->count += 1;
    return true;
}

static void
csane_patterns_free(CsanePatterns *patterns) {
    for (int32 i = 0; i < patterns->count; i += 1) {
        CsanePattern *item = &patterns->items[i];
#if OS_UNIX
        regfree(&item->compiled);
        if (item->has_function) {
            regfree(&item->function_compiled);
        }
#endif
        if (item->rule_id != NULL) {
            free2(item->rule_id, item->rule_id_len + 1);
        }
    }
    if (patterns->items != NULL) {
        free2(patterns->items, patterns->capacity*SIZEOF(CsanePattern));
    }
    *patterns = (CsanePatterns){0};
    return;
}

static bool
csane_match(CsanePattern *pattern, char *path, int32 path_len) {
    while (path_len >= 2 && path[0] == '.' && path[1] == '/') {
        path += 2;
        path_len -= 2;
    }
#if OS_UNIX
    return regexec(&pattern->compiled, path, 0, NULL, 0) == 0;
#else
    (void)pattern;
    (void)path;
    return false;
#endif
}

bool
csane_policy_match(CsanePatterns *patterns, char *path, int32 path_len) {
    for (int32 i = 0; i < patterns->count; i += 1) {
        if (csane_match(&patterns->items[i], path, path_len)) {
            return true;
        }
    }
    return false;
}

/* The name pattern is a POSIX regex and therefore needs a terminated copy
 * of the borrowed token text. Its length is already known. */
static bool
csane_pattern_name_matches(CsanePattern *pattern, Token *name) {
    char *text = xstrndup(name->text, name->len);
    bool matches;
#if OS_UNIX
    matches = regexec(&pattern->function_compiled, text, 0, NULL, 0) == 0;
#else
    matches = false;
#endif
    free2(text, name->len + 1);
    return matches;
}

bool
csane_policy_rule_allowed(CsanePolicy *policy, char *path, int32 path_len,
                          char *rule_id, int32 rule_id_len, Token *function) {
    for (int32 i = 0; i < policy->rule_exemptions.count; i += 1) {
        CsanePattern *pattern = &policy->rule_exemptions.items[i];
        if (STREQUAL(pattern->rule_id, pattern->rule_id_len,
                     rule_id, rule_id_len)
            && csane_match(pattern, path, path_len)) {
            if (!pattern->has_function) {
                return true;
            }
            if (function != NULL
                && csane_pattern_name_matches(pattern, function)) {
                return true;
            }
        }
    }
    return false;
}

/* Unlike allow-function-rule, allow-macro-rule matches the invoked macro
 * name at file scope, not an enclosing function declaration. */
bool
csane_policy_macro_allowed(CsanePolicy *policy, char *path, int32 path_len,
                           char *rule_id, int32 rule_len, Token *macro) {
    for (int32 i = 0; i < policy->macro_exemptions.count; i += 1) {
        CsanePattern *pattern = &policy->macro_exemptions.items[i];
        if (STREQUAL(pattern->rule_id, pattern->rule_id_len,
                     rule_id, rule_len)
            && csane_match(pattern, path, path_len)
            && csane_pattern_name_matches(pattern, macro)) {
            return true;
        }
    }
    return false;
}

/* A typedef exception is matched against its alias, not against an
 * enclosing function or every declaration in the same source file. */
bool
csane_policy_typedef_allowed(CsanePolicy *policy, char *path,
                             int32 path_len, char *rule_id, int32 rule_id_len,
                             Token *alias) {
    for (int32 i = 0; i < policy->typedef_exemptions.count; i += 1) {
        CsanePattern *pattern = &policy->typedef_exemptions.items[i];
        if (STREQUAL(pattern->rule_id, pattern->rule_id_len,
                     rule_id, rule_id_len)
            && csane_match(pattern, path, path_len)
            && csane_pattern_name_matches(pattern, alias)) {
            return true;
        }
    }
    return false;
}

int32
csane_policy_columns(CsanePolicy *policy, char *path, int32 path_len) {
    char resolved[PATH_MAX];
    int32 columns = policy->default_columns;

    if (csane_policy_match(&policy->no_columns, path, path_len)) {
        return 0;
    }
    /* Match the legacy Perl checker's canonical cecup/src path, not the
     * diagnostic spelling (which may be just ./src/file.c).
     */
    if (realpath(path, resolved) != NULL
        && memmem64(resolved, strnlen32(resolved, PATH_MAX),
                    STRLIT("/cecup/src/")) != NULL) {
        columns = 100;
    }
    for (int32 i = 0; i < policy->column_overrides.count; i += 1) {
        CsanePattern *item = &policy->column_overrides.items[i];
        if (csane_match(item, path, path_len)) {
            columns = item->columns;
        }
    }
    return columns;
}

static bool
csane_path_prefix(char *path, int32 path_len, char *prefix,
                  int32 prefix_len) {
    if (!BEGINS_WITH(path, path_len, prefix, prefix_len)) {
        return false;
    }
    return path_len == prefix_len || path[prefix_len] == '/';
}

bool
csane_policy_add_root(CsanePolicy *policy, char *path, int32 path_len) {
    char resolved[PATH_MAX];
    int32 resolved_len;

    if (realpath(path, resolved) == NULL) {
        error2("%.*s: cannot resolve cbase directory\n", path_len, path);
        return false;
    }
    resolved_len = strnlen32(resolved, PATH_MAX);
    return csane_paths_add(&policy->cbase_roots, resolved, resolved_len);
}

/* As a fallback for isolated --file analysis, canonical directory segments
 * retain the original convention even without a project root in the cwd.
 */
static bool
csane_cbase_segment(char *path, int32 path_len) {
    return memmem64(path, path_len, STRLIT("/cbase/")) != NULL;
}

bool
csane_policy_in_cbase(CsanePolicy *policy, char *path, int32 path_len) {
    char resolved[PATH_MAX];
    int32 resolved_len;

    (void)path_len;
    if (realpath(path, resolved) == NULL) {
        return false;
    }
    resolved_len = strnlen32(resolved, PATH_MAX);
    for (int32 i = 0;
         i < strflex_list_len(&policy->cbase_roots.strings); i += 1) {
        if (csane_path_prefix(resolved, resolved_len,
                              policy->cbase_roots.strings.items[i]->data,
                              policy->cbase_roots.strings.items[i]->len)) {
            return true;
        }
    }
    return csane_cbase_segment(resolved, resolved_len);
}

bool
csane_policy_in_ryu(CsanePolicy *policy, char *path, int32 path_len) {
    char resolved[PATH_MAX];
    int32 resolved_len;

    (void)path_len;
    if (realpath(path, resolved) == NULL) {
        return false;
    }
    resolved_len = strnlen32(resolved, PATH_MAX);
    for (int32 i = 0;
         i < strflex_list_len(&policy->cbase_roots.strings); i += 1) {
        char *root = policy->cbase_roots.strings.items[i]->data;
        int32 length = policy->cbase_roots.strings.items[i]->len;
        if (resolved_len <= length + 5
            || !BEGINS_WITH(resolved, resolved_len, root, length)
            || !BEGINS_WITH(resolved + length, resolved_len - length,
                            "/ryu/")) {
            continue;
        }
        if (memchr64(resolved + length + 5, '/',
                     resolved_len - length - 5) != NULL) {
            return false;
        }
        return true;
    }
    if (csane_cbase_segment(resolved, resolved_len)) {
        for (int32 i = 0; i + 5 < resolved_len; i += 1) {
            if (resolved[i] == '/' && resolved[i + 1] == 'r'
                && resolved[i + 2] == 'y' && resolved[i + 3] == 'u'
                && resolved[i + 4] == '/') {
                for (int32 j = i + 5; j < resolved_len; j += 1) {
                    if (resolved[j] == '/') {
                        return false;
                    }
                }
                return true;
            }
        }
    }
    return false;
}

static char *
csane_join_path(char *root, int32 a, char *name, int32 b,
                int32 *path_len) {
    char *path = malloc2(a + b + 2);
    memcpy64(path, root, a);
    path[a] = '/';
    memcpy64(path + a + 1, name, b);
    *path_len = a + b + 1;
    path[*path_len] = '\0';
    return path;
}

static bool
csane_exists(char *path, int32 path_len) {
    struct stat st;
    ASSERT(path[path_len] == '\0');
    return stat(path, &st) == 0;
}

static bool
csane_load_names(CsanePaths *names, char *path, int32 path_len) {
    char *text = NULL;
    int32 len = read_entire_file(path, &text);
    bool ok = true;
    int32 start = 0;

    if (len < 0) {
        error2("%.*s: unable to read policy names\n", path_len, path);
        return false;
    }
    csane_paths_free(names);
    for (int32 i = 0; i <= len; i += 1) {
        int32 begin;
        int32 end;

        if (i < len && text[i] != '\n') {
            continue;
        }
        begin = start;
        end = i;
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
            begin += 1;
        }
        if (begin < end && text[begin] != '#') {
            int32 word_end = begin;
            while (word_end < end && text[word_end] != ' '
                   && text[word_end] != '\t' && text[word_end] != '\r') {
                word_end += 1;
            }
            if (!csane_paths_add(names, text + begin, word_end - begin)) {
                ok = false;
                break;
            }
        }
        start = i + 1;
    }
    free2(text, len + 1);
    return ok;
}

/* The checked-in lists take priority over compiled fallback names. This also
 * supports projects using cbase as a standalone source directory.
 */
bool
csane_policy_project(CsanePolicy *policy, char *project,
                     int32 project_len) {
    char resolved[PATH_MAX];
    bool ok = true;
    char *cbase;
    char *root;
    char *names;
    int32 cbase_len;
    int32 names_len;
    int32 root_len;
    int32 resolved_len;

    if (realpath(project, resolved) == NULL) {
        error2("%.*s: cannot resolve project directory\n",
               project_len, project);
        return false;
    }
    resolved_len = strnlen32(resolved, PATH_MAX);
    if (!csane_paths_add(&policy->project_roots, resolved, resolved_len)) {
        error2("%.*s: cannot resolve project directory\n",
               project_len, project);
        return false;
    }
    cbase = csane_join_path(project, project_len, STRLIT("cbase"), &cbase_len);
    root = cbase;
    root_len = cbase_len;
    names = csane_join_path(root, root_len, STRLIT("functions_never_use.txt"),
                            &names_len);
    if (!csane_exists(names, names_len)) {
        free2(names, names_len + 1);
        root = project;
        root_len = project_len;
        names = csane_join_path(root, root_len,
                                STRLIT("functions_never_use.txt"), &names_len);
    }
    if (csane_exists(names, names_len)) {
        ok = csane_load_names(&policy->never_names, names, names_len) && ok;
        if (root == cbase) {
            ok = csane_policy_add_root(policy, root, root_len) && ok;
        } else {
            ok = csane_policy_add_root(policy, project, project_len) && ok;
        }
    } else if (csane_exists(cbase, cbase_len)) {
        ok = csane_policy_add_root(policy, cbase, cbase_len) && ok;
    }
    free2(names, names_len + 1);
    names = csane_join_path(root, root_len,
                            STRLIT("functions_allowed_cbase_only.txt"),
                            &names_len);
    if (csane_exists(names, names_len)) {
        ok = csane_load_names(&policy->cbase_names, names, names_len) && ok;
    }
    free2(names, names_len + 1);
    names = csane_join_path(project, project_len, STRLIT("piscou.1"),
                            &names_len);
    if (csane_exists(names, names_len)) {
        int32 config_len;
        char *config = csane_join_path(project, project_len, STRLIT("config.h"),
                                       &config_len);
        if (csane_exists(config, config_len)) {
            ok = csane_policy_add_regex(&policy->no_columns,
                                        STRLIT("(^|/)config\\.h$"),
                                        NULL, 0, NULL, 0, 0)
                 && ok;
        }
        free2(config, config_len + 1);
    }
    free2(names, names_len + 1);
    names = csane_join_path(root, root_len, STRLIT("csanecheck.conf"),
                            &names_len);
    if (csane_exists(names, names_len)) {
        char resolved_config[PATH_MAX];
        bool loaded = false;

        if (realpath(names, resolved_config) == NULL) {
            ok = false;
        } else {
            int32 config_len = strnlen32(resolved_config, PATH_MAX);
            int32 count = strflex_list_len(&policy->configs.strings);
            for (int32 i = 0; i < count; i += 1) {
                if (STREQUAL(policy->configs.strings.items[i]->data,
                             policy->configs.strings.items[i]->len,
                             resolved_config, config_len)) {
                    loaded = true;
                    break;
                }
            }
            if (!loaded) {
                loaded = csane_paths_add(&policy->configs, resolved_config,
                                         config_len);
                ok = loaded && ok;
                if (loaded) {
                    ok = csane_policy_config(policy, resolved_config,
                                             config_len) && ok;
                }
            }
        }
    }
    free2(names, names_len + 1);
    free2(cbase, cbase_len + 1);
    return ok;
}

bool
csane_policy_source(CsanePolicy *policy, char *source, int32 source_len) {
    char directory[PATH_MAX];
    char resolved[PATH_MAX];
    char *slash = NULL;
    int32 len = source_len;
    int32 resolved_len;

    if (len >= PATH_MAX) {
        return false;
    }
    memcpy64(directory, source, len);
    directory[len] = '\0';
    for (int32 i = 0; i < len; i += 1) {
        if (directory[i] == '/') {
            slash = directory + i;
        }
    }
    if (slash == NULL) {
        directory[0] = '.';
        directory[1] = '\0';
    } else if (slash == directory) {
        directory[1] = '\0';
    } else {
        *slash = '\0';
    }
    if (realpath(directory, resolved) == NULL) {
        return true;
    }
    resolved_len = strnlen32(resolved, PATH_MAX);
    for (;;) {
        char *names;
        char *cbase;
        int32 names_len;
        int32 cbase_len;
        int32 leaf_len;
        bool found;

        /* An explicit source inside a conventional source directory still
         * establishes its project, even without policy name files. This
         * keeps anchored exclusions project-relative outside that cwd.
         */
        char *leaf = resolved;
        for (int32 i = 0; i < resolved_len; i += 1) {
            if (resolved[i] == '/') {
                leaf = resolved + i + 1;
            }
        }
        leaf_len = resolved_len - (int32)(leaf - resolved);
        if (STREQUAL(leaf, leaf_len, "src")
            || STREQUAL(leaf, leaf_len, "test")
            || STREQUAL(leaf, leaf_len, "tests")
            || STREQUAL(leaf, leaf_len, "examples")
            || STREQUAL(leaf, leaf_len, "tools")
            || STREQUAL(leaf, leaf_len, "cyblocks")
            || STREQUAL(leaf, leaf_len, "cbase")) {
            if (leaf > resolved + 1) {
                bool added;

                leaf[-1] = '\0';
                added = csane_paths_add(&policy->project_roots, resolved,
                                        (int32)(leaf - resolved) - 1);
                leaf[-1] = '/';
                if (!added) {
                    return false;
                }
            }
        }
        names = csane_join_path(resolved, resolved_len,
                                STRLIT("functions_never_use.txt"), &names_len);
        cbase = csane_join_path(resolved, resolved_len,
                                STRLIT("cbase/functions_never_use.txt"),
                                &cbase_len);
        found = csane_exists(names, names_len)
                || csane_exists(cbase, cbase_len);
        free2(names, names_len + 1);
        free2(cbase, cbase_len + 1);
        if (found) {
            return csane_policy_project(policy, resolved, resolved_len);
        }
        len = resolved_len;
        while (len > 1 && resolved[len - 1] != '/') {
            len -= 1;
        }
        if (len <= 1) {
            break;
        }
        resolved[len - 1] = '\0';
        resolved_len = len - 1;
    }
    return true;
}

bool
csane_policy_init(CsanePolicy *policy) {
    *policy = (CsanePolicy){.default_columns = 80};
    for (int32 i = 0; i < LENGTH(csane_default_never); i += 1) {
        if (!csane_paths_add(&policy->never_names,
                             csane_default_never[i]->data,
                             csane_default_never[i]->len)) {
            csane_policy_free(policy);
            return false;
        }
    }
    for (int32 i = 0; i < (int32)(SIZEOF(csane_default_cbase)
                                /SIZEOF(csane_default_cbase[0])); i += 1) {
        if (!csane_paths_add(&policy->cbase_names,
                             csane_default_cbase[i]->data,
                             csane_default_cbase[i]->len)) {
            csane_policy_free(policy);
            return false;
        }
    }
    return true;
}

void
csane_policy_free(CsanePolicy *policy) {
    csane_paths_free(&policy->never_names);
    csane_paths_free(&policy->cbase_names);
    csane_paths_free(&policy->cbase_roots);
    csane_paths_free(&policy->project_roots);
    csane_paths_free(&policy->configs);
    csane_patterns_free(&policy->excludes);
    csane_patterns_free(&policy->no_columns);
    csane_patterns_free(&policy->column_overrides);
    csane_patterns_free(&policy->rule_exemptions);
    csane_patterns_free(&policy->macro_exemptions);
    csane_patterns_free(&policy->typedef_exemptions);
    *policy = (CsanePolicy){0};
    return;
}

/* Return the next word's length, or zero if no word remains.  Tokens are
 * terminated in place for the existing regex and filesystem interfaces. */
static int32
csane_word(char *line, int32 line_len, int32 *offset, char **word) {
    int32 start = *offset;
    int32 end;

    *word = NULL;
    while (start < line_len && (line[start] == ' ' || line[start] == '\t'
           || line[start] == '\r')) {
        start += 1;
    }
    end = start;
    while (end < line_len && line[end] != ' ' && line[end] != '\t'
           && line[end] != '\r' && line[end] != '\0') {
        end += 1;
    }
    *offset = end;
    if (end == start) {
        return 0;
    }
    *word = line + start;
    if (end < line_len) {
        line[end] = '\0';
        *offset += 1;
    }
    return end - start;
}

static bool
csane_identifier(char *name, int32 len) {
    if (name == NULL || len == 0
        || !((name[0] >= 'A' && name[0] <= 'Z')
             || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) {
        return false;
    }
    for (int32 i = 1; i < len; i += 1) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static StrFlex *csane_known_rules[] = {
    SFLIT("API-NEVER"),
    SFLIT("API-CBASE-ONLY"),
    SFLIT("API-LIBC-PRINTF"),
    SFLIT("PP-DEFINED"),
    SFLIT("PP-SIZEOF"),
    SFLIT("SRC-TABS"),
    SFLIT("SRC-TRAILING"),
    SFLIT("SRC-COLUMNS"),
    SFLIT("RYU-ENDIF-COMMENT"),
    SFLIT("RYU-FINAL-NEWLINE"),
    SFLIT("AST-DECL-ONE"),
    SFLIT("AST-BRACES"),
    SFLIT("AST-SWITCH-ALIGN"),
    SFLIT("AST-ARTIFICIAL-BLOCK"),
    SFLIT("STYLE-ASSERT-REL"),
    SFLIT("STYLE-ASSERT-BOUNDS"),
    SFLIT("STYLE-ASSERT-CLOSE-ZERO"),
    SFLIT("STYLE-STRLIT-LEN"),
    SFLIT("STYLE-STREQUAL"),
    SFLIT("STYLE-PRINTF"),
    SFLIT("STYLE-STATUS-TEMP"),
    SFLIT("FMT-MULTIPLY"),
    SFLIT("FMT-BINARY-SPACING"),
    SFLIT("FMT-ASSIGN-SPACING"),
    SFLIT("FMT-ASSIGN-EQ"),
    SFLIT("FMT-CALL-FIRST"),
    SFLIT("FMT-CALL-FIRST-TOPLEVEL"),
    SFLIT("FMT-FUNC-ALIGN"),
    SFLIT("FMT-FUNC-WRAP"),
    SFLIT("FMT-LITERAL-ALIGN"),
    SFLIT("FMT-ARG-ALIGN"),
    SFLIT("FMT-FORMAT-LINE"),
    SFLIT("FMT-FORMAT-ARGS-WRAP"),
    SFLIT("FMT-RELATED-ARGS"),
    SFLIT("FMT-CALL-WRAP"),
    SFLIT("FMT-ASSIGN-WRAP"),
    SFLIT("FMT-COMMA-SPACING"),
    SFLIT("FMT-MEMBER-SPACING"),
    SFLIT("FMT-UNARY-SPACING"),
    SFLIT("FMT-CAST-SPACING"),
    SFLIT("FMT-POINTER-SPACING"),
    SFLIT("FMT-CALL-SPACING"),
    SFLIT("FMT-TRAILING-COMMA"),
    SFLIT("STYLE-INCDEC"),
    SFLIT("TYPE-CONST-SIGNATURE"),
    SFLIT("TYPE-STDINT-T"),
    SFLIT("TYPE-ENUM-TYPEDEF"),
    SFLIT("TYPE-UNION-TYPEDEF"),
    SFLIT("TYPE-POINTER-TYPEDEF"),
};

static bool
csane_known_rule(char *id, int32 id_len) {
    for (int32 i = 0; i < LENGTH(csane_known_rules); i += 1) {
        StrFlex *known = csane_known_rules[i];
        if (STREQUAL(id, id_len, known->data, known->len)) {
            return true;
        }
    }
    return false;
}

/* Policy filenames are resolved relative to the config file itself, making
 * --config portable when the executable is invoked from another directory.
 */
static char *
csane_config_path(char *filename, int32 filename_len, char *path,
                  int32 path_len, int32 *result_len) {
    int32 slash = -1;
    char *result;

    if (path[0] == '/') {
        *result_len = path_len;
        return xstrndup(path, path_len);
    }
    for (int32 i = 0; i < filename_len; i += 1) {
        if (filename[i] == '/') {
            slash = i;
        }
    }
    if (slash < 0) {
        *result_len = path_len;
        return xstrndup(path, path_len);
    }
    *result_len = slash + 1 + path_len;
    result = malloc2(*result_len + 1);
    memcpy64(result, filename, slash + 1);
    memcpy64(result + slash + 1, path, path_len);
    result[*result_len] = '\0';
    return result;
}

static bool
csane_config_line(CsanePolicy *policy, char *line, int32 line_len,
                  char *filename, int32 filename_len, int32 lineno) {
    int32 offset = 0;
    char *key;
    char *first;
    char *second;
    char *extra;
    char *fourth;
    int32 key_len = csane_word(line, line_len, &offset, &key);
    int32 first_len;
    int32 second_len;
    int32 extra_len;
    int32 columns;
    bool ok = false;

    if (key == NULL || key[0] == '#') {
        return true;
    }
    first_len = csane_word(line, line_len, &offset, &first);
    second_len = csane_word(line, line_len, &offset, &second);
    extra_len = csane_word(line, line_len, &offset, &extra);
    csane_word(line, line_len, &offset, &fourth);
    if (STREQUAL(key, key_len, "exclude")
        && first && !second) {
        ok = csane_policy_add_regex(&policy->excludes, first, first_len,
                                    NULL, 0, NULL, 0, 0);
    } else if (STREQUAL(key, key_len, "no-column-limit")
        && first && !second) {
        ok = csane_policy_add_regex(&policy->no_columns, first, first_len,
                                    NULL, 0, NULL, 0, 0);
    } else if (STREQUAL(key, key_len, "column-limit") && first && !second
               && csane_number(first, first_len, &columns)) {
        policy->default_columns = columns;
        ok = true;
    } else if (STREQUAL(key, key_len, "columns")
               && first && second && !extra
               && csane_number(second, second_len, &columns)) {
        ok = csane_policy_add_regex(&policy->column_overrides, first, first_len,
                                    NULL, 0, NULL, 0, columns);
    } else if (STREQUAL(key, key_len, "allow-rule")
               && first && second && !extra
               && csane_known_rule(first, first_len)) {
        ok = csane_policy_add_regex(&policy->rule_exemptions,
                                    second, second_len,
                                    first, first_len, NULL, 0, 0);
    } else if (STREQUAL(key, key_len, "allow-function-rule")
               && first && second && extra && !fourth
               && csane_known_rule(first, first_len)) {
        ok = csane_policy_add_regex(&policy->rule_exemptions,
                                    second, second_len,
                                    first, first_len,
                                    extra, extra_len, 0);
    } else if (STREQUAL(key, key_len, "allow-typedef-rule")
               && first && second && extra && !fourth
               && STREQUAL(first, first_len, "TYPE-POINTER-TYPEDEF")) {
        ok = csane_policy_add_regex(&policy->typedef_exemptions,
                                    second, second_len,
                                    first, first_len,
                                    extra, extra_len, 0);
    } else if (STREQUAL(key, key_len, "allow-macro-rule")
               && first && second && extra && !fourth
               && STREQUAL(first, first_len, "FMT-CALL-FIRST-TOPLEVEL")) {
        ok = csane_policy_add_regex(&policy->macro_exemptions,
                                    second, second_len,
                                    first, first_len,
                                    extra, extra_len, 0);
    } else if (STREQUAL(key, key_len, "never-use")
               && csane_identifier(first, first_len)
               && !second) {
        ok = csane_paths_add(&policy->never_names, first, first_len);
    } else if (STREQUAL(key, key_len, "cbase-only")
               && csane_identifier(first, first_len)
               && !second) {
        ok = csane_paths_add(&policy->cbase_names, first, first_len);
    } else if (STREQUAL(key, key_len, "clear-never-use") && !first) {
        csane_paths_free(&policy->never_names);
        ok = true;
    } else if (STREQUAL(key, key_len, "clear-cbase-only") && !first) {
        csane_paths_free(&policy->cbase_names);
        ok = true;
    } else if (STREQUAL(key, key_len, "never-use-file")
        && first && !second) {
        int32 path_len;
        char *path = csane_config_path(filename, filename_len,
                                       first, first_len, &path_len);
        ok = csane_load_names(&policy->never_names, path, path_len);
        free2(path, path_len + 1);
    } else if (STREQUAL(key, key_len, "cbase-only-file")
        && first && !second) {
        int32 path_len;
        char *path = csane_config_path(filename, filename_len,
                                       first, first_len, &path_len);
        ok = csane_load_names(&policy->cbase_names, path, path_len);
        free2(path, path_len + 1);
    } else if (STREQUAL(key, key_len, "cbase-root")
        && first && !second) {
        int32 path_len;
        char *path = csane_config_path(filename, filename_len,
                                       first, first_len, &path_len);
        ok = csane_policy_add_root(policy, path, path_len);
        free2(path, path_len + 1);
    }
    if (!ok) {
        error2("%.*s:%d: invalid csanecheck policy directive: %.*s\n",
               filename_len, filename, lineno, key_len, key);
    }
    return ok;
}

bool
csane_policy_config(CsanePolicy *policy, char *filename,
                    int32 filename_len) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    bool ok = true;
    int32 start = 0;
    int32 lineno = 1;

    if (len < 0) {
        error2("%.*s: unable to read csanecheck policy\n",
               filename_len, filename);
        return false;
    }
    for (int32 i = 0; i <= len; i += 1) {
        if (i < len && text[i] != '\n') {
            continue;
        }
        text[i] = '\0';
        if (!csane_config_line(policy, text + start, i - start,
                               filename, filename_len, lineno)) {
            ok = false;
        }
        start = i + 1;
        lineno += 1;
    }
    free2(text, len + 1);
    return ok;
}

static bool
csane_ryu_filter(char *filter, int32 len) {
    if (filter == NULL) {
        return false;
    }
    if (ENDS_WITH(filter, len, "ryu")) {
        return len == 3 || filter[len - 4] == '/';
    }
    if (ENDS_WITH(filter, len, "ryu.c")) {
        return len == 5 || filter[len - 6] == '/';
    }
    return false;
}

bool
csane_policy_filter(char *path, int32 path_len, char *filter,
                    int32 filter_len) {
    char *basename = path;
    char *filter_base = filter;
    int32 n;
    int32 m;

    if (filter == NULL || filter_len == 0) {
        return true;
    }
    for (int32 i = 0; i < path_len; i += 1) {
        if (path[i] == '/') {
            basename = path + i + 1;
        }
    }
    for (int32 i = 0; i < filter_len; i += 1) {
        if (filter[i] == '/') {
            filter_base = filter + i + 1;
        }
    }
    n = path_len - (int32)(basename - path);
    m = filter_len - (int32)(filter_base - filter);
    if (STREQUAL(path, path_len, filter, filter_len)
        || STREQUAL(basename, n, filter_base, m)) {
        return true;
    }
    if (ENDS_WITH(basename, n, ".h") || ENDS_WITH(basename, n, ".H")
        || ENDS_WITH(basename, n, ".C")) {
        return true;
    }
    if (ENDS_WITH(basename, n, ".c")) {
        n -= 2;
    }
    if (ENDS_WITH(filter_base, m, ".c")) {
        m -= 2;
    }
    if (STREQUAL(basename, n, filter_base, m)) {
        return true;
    }
    if (csane_ryu_filter(filter, filter_len)
        && ENDS_WITH(basename, path_len - (int32)(basename - path), ".c")) {
        char *ryu = NULL;
        if (BEGINS_WITH(path, path_len, "ryu/")) {
            ryu = path + 4;
        } else {
            for (int32 i = 0; i + 5 <= path_len; i += 1) {
                if (path[i] == '/'
                    && BEGINS_WITH(path + i + 1, path_len - i - 1, "ryu/")) {
                    ryu = path + i + 5;
                    break;
                }
            }
        }
        if (ryu != NULL) {
            int32 ryu_len = path_len - (int32)(ryu - path);
            for (int32 i = 0; i < ryu_len; i += 1) {
                if (ryu[i] == '/') {
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

/* Resolve the containing directory, not the source file: file symlinks
 * retain their selected name (for example src/alias.c). Also normalize
 * project-relative paths supplied outside the project working directory.
 */
static char *
csane_policy_selection_path(CsanePolicy *policy,
                            char *path, int32 path_len,
                            char *resolved, int32 *selected_len) {
    char directory[PATH_MAX];
    char *basename = path;
    int32 longest = -1;
    char *selected = path;
    int32 size;
    int32 dir_len;
    int32 base_len;
    int32 count = strflex_list_len(&policy->project_roots.strings);
    int32 resolved_path_len;

    *selected_len = path_len;
    for (int32 j = 0; j < path_len; j += 1) {
        if (path[j] == '/') {
            basename = path + j + 1;
        }
    }
    for (int32 i = 0; i < count; i += 1) {
        char *root = policy->project_roots.strings.items[i]->data;
        int32 length = policy->project_roots.strings.items[i]->len;
        if (length > longest && csane_path_prefix(path, path_len, root, length)
            && path[length] == '/') {
            selected = path + length + 1;
            *selected_len = path_len - length - 1;
            longest = length;
        }
    }
    if (longest >= 0) {
        return selected;
    }
    size = (int32)(basename - path);
    if (size >= PATH_MAX) {
        return path;
    }
    if (size == 0) {
        directory[0] = '.';
        directory[1] = '\0';
    } else if (size == 1) {
        directory[0] = '/';
        directory[1] = '\0';
    } else {
        memcpy64(directory, path, size - 1);
        directory[size - 1] = '\0';
    }
    if (realpath(directory, resolved) == NULL) {
        return path;
    }
    dir_len = strnlen32(resolved, PATH_MAX);
    base_len = path_len - (int32)(basename - path);
    if (dir_len + base_len + 2 >= PATH_MAX) {
        return path;
    }
    resolved[dir_len] = '/';
    memcpy64(resolved + dir_len + 1, basename, base_len);
    resolved_path_len = dir_len + base_len + 1;
    resolved[resolved_path_len] = '\0';
    for (int32 i = 0; i < count; i += 1) {
        char *root = policy->project_roots.strings.items[i]->data;
        int32 length = policy->project_roots.strings.items[i]->len;
        if (length > longest
            && csane_path_prefix(resolved, resolved_path_len, root, length)
            && resolved[length] == '/') {
            selected = resolved + length + 1;
            *selected_len = resolved_path_len - length - 1;
            longest = length;
        }
    }
    return selected;
}

bool
csane_policy_selected(CsanePolicy *policy, char *path, int32 path_len,
                      char *filter, int32 filter_len) {
    char resolved[PATH_MAX];
    int32 source_len;
    char *source = csane_policy_selection_path(policy, path, path_len,
                                               resolved, &source_len);
    if (!csane_source_allowed(source, source_len)
        || csane_policy_match(&policy->excludes, source, source_len)) {
        return false;
    }
    return csane_policy_filter(source, source_len, filter, filter_len);
}

#endif /* CSANECHECK_CONFIG_C */
