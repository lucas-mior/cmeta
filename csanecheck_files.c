// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_FILES_C)
#define CSANECHECK_FILES_C

#include "cbase.h"
#include "csanecheck.h"

static char *
csane_strdup(char *string) {
    int32 len = strlen32(string);
    char *copy = malloc2((int64)len + 1);
    memcpy64(copy, string, (int64)len + 1);
    return copy;
}

static char *
csane_join(char *directory, char *name) {
    int32 left = strlen32(directory);
    int32 right = strlen32(name);
    bool slash = (left > 0) && (directory[left - 1] != '/');
    char *path = malloc2((int64)left + right + slash + 1);
    memcpy64(path, directory, left);
    if (slash) {
        path[left] = '/';
    }
    memcpy64(path + left + slash, name, (int64)right + 1);
    return path;
}

static void
csane_path_free(char *path) {
    free2(path, (int64)strlen32(path) + 1);
    return;
}

bool
csane_paths_add(CsanePaths *paths, char *path) {
    if ((paths == NULL) || (path == NULL) || (path[0] == '\0')) {
        return false;
    }
    if (paths->count == INT32_MAX) {
        return false;
    }
    if (paths->count == paths->capacity) {
        int32 capacity = paths->capacity;
        int32 next;
        if (capacity > INT32_MAX/2) {
            return false;
        }
        next = (capacity == 0) ? 32 : capacity*2;
        if (paths->items == NULL) {
            paths->items = malloc2((int64)next*SIZEOF(*paths->items));
        } else {
            paths->items = realloc2(paths->items, capacity, next,
                                    SIZEOF(*paths->items));
        }
        paths->capacity = next;
    }
    paths->items[paths->count] = csane_strdup(path);
    paths->count += 1;
    return true;
}

static int
csane_path_compare(const void *a, const void *b) {
    char *left = *(char **)a;
    char *right = *(char **)b;
    return strcmp(left, right);
}

void
csane_paths_sort_unique(CsanePaths *paths) {
    int32 kept = 0;
    qsort(paths->items, (size_t)paths->count,
          (size_t)SIZEOF(*paths->items), csane_path_compare);
    for (int32 i = 0; i < paths->count; i += 1) {
        if ((kept > 0) && !strcmp(paths->items[kept - 1], paths->items[i])) {
            csane_path_free(paths->items[i]);
            continue;
        }
        paths->items[kept] = paths->items[i];
        kept += 1;
    }
    paths->count = kept;
    return;
}

void
csane_paths_free(CsanePaths *paths) {
    for (int32 i = 0; i < paths->count; i += 1) {
        csane_path_free(paths->items[i]);
    }
    if (paths->items != NULL) {
        free2(paths->items, (int64)paths->capacity*SIZEOF(*paths->items));
    }
    *paths = (CsanePaths){0};
    return;
}

static bool
csane_extension(char *path) {
    int32 len = strlen32(path);
    if (len >= 7 && !strcmp(path + len - 7, ".meta.h")) {
        return false;
    }
    if (len < 3) {
        return false;
    }
    return !strcmp(path + len - 2, ".c")
           || !strcmp(path + len - 2, ".h");
}

static bool
csane_skip_directory(char *name) {
    return !strcmp(name, "stc") || !strcmp(name, ".git")
           || !strcmp(name, "bin") || !strcmp(name, ".test-tmp");
}

/* Depth zero means only the directory's direct files. Directory symlinks
 * are not followed: that avoids cycles and repeated files under aliases.
 */
static bool
csane_walk(CsanePaths *paths, char *directory, int32 depth,
           int32 max_depth) {
    DirEntry *entries = NULL;
    int32 count = get_directory_entries(directory, &entries);
    if (count < 0) {
        fprintf(stderr, "%s: cannot read directory: %s\n",
                        directory, strerror(errno));
        return false;
    }
    bool ok = true;
    for (int32 i = 0; i < count; i += 1) {
        char *name = entries[i].name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            continue;
        }
        char *path = csane_join(directory, name);
        struct stat st;
#if OS_WINDOWS
        if (stat(path, &st) != 0) {
#else
        if (lstat(path, &st) != 0) {
#endif
            fprintf(stderr, "%s: cannot stat: %s\n", path, strerror(errno));
            ok = false;
        } else if (S_ISDIR(st.st_mode)) {
            if (!csane_skip_directory(name)
                && ((max_depth < 0) || (depth < max_depth))
                && !csane_walk(paths, path, depth + 1, max_depth)) {
                ok = false;
            }
        } else if ((S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
                   && csane_extension(name) && !csane_paths_add(paths, path)) {
            ok = false;
        }
        csane_path_free(path);
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
    return csane_paths_add(paths, path);
}

bool
csane_collect_root(CsanePaths *paths, char *root, int32 max_depth) {
    char *subdirs[] = {"cbase", "src", "test", "tests", "examples",
                       "cyblocks", "tools"};
    struct stat st;
    bool ok = true;
    if ((stat(root, &st) != 0) || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: not a directory\n", root);
        return false;
    }
    /* Ordinary files directly in the project root. */
    if (!csane_walk(paths, root, 0, 0)) {
        ok = false;
    }
    for (int32 i = 0; i < (int32)(SIZEOF(subdirs)/SIZEOF(subdirs[0]));
         i += 1) {
        char *path = csane_join(root, subdirs[i]);
        if ((stat(path, &st) == 0) && S_ISDIR(st.st_mode)) {
            if (!csane_walk(paths, path, 0, max_depth - (max_depth > 0))) {
                ok = false;
            }
        }
        csane_path_free(path);
    }
    /* Ryu's vendored sources are checked at all nesting depths. */
    char *ryu = csane_join(root, "cbase/ryu");
    if ((stat(ryu, &st) == 0) && S_ISDIR(st.st_mode)) {
        if (!csane_walk(paths, ryu, 0, -1)) {
            ok = false;
        }
    }
    csane_path_free(ryu);
    return ok;
}

bool
csane_collect_file_list(CsanePaths *paths, char *filename) {
    char *text = NULL;
    int32 len = read_entire_file(filename, &text);
    if (len < 0) {
        return false;
    }
    bool ok = true;
    int32 start = 0;
    for (int32 i = 0; i <= len; i += 1) {
        if ((i < len) && (text[i] != '\n')) {
            continue;
        }
        int32 end = i;
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
