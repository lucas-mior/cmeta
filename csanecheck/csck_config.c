// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CONFIG_C)
#define CSANECHECK_CONFIG_C

#include "cbase.h"
#include "csck.h"

static char *csane_default_never[] = {
    "stpcpy", "stpncpy", "strcat", "strchr", "strcpy", "strcspn",
    "strdup", "strncat", "strncpy", "strndup", "strpbrk", "strrchr",
    "strspn", "strstr", "strtok", "strtok_r", "strxfrm", "strxfrm_l",
    "strsep", "isalnum", "isalpha", "isascii", "isblank", "iscntrl",
    "isdigit", "isgraph", "islower", "isprint", "ispunct", "isspace",
    "isupper", "isxdigit",
};

static char *csane_default_cbase[] = {
    "memccpy", "memchr", "memcmp", "memcpy", "memmove", "memset",
    "strlen", "strncmp", "strnlen",
};

static char *
csane_copy(char *text) {
    int32 len = strlen32(text);
    char *result = malloc2((int64)len + 1);
    memcpy64(result, text, (int64)len + 1);
    return result;
}

static bool
csane_number(char *text, int32 *out) {
    char *end;
    long value;
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < 1 || value >= INT32_MAX) {
        return false;
    }
    *out = (int32)value;
    return true;
}

bool
csane_policy_add_regex(CsanePatterns *patterns, char *text, char *id,
                       int32 columns) {
    CsanePattern pattern = {0};
    if (patterns->count == INT32_MAX
        || (patterns->count == patterns->capacity
            && patterns->capacity > INT32_MAX/2)) {
        return false;
    }
#if OS_UNIX
    int result = regcomp(&pattern.compiled, text, REG_EXTENDED | REG_NOSUB);
    if (result != 0) {
        char description[256];
        regerror(result, &pattern.compiled, description, SIZEOF(description));
        fprintf(stderr, "invalid regex '%s': %s\n", text, description);
        return false;
    }
#else
    fprintf(stderr, "POSIX regular expressions are unavailable: %s\n", text);
    return false;
#endif
    pattern.text = csane_copy(text);
    if (id != NULL) {
        pattern.rule_id = csane_copy(id);
    }
    pattern.columns = columns;
    if (patterns->count == patterns->capacity) {
        int32 previous = patterns->capacity;
        int32 next = previous ? previous*2 : 8;
        if (previous > INT32_MAX/2) {
            return false;
        }
        if (previous == 0) {
            patterns->items = malloc2((int64)next*SIZEOF(pattern));
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
#endif
        free2(item->text, (int64)strlen32(item->text) + 1);
        if (item->rule_id != NULL) {
            free2(item->rule_id, (int64)strlen32(item->rule_id) + 1);
        }
    }
    if (patterns->items != NULL) {
        free2(patterns->items, (int64)patterns->capacity*SIZEOF(CsanePattern));
    }
    *patterns = (CsanePatterns){0};
    return;
}

static bool
csane_match(CsanePattern *pattern, char *path) {
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
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
csane_policy_match(CsanePatterns *patterns, char *path) {
    for (int32 i = 0; i < patterns->count; i += 1) {
        if (csane_match(&patterns->items[i], path)) {
            return true;
        }
    }
    return false;
}

bool
csane_policy_rule_allowed(CsanePolicy *policy, char *path, char *rule_id) {
    for (int32 i = 0; i < policy->rule_exemptions.count; i += 1) {
        CsanePattern *pattern = &policy->rule_exemptions.items[i];
        if (!strcmp(pattern->rule_id, rule_id)
            && csane_match(pattern, path)) {
            return true;
        }
    }
    return false;
}

int32
csane_policy_columns(CsanePolicy *policy, char *path) {
    int32 columns = policy->default_columns;
    if (csane_policy_match(&policy->no_columns, path)) {
        return 0;
    }
    for (int32 i = 0; i < policy->column_overrides.count; i += 1) {
        CsanePattern *item = &policy->column_overrides.items[i];
        if (csane_match(item, path)) {
            columns = item->columns;
        }
    }
    return columns;
}

static bool
csane_path_prefix(char *path, char *prefix) {
    int32 len = strlen32(prefix);
    if (strlen32(path) < len || memcmp64(path, prefix, len) != 0) {
        return false;
    }
    return path[len] == '/' || path[len] == '\0';
}

bool
csane_policy_add_root(CsanePolicy *policy, char *path) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        fprintf(stderr, "%s: cannot resolve cbase directory\n", path);
        return false;
    }
    return csane_paths_add(&policy->cbase_roots, resolved);
}

