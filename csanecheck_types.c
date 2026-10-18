// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TYPES_C)
#define CSANECHECK_TYPES_C

#include "cbase.h"
#include "csanecheck.h"

/* The collector reads only quoted, locally resolvable headers. It does not
 * pretend to evaluate conditional compilation or include system headers.
 * Names are discovered before parsing the translation unit, so declaration
 * parsing can use actual project typedefs instead of spelling heuristics.
 */
static bool
csane_types_has(CsanePaths *paths, char *name) {
    for (int32 i = 0; i < paths->count; i += 1) {
        if (!strcmp(paths->items[i], name)) {
            return true;
        }
    }
    return false;
}

static void
csane_types_add(CsanePaths *names, Token *token) {
    if (token->kind != TOKEN_IDENT || token->len <= 0
        || token->len >= 256) {
        return;
    }
    char name[256];
    memcpy64(name, token->text, token->len);
    name[token->len] = '\0';
    if (!csane_types_has(names, name)) {
        csane_paths_add(names, name);
    }
}

static int32
csane_types_next(Tokenization *tokens, int32 after) {
    TokenRange all = {0, tokens->token_count};
    return token_range_next_significant(tokens, all, after);
}

/* Find the semicolon of an entire typedef, without treating semicolons in
 * record bodies or nested declarators as terminators. */
static int32
csane_types_end(Tokenization *tokens, int32 first,
                CPreprocessorAnalysis *pp) {
    for (int32 i = first; i < tokens->token_count;
         i = csane_types_next(tokens, i)) {
        Token *t = &tokens->tokens[i];
        if (pp->token_directives[i] >= 0) {
            continue;
        }
        if (TOKEN_IS(t, ";")) {
            return i;
        }
        if (token_is_open_delimiter(t)) {
            TokenRange range = {i, tokens->token_count};
            int32 end = token_range_matching_delimiter_forward(tokens,
                                                               range, i);
            if (end < 0) {
                return -1;
            }
            i = end;
        }
    }
    return -1;
}

/* Extract comma-separated aliases without consuming fields or arguments
 * inside nested records and declarators. These are candidate names, not
 * macro-expanded types. Array and function-pointer aliases are supported. */
static void
csane_types_alias(CsanePaths *names, Tokenization *tokens,
                  int32 first, int32 end) {
    TokenRange range = {first, end};
    int32 last = token_range_previous_significant(tokens, range, end);
    if (last < first) {
        return;
    }
    if (tokens->tokens[last].kind == TOKEN_IDENT) {
        csane_types_add(names, &tokens->tokens[last]);
        return;
    }
    if (TOKEN_IS(&tokens->tokens[last], "]")) {
        int32 open = token_range_matching_delimiter_reverse(tokens, range,
                                                            last);
        if (open >= 0) {
            int32 before = token_range_previous_significant(tokens, range,
                                                            open);
            if (before >= first) {
                csane_types_add(names, &tokens->tokens[before]);
            }
        }
        return;
    }
    for (int32 i = first; i < end; i += 1) {
        if (!TOKEN_IS(&tokens->tokens[i], "(")) {
            continue;
        }
        int32 star = csane_types_next(tokens, i);
        int32 name = csane_types_next(tokens, star);
        int32 close = csane_types_next(tokens, name);
        if (star < end && name < end && close < end
            && TOKEN_IS(&tokens->tokens[star], "*")
            && tokens->tokens[name].kind == TOKEN_IDENT
            && TOKEN_IS(&tokens->tokens[close], ")")) {
            csane_types_add(names, &tokens->tokens[name]);
        }
    }
}

static void
csane_types_typedef(CsanePaths *names, Tokenization *tokens,
                    int32 first, int32 end) {
    TokenRange all = {first, end};
    TokenDelimiterDepth depth = {0};
    int32 segment = first;
    while (segment < end) {
        int32 comma = token_range_next_text_at_depth(tokens, all,
                                                     segment - 1, depth,
                                                     ",", 1);
        csane_types_alias(names, tokens, segment, comma);
        if (comma >= end) {
            break;
        }
        segment = csane_types_next(tokens, comma);
    }
}

static bool
csane_types_resolve(char *including, char *name, CsanePolicy *policy,
                    char *resolved) {
    char candidate[PATH_MAX];
    char directory[PATH_MAX];
    int32 len = strlen32(including);
    if (len >= PATH_MAX) {
        return false;
    }
    memcpy64(directory, including, len + 1);
    int32 slash = len - 1;
    while (slash >= 0 && directory[slash] != '/') {
        slash -= 1;
    }
    if (slash >= 0) {
        directory[slash] = '\0';
    } else {
        directory[0] = '.';
        directory[1] = '\0';
    }
    if (snprintf(candidate, PATH_MAX, "%s/%s", directory, name)
        < PATH_MAX && realpath(candidate, resolved) != NULL) {
        return true;
    }
    for (int32 i = 0; i < policy->cbase_roots.count; i += 1) {
        char *root = policy->cbase_roots.items[i];
        int32 printed = snprintf(candidate, PATH_MAX, "%s/%s", root, name);
        if (printed >= 0 && printed < PATH_MAX
            && realpath(candidate, resolved) != NULL) {
            return true;
        }
    }
    return false;
}

