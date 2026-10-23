// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSCK_CACHE_C)
#define CSCK_CACHE_C

#include "cbase.h"
#include "csck.h"

#if OS_UNIX
#include <fcntl.h>
#include <unistd.h>

/* Cache entries are private to one invocation's checker and policy state.
 * A failed cache operation simply falls back to normal analysis.
 */
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

static uint64
csane_cache_hash(uint64 hash, char *data, int64 len) {
    for (int64 i = 0; i < len; i += 1) {
        hash ^= (uint8)data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64
csane_cache_stat(uint64 hash, char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return csane_cache_hash(hash, path, strlen(path));
    }
    hash = csane_cache_hash(hash, (char *)&st.st_dev, SIZEOF(st.st_dev));
    hash = csane_cache_hash(hash, (char *)&st.st_ino, SIZEOF(st.st_ino));
    hash = csane_cache_hash(hash, (char *)&st.st_size, SIZEOF(st.st_size));
    hash = csane_cache_hash(hash, (char *)&st.st_mtim, SIZEOF(st.st_mtim));
    return csane_cache_hash(hash, (char *)&st.st_ctim, SIZEOF(st.st_ctim));
}

static uint64
csane_cache_fingerprint(int32 argc, char **argv, CsanePolicy *policy) {
    uint64 hash = UINT64_C(14695981039346656037);
    for (int32 i = 0; i < argc; i += 1) {
        hash = csane_cache_hash(hash, argv[i], strlen(argv[i]) + 1);
    }
    hash = csane_cache_stat(hash, "/proc/self/exe");
    for (int32 i = 0; i < strflex_list_len(&policy->configs.strings);
         i += 1) {
        hash = csane_cache_stat(hash, policy->configs.strings.items[i]->data);
    }
    /* Imported policy lists also affect results. */
    for (int32 i = 0; i < strflex_list_len(&policy->project_roots.strings);
         i += 1) {
        char buffer[4096];
        StrFlex *root = policy->project_roots.strings.items[i];
        char *names[] = {"cbase/functions_never_use.txt",
                         "cbase/functions_allowed_cbase_only.txt",
                         "functions_never_use.txt",
                         "functions_allowed_cbase_only.txt",
                         "cbase/csanecheck.conf", "csanecheck.conf"};
        for (int32 j = 0; j < 6; j += 1) {
            int32 n = snprintf(buffer, SIZEOF(buffer), "%.*s/%s",
                               root->len, root->data, names[j]);
            if (n > 0 && n < SIZEOF(buffer)) {
                hash = csane_cache_stat(hash, buffer);
            }
        }
    }
    return hash;
}

static bool
csane_cache_signature(char *path, CsaneCacheRecord *record) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    record->source_size = (uint64)st.st_size;
    record->source_mtime = (uint64)st.st_mtim.tv_sec*UINT64_C(1000000000)
                           + (uint64)st.st_mtim.tv_nsec;
    record->source_ctime = (uint64)st.st_ctim.tv_sec*UINT64_C(1000000000)
                           + (uint64)st.st_ctim.tv_nsec;
    return true;
}

static bool
csane_cache_read(char *name, CsaneCacheRecord *expected,
                 CsaneStats *stats, int32 *status) {
    CsaneCacheRecord saved;
    FILE *file = fopen(name, "rb");
    if (file == NULL) {
        return false;
    }
    bool valid = fread(&saved, SIZEOF(saved), 1, file) == 1
                 && saved.magic == expected->magic
                 && saved.fingerprint == expected->fingerprint
                 && saved.source_size == expected->source_size
                 && saved.source_mtime == expected->source_mtime
                 && saved.source_ctime == expected->source_ctime
                 && saved.output_len <= 16*1024*1024;
    char *output = NULL;
    if (valid) {
        output = malloc(saved.output_len + 1);
        valid = output != NULL
                && fread(output, 1, saved.output_len, file)
                   == saved.output_len && fgetc(file) == EOF;
    }
    fclose(file);
    if (valid) {
        fwrite(output, 1, saved.output_len, stdout);
        *status = saved.status;
        stats->files += saved.delta.files;
        stats->tokens += saved.delta.tokens;
        stats->externals += saved.delta.externals;
        stats->structural_issues += saved.delta.structural_issues;
        stats->preprocessor_issues += saved.delta.preprocessor_issues;
        stats->violations += saved.delta.violations;
        stats->declarations += saved.delta.declarations;
        stats->function_declarations += saved.delta.function_declarations;
        stats->function_definitions += saved.delta.function_definitions;
        stats->statements += saved.delta.statements;
        stats->opaque_statements += saved.delta.opaque_statements;
        stats->opaque_externals += saved.delta.opaque_externals;
    }
    free(output);
    return valid;
}

static void
csane_cache_write(char *name, CsaneCacheRecord *record, FILE *output) {
    char temporary[256];
    int32 n = snprintf(temporary, SIZEOF(temporary), "%s.%ld.tmp",
                       name, (long)getpid());
    if (n <= 0 || n >= SIZEOF(temporary)) {
        return;
    }
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        return;
    }
    bool good = fwrite(record, SIZEOF(*record), 1, file) == 1;
    char buffer[4096];
    uint32 remaining = record->output_len;
    rewind(output);
    while (good && remaining > 0) {
        uint32 count = remaining < SIZEOF(buffer) ? remaining : SIZEOF(buffer);
        good = fread(buffer, 1, count, output) == count
               && fwrite(buffer, 1, count, file) == count;
        remaining -= count;
    }
    if (fclose(file) != 0) {
        good = false;
    }
    if (!good || rename(temporary, name) != 0) {
        unlink(temporary);
    }
}
#endif
#endif