/* As a fallback for isolated --file analysis, canonical directory segments
 * retain the original convention even without a project root in the cwd.
 */
static bool
csane_cbase_segment(char *path) {
    for (char *p = path; *p; p += 1) {
        if (p[0] == '/' && p[1] == 'c' && p[2] == 'b' && p[3] == 'a'
            && p[4] == 's' && p[5] == 'e' && p[6] == '/') {
            return true;
        }
    }
    return false;
}

bool
csane_policy_in_cbase(CsanePolicy *policy, char *path) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        return false;
    }
    for (int32 i = 0; i < policy->cbase_roots.count; i += 1) {
        if (csane_path_prefix(resolved, policy->cbase_roots.items[i])) {
            return true;
        }
    }
    return csane_cbase_segment(resolved);
}

bool
csane_policy_in_ryu(CsanePolicy *policy, char *path) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        return false;
    }
    for (int32 i = 0; i < policy->cbase_roots.count; i += 1) {
        char *root = policy->cbase_roots.items[i];
        int32 length = strlen32(root);
        if (strlen32(resolved) <= length + 5
            || memcmp64(resolved, root, length)
               || memcmp64(resolved + length, "/ryu/", 5)) {
            continue;
        }
        for (char *p = resolved + length + 5; *p; p += 1) {
            if (*p == '/') {
                return false;
            }
        }
        return true;
    }
    if (csane_cbase_segment(resolved)) {
        int32 len = strlen32(resolved);
        for (int32 i = 0; i + 5 < len; i += 1) {
            if (resolved[i] == '/' && resolved[i + 1] == 'r'
                && resolved[i + 2] == 'y' && resolved[i + 3] == 'u'
                && resolved[i + 4] == '/') {
                for (int32 j = i + 5; j < len; j += 1) {
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
csane_join_path(char *root, char *name) {
    int32 a = strlen32(root);
    int32 b = strlen32(name);
    char *path = malloc2((int64)a + b + 2);
    memcpy64(path, root, a);
    path[a] = '/';
    memcpy64(path + a + 1, name, (int64)b + 1);
    return path;
}

static bool
csane_exists(char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool
csane_load_names(CsanePaths *names, char *path) {
    char *text = NULL;
    int32 len = read_entire_file(path, &text);
    if (len < 0) {
        fprintf(stderr, "%s: unable to read policy names\n", path);
        return false;
    }
    csane_paths_free(names);
    bool ok = true;
    int32 start = 0;
    for (int32 i = 0; i <= len; i += 1) {
        if (i < len && text[i] != '\n') {
            continue;
        }
        text[i] = '\0';
        char *word = text + start;
        while (*word == ' ' || *word == '\t') {
            word += 1;
        }
        if (*word != '#' && *word != '\0') {
            char *end = word;
            while (*end && *end != ' ' && *end != '\t' && *end != '\r') {
                end += 1;
            }
            *end = '\0';
            if (!csane_paths_add(names, word)) {
                ok = false;
                break;
            }
        }
        start = i + 1;
    }
    free2(text, (int64)len + 1);
    return ok;
}

/* The checked-in lists take priority over compiled fallback names. This also
 * supports projects using cbase as a standalone source directory.
 */
bool
csane_policy_project(CsanePolicy *policy, char *project) {
    char resolved[PATH_MAX];
    if (realpath(project, resolved) == NULL
        || !csane_paths_add(&policy->project_roots, resolved)) {
        fprintf(stderr, "%s: cannot resolve project directory\n", project);
        return false;
    }
    bool ok = true;
    char *cbase = csane_join_path(project, "cbase");
    char *root = cbase;
    char *names = csane_join_path(root, "functions_never_use.txt");
    if (!csane_exists(names)) {
        free2(names, (int64)strlen32(names) + 1);
        root = project;
        names = csane_join_path(root, "functions_never_use.txt");
    }
    if (csane_exists(names)) {
        ok = csane_load_names(&policy->never_names, names) && ok;
        if (root == cbase) {
            ok = csane_policy_add_root(policy, root) && ok;
        } else {
            ok = csane_policy_add_root(policy, project) && ok;
        }
    } else if (csane_exists(cbase)) {
        ok = csane_policy_add_root(policy, cbase) && ok;
    }
    free2(names, (int64)strlen32(names) + 1);
    names = csane_join_path(root, "functions_allowed_cbase_only.txt");
    if (csane_exists(names)) {
        ok = csane_load_names(&policy->cbase_names, names) && ok;
    }
    free2(names, (int64)strlen32(names) + 1);
    names = csane_join_path(project, "piscou.1");
    if (csane_exists(names)) {
        char *config = csane_join_path(project, "config.h");
        if (csane_exists(config)) {
            ok = csane_policy_add_regex(&policy->no_columns,
                                        "(^|/)config\\.h$", NULL, 0)
                 && ok;
        }
        free2(config, (int64)strlen32(config) + 1);
    }
    free2(names, (int64)strlen32(names) + 1);
    free2(cbase, (int64)strlen32(cbase) + 1);
    return ok;
}

bool
csane_policy_source(CsanePolicy *policy, char *source) {
    char directory[PATH_MAX];
    char *slash = NULL;
    int32 len = strlen32(source);
    if (len >= PATH_MAX) {
        return false;
    }
    memcpy64(directory, source, (int64)len + 1);
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
    char resolved[PATH_MAX];
    if (realpath(directory, resolved) == NULL) {
        return true;
    }
    for (;;) {
        /* An explicit source inside a conventional source directory still
         * establishes its project, even without policy name files. This
         * keeps anchored exclusions project-relative outside that cwd.
         */
        char *leaf = resolved;
        for (char *p = resolved; *p; p += 1) {
            if (*p == '/') {
                leaf = p + 1;
            }
        }
        if (!strcmp(leaf, "src") || !strcmp(leaf, "test")
            || !strcmp(leaf, "tests") || !strcmp(leaf, "examples")
            || !strcmp(leaf, "tools") || !strcmp(leaf, "cyblocks")
            || !strcmp(leaf, "cbase")) {
            if (leaf > resolved + 1) {
                leaf[-1] = '\0';
                bool added = csane_paths_add(&policy->project_roots, resolved);
                leaf[-1] = '/';
                if (!added) {
                    return false;
                }
            }
        }
        char *names = csane_join_path(resolved, "functions_never_use.txt");
        char *cbase = csane_join_path(resolved,
                                      "cbase/functions_never_use.txt");
        bool found = csane_exists(names) || csane_exists(cbase);
        free2(names, (int64)strlen32(names) + 1);
        free2(cbase, (int64)strlen32(cbase) + 1);
        if (found) {
            return csane_policy_project(policy, resolved);
        }
        len = strlen32(resolved);
        while (len > 1 && resolved[len - 1] != '/') {
            len -= 1;
        }
        if (len <= 1) {
            break;
        }
        resolved[len - 1] = '\0';
    }
    return true;
}

bool
csane_policy_init(CsanePolicy *policy) {
    *policy = (CsanePolicy){.default_columns = 80};
    for (int32 i = 0; i < (int32)(SIZEOF(csane_default_never)
                                /SIZEOF(csane_default_never[0])); i += 1) {
        if (!csane_paths_add(&policy->never_names, csane_default_never[i])) {
            csane_policy_free(policy);
            return false;
        }
    }
    for (int32 i = 0; i < (int32)(SIZEOF(csane_default_cbase)
                                /SIZEOF(csane_default_cbase[0])); i += 1) {
        if (!csane_paths_add(&policy->cbase_names, csane_default_cbase[i])) {
            csane_policy_free(policy);
            return false;
        }
    }
    if (!csane_policy_add_regex(&policy->column_overrides,
                                "(^|/)cecup/src/", NULL, 100)) {
        csane_policy_free(policy);
        return false;
    }
    return true;
}

void
csane_policy_free(CsanePolicy *policy) {
    csane_paths_free(&policy->never_names);
    csane_paths_free(&policy->cbase_names);
    csane_paths_free(&policy->cbase_roots);
    csane_paths_free(&policy->project_roots);
    csane_patterns_free(&policy->excludes);
    csane_patterns_free(&policy->no_columns);
    csane_patterns_free(&policy->column_overrides);
    csane_patterns_free(&policy->rule_exemptions);
    *policy = (CsanePolicy){0};
    return;
}

static char *
csane_word(char **cursor) {
    char *start = *cursor;
    while (*start == ' ' || *start == '\t' || *start == '\r') {
        start += 1;
    }
    char *end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t' && *end != '\r') {
        end += 1;
    }
    if (*end) {
        *end = '\0';
        end += 1;
    }
    *cursor = end;
    return *start ? start : NULL;
}

static bool
csane_identifier(char *name) {
    if (name == NULL || !((*name >= 'A' && *name <= 'Z')
                          || (*name >= 'a' && *name <= 'z')
                          || *name == '_')) {
        return false;
    }
    for (char *p = name + 1; *p; p += 1) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
              || (*p >= '0' && *p <= '9') || *p == '_')) {
            return false;
        }
    }
    return true;
}

static bool
csane_known_rule(char *id) {
    char *known[] = {
        "API-NEVER", "API-CBASE-ONLY", "PP-DEFINED", "PP-SIZEOF",
        "SRC-TABS", "SRC-TRAILING", "SRC-COLUMNS", "RYU-ENDIF-COMMENT",
        "RYU-FINAL-NEWLINE", "AST-DECL-ONE",
        "AST-BRACES", "AST-SWITCH-ALIGN", "AST-ARTIFICIAL-BLOCK",
        "STYLE-ASSERT-REL", "STYLE-ASSERT-BOUNDS", "STYLE-STRLIT-LEN",
        "STYLE-STREQUAL", "STYLE-PRINTF", "STYLE-STATUS-TEMP",
        "FMT-MULTIPLY", "FMT-BINARY-SPACING", "FMT-ASSIGN-SPACING",
        "FMT-ASSIGN-EQ", "FMT-CALL-FIRST", "FMT-FUNC-ALIGN",
        "FMT-LITERAL-ALIGN", "FMT-ARG-ALIGN", "FMT-FORMAT-LINE",
        "FMT-RELATED-ARGS", "FMT-CALL-WRAP", "FMT-ASSIGN-WRAP",
        "FMT-COMMA-SPACING", "FMT-MEMBER-SPACING",
        "FMT-UNARY-SPACING", "FMT-CAST-SPACING",
        "FMT-POINTER-SPACING", "FMT-CALL-SPACING",
        "FMT-TRAILING-COMMA", "STYLE-INCDEC",
        "TYPE-CONST-SIGNATURE", "TYPE-STDINT-T",
        "TYPE-VLA", "TYPE-STRUCT-TYPEDEF",
        "TYPE-ENUM-TYPEDEF", "TYPE-UNION-TYPEDEF",
        "TYPE-POINTER-TYPEDEF", "DECL-AFTER-STMT",
    };
    for (int32 i = 0; i < (int32)(SIZEOF(known)/SIZEOF(known[0]));
         i += 1) {
        if (!strcmp(id, known[i])) {
            return true;
        }
    }
    return false;
}

/* Policy filenames are resolved relative to the config file itself, making
 * --config portable when the executable is invoked from another directory.
 */
static char *
csane_config_path(char *filename, char *path) {
    if (path[0] == '/') {
        return csane_copy(path);
    }
    int32 slash = -1;
    for (int32 i = 0; filename[i]; i += 1) {
        if (filename[i] == '/') {
            slash = i;
        }
    }
    if (slash < 0) {
        return csane_copy(path);
    }
    int32 len = strlen32(path);
    char *result = malloc2((int64)slash + len + 2);
    memcpy64(result, filename, slash + 1);
    memcpy64(result + slash + 1, path, (int64)len + 1);
    return result;
}

static bool
csane_config_line(CsanePolicy *policy, char *line, char *filename,
                  int32 lineno) {
    char *cursor = line;
    char *key = csane_word(&cursor);
    char *first;
    char *second;
    char *extra;
    int32 columns;
    bool ok = false;
    if (key == NULL || key[0] == '#') {
        return true;
    }
    first = csane_word(&cursor);
    second = csane_word(&cursor);
    extra = csane_word(&cursor);
    if (!strcmp(key, "exclude") && first && !second) {
        ok = csane_policy_add_regex(&policy->excludes, first, NULL, 0);
    } else if (!strcmp(key, "no-column-limit") && first && !second) {
        ok = csane_policy_add_regex(&policy->no_columns, first, NULL, 0);
    } else if (!strcmp(key, "column-limit") && first && !second
               && csane_number(first, &columns)) {
        policy->default_columns = columns;
        ok = true;
    } else if (!strcmp(key, "columns") && first && second && !extra
               && csane_number(second, &columns)) {
        ok = csane_policy_add_regex(&policy->column_overrides, first, NULL,
                                    columns);
    } else if (!strcmp(key, "allow-rule") && first && second && !extra
               && csane_known_rule(first)) {
        ok = csane_policy_add_regex(&policy->rule_exemptions, second, first, 0);
    } else if (!strcmp(key, "never-use") && csane_identifier(first)
               && !second) {
        ok = csane_paths_add(&policy->never_names, first);
    } else if (!strcmp(key, "cbase-only") && csane_identifier(first)
               && !second) {
        ok = csane_paths_add(&policy->cbase_names, first);
    } else if (!strcmp(key, "clear-never-use") && !first) {
        csane_paths_free(&policy->never_names);
        ok = true;
    } else if (!strcmp(key, "clear-cbase-only") && !first) {
        csane_paths_free(&policy->cbase_names);
        ok = true;
    } else if (!strcmp(key, "never-use-file") && first && !second) {
        char *path = csane_config_path(filename, first);
        ok = csane_load_names(&policy->never_names, path);
        free2(path, (int64)strlen32(path) + 1);
    } else if (!strcmp(key, "cbase-only-file") && first && !second) {
        char *path = csane_config_path(filename, first);
        ok = csane_load_names(&policy->cbase_names, path);
        free2(path, (int64)strlen32(path) + 1);
    } else if (!strcmp(key, "cbase-root") && first && !second) {
        char *path = csane_config_path(filename, first);
        ok = csane_policy_add_root(policy, path);
        free2(path, (int64)strlen32(path) + 1);
    }
    if (!ok) {
        fprintf(stderr, "%s:%d: invalid csanecheck policy directive: %s\n",
                        filename, lineno, key);
    }
    return ok;
}

bool
csane_policy_config(CsanePolicy *policy, char *filename) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    if (len < 0) {
        fprintf(stderr, "%s: unable to read csanecheck policy\n", filename);
        return false;
    }
    bool ok = true;
    int32 start = 0;
    int32 lineno = 1;
    for (int32 i = 0; i <= len; i += 1) {
        if (i < len && text[i] != '\n') {
            continue;
        }
        text[i] = '\0';
        if (!csane_config_line(policy, text + start, filename, lineno)) {
            ok = false;
        }
        start = i + 1;
        lineno += 1;
    }
    free2(text, (int64)len + 1);
    return ok;
}