/* A conventional, file-wide include guard is not a feature-dependent
 * alternative. Ignore it when deciding whether a typedef is conditional. */
static int32
csane_types_guard(CPreprocessorAnalysis *pp) {
    if (pp->group_count <= 0 || pp->directive_count <= 0) {
        return -1;
    }
    CPreprocessorGroup *group = &pp->groups[0];
    int32 first = group->first_branch;
    if (group->parent_branch >= 0 || first < 0
        || group->endif_directive < 0) {
        return -1;
    }
    int32 guard_end = pp->directives[group->endif_directive].source.end;
    if ((int64)guard_end*4 < (int64)pp->tokenization->text_len*3) {
        return -1;
    }
    CPreprocessorDirective *opening =
        &pp->directives[pp->branches[first].directive_index];
    if (opening->source.start > 512) {
        return -1;
    }
    if (opening->kind == C_PREPROCESSOR_DIRECTIVE_IFNDEF) {
        return first;
    }
    if (opening->kind != C_PREPROCESSOR_DIRECTIVE_IF) {
        return -1;
    }
    Tokenization *tokens = pp->tokenization;
    int32 bang = token_range_next_text(tokens, opening->tokens,
                                       opening->hash_token, "!", 1);
    int32 defined = token_range_next_text(tokens, opening->tokens,
                                          opening->hash_token, "defined", 7);
    if (bang < opening->tokens.end && defined < opening->tokens.end) {
        return first;
    }
    return -1;
}

static void
csane_types_visit(CsanePaths *names, CsanePaths *visited,
                  char *path, CsanePolicy *policy, int32 depth) {
    char resolved[PATH_MAX];
    char *source = NULL;
    if (depth >= 48 || realpath(path, resolved) == NULL
        || csane_types_has(visited, resolved)) {
        return;
    }
    csane_paths_add(visited, resolved);
    int32 len = read_entire_file(resolved, &source);
    if (len < 0) {
        return;
    }
    Tokenization tokens = tokenize(source, len);
    CPreprocessorAnalysis pp = {0};
    if (c_preprocessor_analysis_init(&pp, &tokens)) {
        int32 guard = csane_types_guard(&pp);
        for (int32 i = 0; i < pp.directive_count; i += 1) {
            CPreprocessorDirective *directive = &pp.directives[i];
            if (directive->kind != C_PREPROCESSOR_DIRECTIVE_INCLUDE) {
                continue;
            }
            int32 start = directive->source.start;
            int32 end = directive->source.end;
            for (int32 j = start; j < end; j += 1) {
                if (source[j] != '"') {
                    continue;
                }
                int32 k = j + 1;
                while (k < end && source[k] != '"') {
                    k += 1;
                }
                if (k < end && k - j < PATH_MAX - 1) {
                    char name[PATH_MAX];
                    char include[PATH_MAX];
                    memcpy64(name, source + j + 1, k - j - 1);
                    name[k - j - 1] = '\0';
                    if (csane_types_resolve(resolved, name, policy, include)) {
                        csane_types_visit(names, visited, include, policy,
                                          depth + 1);
                    }
                }
                break;
            }
        }
        for (int32 i = 0; depth > 0 && i < tokens.token_count; i += 1) {
            if (pp.token_directives[i] >= 0
                || (pp.token_branches[i] >= 0
                    && pp.token_branches[i] != guard)
                || !TOKEN_IS(&tokens.tokens[i], "typedef")) {
                continue;
            }
            int32 end = csane_types_end(&tokens, i + 1, &pp);
            if (end >= 0) {
                csane_types_typedef(names, &tokens, i + 1, end);
                i = end;
            }
        }
    }
    c_preprocessor_analysis_free(&pp);
    free_tokenization(&tokens);
    free2(source, (int64)len + 1);
}

/* Bind imported names to matching tokens in this translation unit. The AST
 * still borrows every identifier from its own tokenization. */
static void
csane_types_bind(CAst *ast, CsanePaths *names) {
    for (int32 i = 0; i < ast->tokenization->token_count; i += 1) {
        Token *t = &ast->tokenization->tokens[i];
        if (t->kind != TOKEN_IDENT || t->len >= 256) {
            continue;
        }
        char name[256];
        memcpy64(name, t->text, t->len);
        name[t->len] = '\0';
        if (csane_types_has(names, name)) {
            c_ast_bind(ast, i, C_NAME_TYPEDEF);
        }
    }
}

static void
csane_types_prepare(CAst *ast, char *path, CsanePolicy *policy) {
    CsanePaths names = {0};
    CsanePaths visited = {0};
    csane_types_visit(&names, &visited, path, policy, 0);
    csane_types_bind(ast, &names);
    csane_paths_free(&names);
    csane_paths_free(&visited);
}

#endif /* CSANECHECK_TYPES_C */
