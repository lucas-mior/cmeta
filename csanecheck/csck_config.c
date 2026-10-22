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
        fprintf(stderr, "invalid regex '%.*s': %s\n", text_len, text,
                description);
        return false;
    }
#else
    fprintf(stderr, "POSIX regular expressions are unavailable: %.*s\n",
            text_len, text);
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
            error2("invalid regex '%.*s': %s\n", function_len, function,
                   description);
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
        if (item->has_function) {
            regfree(&item->function_compiled);
        }
#endif
        if (item->rule_id != NULL) {
            free2(item->rule_id, (int64)item->rule_id_len + 1);
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
csane_policy_rule_allowed(CsanePolicy *policy, char *path, char *rule_id,
                          Token *function) {
    for (int32 i = 0; i < policy->rule_exemptions.count; i += 1) {
        CsanePattern *pattern = &policy->rule_exemptions.items[i];
        if (STREQUAL(pattern->rule_id, pattern->rule_id_len,
                     rule_id, strlen32(rule_id))
            && csane_match(pattern, path)) {
            if (!pattern->has_function) {
                return true;
            }
            if (function != NULL) {
                char *name = malloc2((int64)function->len + 1);
                bool matches;

                memcpy64(name, function->text, function->len);
                name[function->len] = '\0';
#if OS_UNIX
                matches = regexec(&pattern->function_compiled, name,
                                  0, NULL, 0) == 0;
#else
                matches = false;
#endif
                free2(name, (int64)function->len + 1);
                if (matches) {
                    return true;
                }
            }
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
csane_path_prefix(char *path, int32 path_len, char *prefix,
                  int32 prefix_len) {
    if (!BEGINS_WITH(path, path_len, prefix, prefix_len)) {
        return false;
    }
    return path_len == prefix_len || path[prefix_len] == '/';
}

bool
csane_policy_add_root(CsanePolicy *policy, char *path) {
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        fprintf(stderr, "%s: cannot resolve cbase directory\n", path);
        return false;
    }
    return csane_paths_add(&policy->cbase_roots, resolved, strlen32(resolved));
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
    for (int32 i = 0; i < strflex_list_len(&policy->cbase_roots.strings); i += 1) {
        if (csane_path_prefix(resolved, strlen32(resolved),
                              policy->cbase_roots.strings.items[i]->data,
                              policy->cbase_roots.strings.items[i]->len)) {
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
    for (int32 i = 0; i < strflex_list_len(&policy->cbase_roots.strings); i += 1) {
        char *root = policy->cbase_roots.strings.items[i]->data;
        int32 length = policy->cbase_roots.strings.items[i]->len;
        int32 resolved_len = strlen32(resolved);
        if (resolved_len <= length + 5
            || !BEGINS_WITH(resolved, resolved_len, root, length)
            || !BEGINS_WITH(resolved + length, resolved_len - length, "/ryu/")) {
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
csane_join_path(char *root, int32 a, char *name, int32 b,
                int32 *path_len) {
    char *path = malloc2((int64)a + b + 2);
    memcpy64(path, root, a);
    path[a] = '/';
    memcpy64(path + a + 1, name, (int64)b + 1);
    *path_len = a + b + 1;
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
    bool ok = true;
    int32 start = 0;

    if (len < 0) {
        fprintf(stderr, "%s: unable to read policy names\n", path);
        return false;
    }
    csane_paths_free(names);
    for (int32 i = 0; i <= len; i += 1) {
        char *word;

        if (i < len && text[i] != '\n') {
            continue;
        }
        int32 begin = start;
        int32 end = i;
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
    free2(text, (int64)len + 1);
    return ok;
}

/* The checked-in lists take priority over compiled fallback names. This also
 * supports projects using cbase as a standalone source directory.
 */
bool
csane_policy_project(CsanePolicy *policy, char *project) {
    char resolved[PATH_MAX];
    bool ok = true;
    char *cbase;
    char *root;
    char *names;
    int32 cbase_len;
    int32 names_len;
    int32 project_len = strlen32(project);
    int32 root_len;

    if (realpath(project, resolved) == NULL
        || !csane_paths_add(&policy->project_roots, resolved, strlen32(resolved))) {
        fprintf(stderr, "%s: cannot resolve project directory\n", project);
        return false;
    }
    cbase = csane_join_path(project, project_len, STRLIT("cbase"),
                            &cbase_len);
    root = cbase;
    root_len = cbase_len;
    names = csane_join_path(root, root_len, STRLIT("functions_never_use.txt"),
                            &names_len);
    if (!csane_exists(names)) {
        free2(names, (int64)names_len + 1);
        root = project;
        root_len = project_len;
        names = csane_join_path(root, root_len, STRLIT("functions_never_use.txt"),
                            &names_len);
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
    free2(names, (int64)names_len + 1);
    names = csane_join_path(root, root_len, STRLIT("functions_allowed_cbase_only.txt"),
                            &names_len);
    if (csane_exists(names)) {
        ok = csane_load_names(&policy->cbase_names, names) && ok;
    }
    free2(names, (int64)names_len + 1);
    names = csane_join_path(project, project_len, STRLIT("piscou.1"),
                            &names_len);
    if (csane_exists(names)) {
        int32 config_len;
        char *config = csane_join_path(project, project_len, STRLIT("config.h"),
                                       &config_len);
        if (csane_exists(config)) {
            ok = csane_policy_add_regex(&policy->no_columns,
                                        STRLIT("(^|/)config\\.h$"), NULL, 0, NULL, 0, 0)
                 && ok;
        }
        free2(config, (int64)config_len + 1);
    }
    free2(names, (int64)names_len + 1);
    names = csane_join_path(root, root_len, STRLIT("csanecheck.conf"),
                            &names_len);
    if (csane_exists(names)) {
        char resolved_config[PATH_MAX];
        bool loaded = false;

        if (realpath(names, resolved_config) == NULL) {
            ok = false;
        } else {
            for (int32 i = 0; i < strflex_list_len(&policy->configs.strings); i += 1) {
                if (STREQUAL(policy->configs.strings.items[i]->data,
                             policy->configs.strings.items[i]->len,
                             resolved_config, strlen32(resolved_config))) {
                    loaded = true;
                    break;
                }
            }
            if (!loaded) {
                loaded = csane_paths_add(&policy->configs, resolved_config,
                                         strlen32(resolved_config));
                ok = loaded && ok;
                if (loaded) {
                    ok = csane_policy_config(policy, resolved_config) && ok;
                }
            }
        }
    }
    free2(names, (int64)names_len + 1);
    free2(cbase, (int64)cbase_len + 1);
    return ok;
}

bool
csane_policy_source(CsanePolicy *policy, char *source) {
    char directory[PATH_MAX];
    char resolved[PATH_MAX];
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
    if (realpath(directory, resolved) == NULL) {
        return true;
    }
    for (;;) {
        char *names;
        char *cbase;
        int32 names_len;
        int32 cbase_len;
        int32 resolved_len = strlen32(resolved);
        bool found;

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
                bool added;

                leaf[-1] = '\0';
                added = csane_paths_add(&policy->project_roots, resolved, strlen32(resolved));
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
        found = csane_exists(names) || csane_exists(cbase);
        free2(names, (int64)names_len + 1);
        free2(cbase, (int64)cbase_len + 1);
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
        if (!csane_paths_add(&policy->never_names, csane_default_never[i],
                             strlen32(csane_default_never[i]))) {
            csane_policy_free(policy);
            return false;
        }
    }
    for (int32 i = 0; i < (int32)(SIZEOF(csane_default_cbase)
                                /SIZEOF(csane_default_cbase[0])); i += 1) {
        if (!csane_paths_add(&policy->cbase_names, csane_default_cbase[i],
                             strlen32(csane_default_cbase[i]))) {
            csane_policy_free(policy);
            return false;
        }
    }
    if (!csane_policy_add_regex(&policy->column_overrides,
                                STRLIT("(^|/)cecup/src/"), NULL, 0, NULL, 0, 100)) {
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
    csane_paths_free(&policy->configs);
    csane_patterns_free(&policy->excludes);
    csane_patterns_free(&policy->no_columns);
    csane_patterns_free(&policy->column_overrides);
    csane_patterns_free(&policy->rule_exemptions);
    *policy = (CsanePolicy){0};
    return;
}

typedef struct CsaneWord {
    char *data;
    int32 len;
} CsaneWord;

static CsaneWord
csane_word(char **cursor, int32 *remaining) {
    CsaneWord result = {0};
    int32 skip = 0;
    int32 count = 0;

    while (skip < *remaining && ((*cursor)[skip] == ' '
           || (*cursor)[skip] == '\t' || (*cursor)[skip] == '\r')) {
        skip += 1;
    }
    *cursor += skip;
    *remaining -= skip;
    while (count < *remaining && (*cursor)[count] != ' '
           && (*cursor)[count] != '\t' && (*cursor)[count] != '\r'
           && (*cursor)[count] != '\0') {
        count += 1;
    }
    if (count == 0) {
        return result;
    }
    result.data = *cursor;
    result.len = count;
    if (count < *remaining) {
        (*cursor)[count] = '\0';
        *cursor += count + 1;
        *remaining -= count + 1;
    } else {
        *cursor += count;
        *remaining = 0;
    }
    return result;
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

static bool
csane_known_rule(char *id, int32 id_len) {
    static struct {
        char *data;
        int32 len;
    } known[] = {
#define CSANE_RULE(NAME) {STRLIT(NAME)}
        CSANE_RULE("API-NEVER"),
        CSANE_RULE("API-CBASE-ONLY"),
        CSANE_RULE("PP-DEFINED"),
        CSANE_RULE("PP-SIZEOF"),
        CSANE_RULE("SRC-TABS"),
        CSANE_RULE("SRC-TRAILING"),
        CSANE_RULE("SRC-COLUMNS"),
        CSANE_RULE("RYU-ENDIF-COMMENT"),
        CSANE_RULE("RYU-FINAL-NEWLINE"),
        CSANE_RULE("AST-DECL-ONE"),
        CSANE_RULE("AST-BRACES"),
        CSANE_RULE("AST-SWITCH-ALIGN"),
        CSANE_RULE("AST-ARTIFICIAL-BLOCK"),
        CSANE_RULE("STYLE-ASSERT-REL"),
        CSANE_RULE("STYLE-ASSERT-BOUNDS"),
        CSANE_RULE("STYLE-STRLIT-LEN"),
        CSANE_RULE("STYLE-STREQUAL"),
        CSANE_RULE("STYLE-PRINTF"),
        CSANE_RULE("STYLE-STATUS-TEMP"),
        CSANE_RULE("FMT-MULTIPLY"),
        CSANE_RULE("FMT-BINARY-SPACING"),
        CSANE_RULE("FMT-ASSIGN-SPACING"),
        CSANE_RULE("FMT-ASSIGN-EQ"),
        CSANE_RULE("FMT-CALL-FIRST"),
        CSANE_RULE("FMT-FUNC-ALIGN"),
        CSANE_RULE("FMT-LITERAL-ALIGN"),
        CSANE_RULE("FMT-ARG-ALIGN"),
        CSANE_RULE("FMT-FORMAT-LINE"),
        CSANE_RULE("FMT-RELATED-ARGS"),
        CSANE_RULE("FMT-CALL-WRAP"),
        CSANE_RULE("FMT-ASSIGN-WRAP"),
        CSANE_RULE("FMT-COMMA-SPACING"),
        CSANE_RULE("FMT-MEMBER-SPACING"),
        CSANE_RULE("FMT-UNARY-SPACING"),
        CSANE_RULE("FMT-CAST-SPACING"),
        CSANE_RULE("FMT-POINTER-SPACING"),
        CSANE_RULE("FMT-CALL-SPACING"),
        CSANE_RULE("FMT-TRAILING-COMMA"),
        CSANE_RULE("STYLE-INCDEC"),
        CSANE_RULE("TYPE-CONST-SIGNATURE"),
        CSANE_RULE("TYPE-STDINT-T"),
        CSANE_RULE("TYPE-VLA"),
        CSANE_RULE("TYPE-ENUM-TYPEDEF"),
        CSANE_RULE("TYPE-UNION-TYPEDEF"),
        CSANE_RULE("TYPE-POINTER-TYPEDEF"),
#undef CSANE_RULE
    };
    for (int32 i = 0; i < (int32)(SIZEOF(known)/SIZEOF(known[0]));
         i += 1) {
        if (STREQUAL(id, id_len, known[i].data, known[i].len)) {
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
    int32 slash = -1;
    int32 len;
    char *result;

    if (path[0] == '/') {
        return xstrndup(path, strlen32(path));
    }
    for (int32 i = 0; filename[i]; i += 1) {
        if (filename[i] == '/') {
            slash = i;
        }
    }
    if (slash < 0) {
        return xstrndup(path, strlen32(path));
    }
    len = strlen32(path);
    result = malloc2((int64)slash + len + 2);
    memcpy64(result, filename, slash + 1);
    memcpy64(result + slash + 1, path, (int64)len + 1);
    return result;
}

static bool
csane_config_line(CsanePolicy *policy, char *line, int32 line_len,
                  char *filename, int32 lineno) {
    char *cursor = line;
    int32 remaining = line_len;
    CsaneWord key = csane_word(&cursor, &remaining);
    CsaneWord first;
    CsaneWord second;
    CsaneWord extra;
    CsaneWord fourth;
    int32 columns;
    bool ok = false;
    if (key.data == NULL || key.data[0] == '#') {
        return true;
    }
    first = csane_word(&cursor, &remaining);
    second = csane_word(&cursor, &remaining);
    extra = csane_word(&cursor, &remaining);
    fourth = csane_word(&cursor, &remaining);
    if (STREQUAL(key.data, key.len, "exclude")
        && first.data && !second.data) {
        ok = csane_policy_add_regex(&policy->excludes, first.data, first.len,
                                    NULL, 0, NULL, 0, 0);
    } else if (STREQUAL(key.data, key.len, "no-column-limit")
        && first.data && !second.data) {
        ok = csane_policy_add_regex(&policy->no_columns, first.data, first.len,
                                    NULL, 0, NULL, 0, 0);
    } else if (STREQUAL(key.data, key.len, "column-limit") && first.data && !second.data
               && csane_number(first.data, &columns)) {
        policy->default_columns = columns;
        ok = true;
    } else if (STREQUAL(key.data, key.len, "columns")
               && first.data && second.data && !extra.data
               && csane_number(second.data, &columns)) {
        ok = csane_policy_add_regex(&policy->column_overrides, first.data, first.len,
                                    NULL, 0, NULL, 0, columns);
    } else if (STREQUAL(key.data, key.len, "allow-rule")
               && first.data && second.data && !extra.data
              
               && csane_known_rule(first.data, first.len)) {
        ok = csane_policy_add_regex(&policy->rule_exemptions, second.data,
                                    second.len, first.data, first.len,
                                    NULL, 0, 0);
    } else if (STREQUAL(key.data, key.len, "allow-function-rule")
               && first.data && second.data
              
               && extra.data && !fourth.data
              
               && csane_known_rule(first.data, first.len)) {
        ok = csane_policy_add_regex(&policy->rule_exemptions, second.data,
                                    second.len, first.data, first.len,
                                    extra.data, extra.len, 0);
    } else if (STREQUAL(key.data, key.len, "never-use")
               && csane_identifier(first.data, first.len)
               && !second.data) {
        ok = csane_paths_add(&policy->never_names, first.data, first.len);
    } else if (STREQUAL(key.data, key.len, "cbase-only")
               && csane_identifier(first.data, first.len)
               && !second.data) {
        ok = csane_paths_add(&policy->cbase_names, first.data, first.len);
    } else if (STREQUAL(key.data, key.len, "clear-never-use") && !first.data) {
        csane_paths_free(&policy->never_names);
        ok = true;
    } else if (STREQUAL(key.data, key.len, "clear-cbase-only") && !first.data) {
        csane_paths_free(&policy->cbase_names);
        ok = true;
    } else if (STREQUAL(key.data, key.len, "never-use-file")
        && first.data && !second.data) {
        char *path = csane_config_path(filename, first.data);
        ok = csane_load_names(&policy->never_names, path);
        free2(path, (int64)strlen32(path) + 1);
    } else if (STREQUAL(key.data, key.len, "cbase-only-file")
        && first.data && !second.data) {
        char *path = csane_config_path(filename, first.data);
        ok = csane_load_names(&policy->cbase_names, path);
        free2(path, (int64)strlen32(path) + 1);
    } else if (STREQUAL(key.data, key.len, "cbase-root")
        && first.data && !second.data) {
        char *path = csane_config_path(filename, first.data);
        ok = csane_policy_add_root(policy, path);
        free2(path, (int64)strlen32(path) + 1);
    }
    if (!ok) {
        fprintf(stderr, "%s:%d: invalid csanecheck policy directive: %s\n",
                        filename, lineno, key.data);
    }
    return ok;
}

bool
csane_policy_config(CsanePolicy *policy, char *filename) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    bool ok = true;
    int32 start = 0;
    int32 lineno = 1;

    if (len < 0) {
        fprintf(stderr, "%s: unable to read csanecheck policy\n", filename);
        return false;
    }
    for (int32 i = 0; i <= len; i += 1) {
        if (i < len && text[i] != '\n') {
            continue;
        }
        text[i] = '\0';
        if (!csane_config_line(policy, text + start, i - start, filename, lineno)) {
            ok = false;
        }
        start = i + 1;
        lineno += 1;
    }
    free2(text, (int64)len + 1);
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
csane_policy_filter(char *path, char *filter) {
    char *basename = path;
    char *filter_base = filter;
    int32 path_len;
    int32 filter_len;
    int32 n;
    int32 m;

    if (filter == NULL || filter[0] == '\0') {
        return true;
    }
    path_len = strlen32(path);
    filter_len = strlen32(filter);
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
                if (path[i] == '/' && BEGINS_WITH(path + i + 1,
                                                   path_len - i - 1, "ryu/")) {
                    ryu = path + i + 5;
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
    int32 longest = -1;
    char *selected = path;
    int32 size;
    int32 dir_len;
    int32 base_len;

    for (char *p = path; *p; p += 1) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    for (int32 i = 0; i < strflex_list_len(&policy->project_roots.strings); i += 1) {
        char *root = policy->project_roots.strings.items[i]->data;
        int32 length = policy->project_roots.strings.items[i]->len;
        if (length > longest && csane_path_prefix(path, strlen32(path), root, length)
            && path[length] == '/') {
            selected = path + length + 1;
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
    dir_len = strlen32(resolved);
    base_len = strlen32(basename);
    if (dir_len + base_len + 2 >= PATH_MAX) {
        return path;
    }
    resolved[dir_len] = '/';
    memcpy64(resolved + dir_len + 1, basename, (int64)base_len + 1);
    for (int32 i = 0; i < strflex_list_len(&policy->project_roots.strings); i += 1) {
        char *root = policy->project_roots.strings.items[i]->data;
        int32 length = policy->project_roots.strings.items[i]->len;
        if (length > longest && csane_path_prefix(resolved,
                                                   dir_len + base_len + 1, root, length)
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
    if (!csane_source_allowed(source, strlen32(source))
        || csane_policy_match(&policy->excludes, source)) {
        return false;
    }
    return csane_policy_filter(source, filter);
}

#endif /* CSANECHECK_CONFIG_C */
