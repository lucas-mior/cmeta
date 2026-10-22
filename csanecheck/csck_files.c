// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_FILES_C)
#define CSANECHECK_FILES_C

#include "cbase.h"
#include "csck.h"

static char *
csane_join(char *directory, char *name, int32 *path_len) {
    int32 left = strlen32(directory);
    int32 right = strlen32(name);
    bool slash = (left > 0) && (directory[left - 1] != '/');
    char *path = malloc2((int64)left + right + slash + 1);
    memcpy64(path, directory, left);
    if (slash) {
        path[left] = '/';
    }
    memcpy64(path + left + slash, name, (int64)right + 1);
    *path_len = left + slash + right;
    return path;
}

bool
csane_paths_add(CsanePaths *paths, char *path, int32 path_len) {
    if (paths == NULL || path == NULL || path_len <= 0
        || strflex_list_len(&paths->strings) == INT32_MAX) {
        return false;
    }
    strflex_list_push(&paths->strings, path, path_len);
    return true;
}

static int
csane_path_compare(void *a, void *b) {
    StrFlex *left = *(StrFlex **)a;
    StrFlex *right = *(StrFlex **)b;
    return strcmp(left->data, right->data);
}

void
csane_paths_sort_unique(CsanePaths *paths) {
    StrFlex **items = paths->strings.items;
    int32 count = ARRAY_LEN(items);
    int32 kept = 0;

    qsort64(items, count, SIZEOF(*items), csane_path_compare);
    for (int32 i = 0; i < count; i += 1) {
        if (kept > 0 && items[kept - 1]->len == items[i]->len
            && memcmp64(items[kept - 1]->data, items[i]->data,
                        items[i]->len) == 0) {
            continue;
        }
        items[kept] = items[i];
        kept += 1;
    }
    ARRAY_SET_COUNT(paths->strings.items, kept);
    return;
}

void
csane_paths_free(CsanePaths *paths) {
    strflex_list_destroy(&paths->strings);
    return;
}

/* The same source eligibility rule applies to discovered and explicitly
 * selected files. The legacy wrapper excludes stc subtrees and generated
 * .meta.h files regardless of how a source reaches a checker.
 */
bool
csane_source_allowed(char *path) {
    char *name = path;
    int32 len;

    for (char *p = path; *p; p += 1) {
        if (*p == '/') {
            name = p + 1;
        }
    }
    len = strlen32(name);
    if ((len >= 7) && !strcmp(name + len - 7, ".meta.h")) {
        return false;
    }
    if ((len < 3) || (name[len - 2] != '.')
        || (name[len - 1] != 'c' && name[len - 1] != 'C'
            && name[len - 1] != 'h' && name[len - 1] != 'H')) {
        return false;
    }
    for (char *p = path; p < name; p += 1) {
        if ((p == path || p[-1] == '/') && p[0] == 's'
            && p[1] == 't' && p[2] == 'c' && p[3] == '/') {
            return false;
        }
    }
    return true;
}

static bool
csane_path_has(CsanePaths *paths, char *name) {
    int32 name_len = strlen32(name);
    for (int32 i = 0; i < strflex_list_len(&paths->strings); i += 1) {
        StrFlex *entry = paths->strings.items[i];
        if (entry->len == name_len
            && memcmp64(entry->data, name, name_len) == 0) {
            return true;
        }
    }
    return false;
}

bool
csane_discovery_defaults(CsanePaths *roots, CsanePaths *skip_dirs) {
    char *default_roots[] = {
        "cbase", "src", "test", "tests", "examples", "cyblocks",
        "tools", "csanecheck",
    };
    char *default_skips[] = {
        ".git", "bin", "build", "generated", "stc", "third_party",
        "vendor",
    };
    for (int32 i = 0; i < (int32)(SIZEOF(default_roots)
                                /SIZEOF(default_roots[0])); i += 1) {
        if (!csane_paths_add(roots, default_roots[i],
                             strlen32(default_roots[i]))) {
            return false;
        }
    }
    for (int32 i = 0; i < (int32)(SIZEOF(default_skips)
                                /SIZEOF(default_skips[0])); i += 1) {
        if (!csane_paths_add(skip_dirs, default_skips[i],
                             strlen32(default_skips[i]))) {
            return false;
        }
    }
    return true;
}

/* Explicit directory roots are handled by csane_collect_root. Its
 * discovered subdirectories must not follow symlinks (like find without -L).
 */