static bool
csane_ryu_filter(char *filter) {
    if (filter == NULL) {
        return false;
    }
    int32 len = strlen32(filter);
    if (len >= 3 && !strcmp(filter + len - 3, "ryu")) {
        return len == 3 || filter[len - 4] == '/';
    }
    if (len >= 5 && !strcmp(filter + len - 5, "ryu.c")) {
        return len == 5 || filter[len - 6] == '/';
    }
    return false;
}

bool
csane_policy_filter(char *path, char *filter) {
    if (filter == NULL || filter[0] == '\0') {
        return true;
    }
    char *basename = path;
    char *filter_base = filter;
    for (char *p = path; *p; p += 1) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    for (char *p = filter; *p; p += 1) {
        if (*p == '/') {
            filter_base = p + 1;
        }
    }
    if (!strcmp(path, filter) || !strcmp(basename, filter_base)) {
        return true;
    }
    int32 n = strlen32(basename);
    int32 m = strlen32(filter_base);
    if (n >= 2 && (!strcmp(basename + n - 2, ".h")
                   || !strcmp(basename + n - 2, ".H")
                   || !strcmp(basename + n - 2, ".C"))) {
        return true;
    }
    if (n >= 2 && !strcmp(basename + n - 2, ".c")) {
        n -= 2;
    }
    if (m >= 2 && !strcmp(filter_base + m - 2, ".c")) {
        m -= 2;
    }
    if (n == m && !memcmp64(basename, filter_base, n)) {
        return true;
    }
    if (csane_ryu_filter(filter) && n >= 2
        && !strcmp(basename + n, ".c")) {
        char *ryu = NULL;
        if (!memcmp64(path, "ryu/", 4)) {
            ryu = path + 4;
        } else {
            for (char *p = path; *p; p += 1) {
                if (p[0] == '/' && p[1] == 'r' && p[2] == 'y'
                    && p[3] == 'u' && p[4] == '/') {
                    ryu = p + 5;
                    break;
                }
            }
        }
        if (ryu != NULL) {
            for (char *p = ryu; *p; p += 1) {
                if (*p == '/') {
                    return false;
                }
            }
            return true;
        }
    }
    /* A selected .c module keeps all headers, as the legacy shell does. */
    return false;
}

/* Resolve the containing directory, not the source file: file symlinks
 * retain their selected name (for example src/alias.c). Also normalize
 * project-relative paths supplied outside the project working directory.
 */
static char *
csane_policy_selection_path(CsanePolicy *policy, char *path,
                            char *resolved) {
    char directory[PATH_MAX];
    char *basename = path;
    for (char *p = path; *p; p += 1) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    int32 longest = -1;
    char *selected = path;
    for (int32 i = 0; i < policy->project_roots.count; i += 1) {
        char *root = policy->project_roots.items[i];
        int32 length = strlen32(root);
        if (length > longest && csane_path_prefix(path, root)
            && path[length] == '/') {
            selected = path + length + 1;
            longest = length;
        }
    }
    if (longest >= 0) {
        return selected;
    }
    int32 size = (int32)(basename - path);
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
    int32 dir_len = strlen32(resolved);
    int32 base_len = strlen32(basename);
    if (dir_len + base_len + 2 >= PATH_MAX) {
        return path;
    }
    resolved[dir_len] = '/';
    memcpy64(resolved + dir_len + 1, basename, (int64)base_len + 1);
    for (int32 i = 0; i < policy->project_roots.count; i += 1) {
        char *root = policy->project_roots.items[i];
        int32 length = strlen32(root);
        if (length > longest && csane_path_prefix(resolved, root)
            && resolved[length] == '/') {
            selected = resolved + length + 1;
            longest = length;
        }
    }
    return selected;
}

bool
csane_policy_selected(CsanePolicy *policy, char *path, char *filter) {
    char resolved[PATH_MAX];
    char *source = csane_policy_selection_path(policy, path, resolved);
    if (!csane_source_allowed(source)
        || csane_policy_match(&policy->excludes, source)) {
        return false;
    }
    return csane_policy_filter(source, filter);
}

#endif /* CSANECHECK_CONFIG_C */