static bool
csane_discovered_directory(char *path) {
    struct stat st;
#if OS_WINDOWS
    if (stat(path, &st) != 0) {
#else
    if (lstat(path, &st) != 0) {
#endif
        return false;
    }
    return S_ISDIR(st.st_mode);
}

/* Depth zero means only the directory's direct files. Directory symlinks
 * are not followed: that avoids cycles and repeated files under aliases.
 */
static bool
csane_walk(CsanePaths *paths, char *directory, int32 depth,
           int32 max_depth, CsanePaths *skip_dirs) {
    DirEntry *entries = NULL;
    int32 count = get_directory_entries(directory, &entries);
    bool ok = true;

    if (count < 0) {
        fprintf(stderr, "%s: cannot read directory: %s\n",
                        directory, strerror(errno));
        return false;
    }
    for (int32 i = 0; i < count; i += 1) {
        char *name = entries[i].name;
        char *path;
        int32 path_len;
        struct stat st;

        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            continue;
        }
        path = csane_join(directory, name, &path_len);
#if OS_WINDOWS
        if (stat(path, &st) != 0) {
#else
        if (lstat(path, &st) != 0) {
#endif
            fprintf(stderr, "%s: cannot stat: %s\n", path, strerror(errno));
            ok = false;
        } else if (S_ISDIR(st.st_mode)) {
            if (!csane_path_has(skip_dirs, name)
                && ((max_depth < 0) || (depth < max_depth))
                && !csane_walk(paths, path, depth + 1, max_depth,
                               skip_dirs)) {
                ok = false;
            }
        } else if ((S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
                   && csane_source_allowed(name)
                   && !csane_paths_add(paths, path, path_len)) {
            ok = false;
        }
        free2(path, (int64)path_len + 1);
    }
    free2(entries, (int64)count*SIZEOF(*entries));
    return ok;
}

bool
csane_collect_explicit(CsanePaths *paths, char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "%s: cannot stat: %s\n", path, strerror(errno));
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s: not a regular file\n", path);
        return false;
    }
    return csane_paths_add(paths, path, strlen32(path));
}

bool
csane_collect_root(CsanePaths *paths, char *root, int32 max_depth,
                   CsanePaths *source_roots, CsanePaths *skip_dirs) {
    struct stat st;
    bool ok = true;
    char *ryu;
    char *standalone;
    int32 path_len;
    int32 ryu_len;
    int32 standalone_len;

    if ((stat(root, &st) != 0) || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: not a directory\n", root);
        return false;
    }
    /* Ordinary files directly in the project root. */
    if (!csane_walk(paths, root, 0, 0, skip_dirs)) {
        ok = false;
    }
    for (int32 i = 0; i < strflex_list_len(&source_roots->strings); i += 1) {
        char *path = csane_join(root, source_roots->strings.items[i]->data,
                                &path_len);
        if ((max_depth != 0) && csane_discovered_directory(path)) {
            if (!csane_walk(paths, path, 0, max_depth - (max_depth > 0),
                            skip_dirs)) {
                ok = false;
            }
        }
        free2(path, (int64)path_len + 1);
    }
    /* Ryu's vendored sources are checked at all nesting depths. */
    ryu = csane_join(root, "cbase/ryu", &ryu_len);
    if (csane_discovered_directory(ryu)) {
        if (!csane_walk(paths, ryu, 0, -1, skip_dirs)) {
            ok = false;
        }
    }
    free2(ryu, (int64)ryu_len + 1);
    /* The cbase repository can also be checked directly (with the policy
     * lists in its root rather than under a cbase subdirectory).
     */
    standalone = csane_join(root, "functions_never_use.txt", &standalone_len);
    if (stat(standalone, &st) == 0 && S_ISREG(st.st_mode)) {
        ryu = csane_join(root, "ryu", &ryu_len);
        if (csane_discovered_directory(ryu)
            && !csane_walk(paths, ryu, 0, -1, skip_dirs)) {
            ok = false;
        }
        free2(ryu, (int64)ryu_len + 1);
    }
    free2(standalone, (int64)standalone_len + 1);
    return ok;
}

bool
csane_collect_file_list(CsanePaths *paths, char *filename) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    bool ok = true;
    int32 start = 0;

    if (len < 0) {
        return false;
    }
    for (int32 i = 0; i <= len; i += 1) {
        int32 end;

        if ((i < len) && (text[i] != '\n')) {
            continue;
        }
        end = i;
        if ((end > start) && (text[end - 1] == '\r')) {
            end -= 1;
        }
        text[end] = '\0';
        if ((end > start) && !csane_collect_explicit(paths, text + start)) {
            ok = false;
        }
        start = i + 1;
    }
    free2(text, (int64)len + 1);
    return ok;
}

#endif /* CSANECHECK_FILES_C */
