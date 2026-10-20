// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_MACROS_C)
#define CSANECHECK_MACROS_C

#include "cbase.h"
#include "meta.h"
#include "csck.h"

/* The syntax token view has exactly the original token indices. Expanded
 * tokens borrow source positions from the invocation; source and lexical
 * checks always use the unmodified original tokenization. An expansion that
 * needs more token slots than its invocation is left unresolved. */

enum {CSANE_MACRO_DEPTH = 16, CSANE_MACRO_LIMIT = 64,
      CSANE_MACRO_EVENTS_LIMIT = 16384};

typedef struct CsaneMacroUnit {
    Tokenization tokens;
    CPreprocessorAnalysis pp;
    char *source;
    int32 length;
    char *path;
    struct CsaneMacroUnit *next;
} CsaneMacroUnit;

typedef struct CsaneMacroEvent {
    Tokenization *tokens;
    CPreprocessorDefine define;
    Token *name;
    int32 available_at;
    bool definite;
    bool undef;
} CsaneMacroEvent;

typedef struct CsaneMacroToken {
    char *text;
    int32 len;
    enum TokenKind kind;
} CsaneMacroToken;

typedef struct CsaneMacroTokens {
    CsaneMacroToken items[CSANE_MACRO_LIMIT];
    int32 count;
} CsaneMacroTokens;

typedef struct CsaneMacroResolver {
    Tokenization *source;
    CsaneMacroEvent *events;
    int32 count;
    int32 capacity;
    int32 at;
    CsaneMacroUnit *units;
    char *include_paths[32];
    CsaneMacroEvent *active[CSANE_MACRO_DEPTH];
    int32 recursion;
    bool overflowed;
    int32 scratch_count;
    char scratch[CSANE_MACRO_LIMIT][256];
} CsaneMacroResolver;

static bool
csane_macro_equal(CsaneMacroToken a, CsaneMacroToken b) {
    return a.len == b.len && !memcmp64(a.text, b.text, a.len);
}

static bool
csane_macro_is(CsaneMacroToken token, char *text) {
    int32 len = strlen32(text);
    return token.len == len && !memcmp64(token.text, text, len);
}

static CsaneMacroToken
csane_macro_token(Token *token) {
    return (CsaneMacroToken){token->text, token->len, token->kind};
}

static bool
csane_macro_add(CsaneMacroTokens *tokens, CsaneMacroToken token) {
    if (tokens->count >= CSANE_MACRO_LIMIT) {
        return false;
    }
    tokens->items[tokens->count++] = token;
    return true;
}

static bool
csane_macro_collect(Tokenization *source, TokenRange range,
                    CsaneMacroTokens *out) {
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *t = &source->tokens[i];
        if (!token_is_trivia(t)
            && !csane_macro_add(out, csane_macro_token(t))) {
            return false;
        }
    }
    return true;
}

static bool
csane_macro_identifier(char *text, int32 len) {
    if (len <= 0 || !((text[0] >= 'A' && text[0] <= 'Z')
                     || (text[0] >= 'a' && text[0] <= 'z')
                     || text[0] == '_')) {
        return false;
    }
    for (int32 i = 1; i < len; i += 1) {
        char c = text[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static char *
csane_macro_scratch(CsaneMacroResolver *r, char *text, int32 len) {
    if (len <= 0 || len >= 256 || r->scratch_count >= CSANE_MACRO_LIMIT) {
        return NULL;
    }
    char *dst = r->scratch[r->scratch_count++];
    memcpy64(dst, text, len);
    dst[len] = '\0';
    return dst;
}

/* Imported headers are processed at the position of their #include in the
 * including file. Later local definitions and undefinitions take precedence.
 * Non-guard conditional definitions poison earlier values instead of being
 * silently ignored. */
static void
csane_macro_event(CsaneMacroResolver *r, Tokenization *tokens,
                  CPreprocessorDirective *directive, int32 at,
                  bool definite) {
    if (r->count >= CSANE_MACRO_EVENTS_LIMIT) {
        r->overflowed = true;
        return;
    }
    CPreprocessorDefine define = {0};
    Token *name;
    bool undef = directive->kind == C_PREPROCESSOR_DIRECTIVE_UNDEF;
    if (undef) {
        TokenRange range = directive->tokens;
        int32 i = token_range_next_significant(tokens, range,
                                               directive->keyword_token);
        if (i >= range.end || tokens->tokens[i].kind != TOKEN_IDENT) {
            return;
        }
        name = &tokens->tokens[i];
    } else {
        if (!c_preprocessor_define_info(tokens, directive, &define)) {
            return;
        }
        name = &tokens->tokens[define.name_token];
    }
    if (r->count == r->capacity) {
        int32 next = r->capacity ? r->capacity*2 : 128;
        r->events = realloc2(r->events, r->capacity, next,
                             SIZEOF(CsaneMacroEvent));
        r->capacity = next;
    }
    r->events[r->count++] = (CsaneMacroEvent){
        .tokens = tokens, .define = define, .name = name,
        .available_at = at, .definite = definite, .undef = undef,
    };
}

static bool
csane_macro_condition(CPreprocessorAnalysis *pp, int32 guard,
                      int32 token) {
    int32 branch = pp->token_branches[token];
    return branch < 0 || (guard >= 0
                          && branch == pp->groups[guard].first_branch);
}

static bool
csane_macro_include(Tokenization *tokens, CPreprocessorDirective *d,
                    char *name) {
    for (int32 j = d->source.start; j < d->source.end; j += 1) {
        if (tokens->text[j] != '"') {
            continue;
        }
        int32 k = j + 1;
        while (k < d->source.end && tokens->text[k] != '"') {
            k += 1;
        }
        if (k >= d->source.end || k - j >= PATH_MAX) {
            return false;
        }
        memcpy64(name, tokens->text + j + 1, k - j - 1);
        name[k - j - 1] = '\0';
        return true;
    }
    return false;
}

static void
csane_macro_visit(CsaneMacroResolver *r, Tokenization *tokens,
                  CPreprocessorAnalysis *pp, char *path,
                  CsanePolicy *policy, int32 at, bool certain, int32 depth) {
    if (depth >= 32 || r->count >= CSANE_MACRO_EVENTS_LIMIT) {
        r->overflowed = true;
        return;
    }
    int32 guard = depth ? csane_types_guard(pp) : -1;
    for (int32 i = 0; i < pp->directive_count; i += 1) {
        CPreprocessorDirective *d = &pp->directives[i];
        bool definite = certain && csane_macro_condition(pp, guard,
                                                         d->hash_token);
        if (d->kind == C_PREPROCESSOR_DIRECTIVE_INCLUDE) {
            char name[PATH_MAX];
            char resolved[PATH_MAX];
            if (!csane_macro_include(tokens, d, name)
                || !csane_types_resolve(path, name, policy, resolved)) {
                continue;
            }
            /* An include cycle is stopped by the active path, not a global
             * visited set: an intentionally repeated unguarded header may
             * legitimately redefine macros at a later position. */
            bool cycle = false;
            for (int32 j = 0; j < depth; j += 1) {
                if (!strcmp(r->include_paths[j], resolved)) {
                    cycle = true;
                    break;
                }
            }
            if (cycle) {
                continue;
            }
            char *source = NULL;
            int32 len = read_entire_file(resolved, &source);
            if (len < 0) {
                continue;
            }
            CsaneMacroUnit *unit = malloc2(SIZEOF(*unit));
            *unit = (CsaneMacroUnit){0};
            unit->source = source;
            unit->length = len;
            int32 path_len = strlen32(resolved);
            unit->path = malloc2(path_len + 1);
            memcpy64(unit->path, resolved, path_len + 1);
            unit->tokens = tokenize(source, len);
            if (!c_preprocessor_analysis_init(&unit->pp, &unit->tokens)) {
                c_preprocessor_analysis_free(&unit->pp);
                free_tokenization(&unit->tokens);
                free2(unit->source, (int64)len + 1);
                free2(unit->path, strlen32(unit->path) + 1);
                free2(unit, SIZEOF(*unit));
                continue;
            }
            unit->next = r->units;
            r->units = unit;
            r->include_paths[depth] = unit->path;
            csane_macro_visit(r, &unit->tokens, &unit->pp, unit->path,
                              policy, depth ? at : d->tokens.end,
                              definite, depth + 1);
            continue;
        }
        if (d->kind == C_PREPROCESSOR_DIRECTIVE_DEFINE
            || d->kind == C_PREPROCESSOR_DIRECTIVE_UNDEF) {
            csane_macro_event(r, tokens, d,
                              depth ? at : d->tokens.end, definite);
        }
    }
}

static CsaneMacroEvent *
csane_macro_find(CsaneMacroResolver *r, CsaneMacroToken name) {
    for (int32 i = r->count - 1; i >= 0; i -= 1) {
        CsaneMacroEvent *event = &r->events[i];
        if (event->available_at > r->at) {
            continue;
        }
        if (csane_macro_equal(csane_macro_token(event->name), name)) {
            return event;
        }
    }
    return NULL;
}

/* Return argument slices within a balanced macro invocation. */
static bool
csane_macro_arguments(CsaneMacroTokens *input, int32 open, int32 close,
                      TokenRange *args, int32 *count) {
    int32 depth = 0;
    int32 start = open + 1;
    *count = 0;
    if (start == close) {
        return true;
    }
    for (int32 i = start; i <= close; i += 1) {
        CsaneMacroToken token = {0};
        if (i < close) {
            token = input->items[i];
        }
        if (i == close || (depth == 0 && csane_macro_is(token, ","))) {
            if (*count >= 16) {
                return false;
            }
            args[(*count)++] = (TokenRange){start, i};
            start = i + 1;
        } else if (csane_macro_is(token, "(") || csane_macro_is(token, "[")
                   || csane_macro_is(token, "{")) {
            depth += 1;
        } else if (csane_macro_is(token, ")")
                   || csane_macro_is(token, "]")
                   || csane_macro_is(token, "}")) {
            depth -= 1;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0;
}

static int32
csane_macro_parameter(CsaneMacroEvent *event, CsaneMacroToken token) {
    Tokenization *t = event->tokens;
    int32 count = c_preprocessor_define_parameter_count(t, &event->define);
    for (int32 i = 0; i < count; i += 1) {
        TokenRange range;
        if (!c_preprocessor_define_parameter(t, &event->define, i, &range)) {
            return -1;
        }
        for (int32 j = range.first; j < range.end; j += 1) {
            if (t->tokens[j].kind == TOKEN_IDENT
                && csane_macro_equal(csane_macro_token(&t->tokens[j]),
                                     token)) {
                return i;
            }
        }
    }
    return -1;
}

static bool csane_macro_expand(CsaneMacroResolver *, CsaneMacroTokens *,
                               CsaneMacroTokens *, int32);

static bool
csane_macro_append(CsaneMacroTokens *dst, CsaneMacroTokens *src,
                   TokenRange range) {
    for (int32 i = range.first; i < range.end; i += 1) {
        if (!csane_macro_add(dst, src->items[i])) {
            return false;
        }
    }
    return true;
}

static bool
csane_macro_concat(CsaneMacroResolver *r, CsaneMacroToken a,
                   CsaneMacroToken b, CsaneMacroToken *result) {
    if (a.kind != TOKEN_IDENT
        || (b.kind != TOKEN_IDENT
            && !(b.kind == TOKEN_LITERAL
                 && b.len > 0 && b.text[0] >= '0'
                 && b.text[0] <= '9'))
        || a.len + b.len >= 256) {
        return false;
    }
    char buffer[256];
    memcpy64(buffer, a.text, a.len);
    memcpy64(buffer + a.len, b.text, b.len);
    int32 len = a.len + b.len;
    if (!csane_macro_identifier(buffer, len)) {
        return false;
    }
    char *name = csane_macro_scratch(r, buffer, len);
    if (name == NULL) {
        return false;
    }
    *result = (CsaneMacroToken){name, len, TOKEN_IDENT};
    return true;
}

static bool
csane_macro_cat(CsaneMacroResolver *r, CsaneMacroTokens *input,
                TokenRange *args, int32 count, CsaneMacroTokens *result,
                int32 depth) {
    if (count < 2 || count > 8) {
        return false;
    }
    CsaneMacroToken combined = {0};
    for (int32 p = 0; p < count; p += 1) {
        CsaneMacroTokens part = {0};
        CsaneMacroTokens expanded = {0};
        if (!csane_macro_append(&part, input, args[p])
            || !csane_macro_expand(r, &part, &expanded, depth + 1)
            || expanded.count != 1) {
            return false;
        }
        if (p == 0) {
            if (expanded.items[0].kind != TOKEN_IDENT) {
                return false;
            }
            combined = expanded.items[0];
        } else if (!csane_macro_concat(r, combined, expanded.items[0],
                                       &combined)) {
            return false;
        }
    }
    return csane_macro_add(result, combined);
}

/* Substitute parameters, then rescan. ## pastes identifiers; # stringifies
 * only a single identifier argument. Anything more complex remains opaque. */
static bool
csane_macro_substitute(CsaneMacroResolver *r, CsaneMacroEvent *event,
                       CsaneMacroTokens *input, TokenRange *args,
                       int32 count, CsaneMacroTokens *output, int32 depth) {
    CsaneMacroTokens body = {0};
    CsaneMacroTokens substituted = {0};
    if (!csane_macro_collect(event->tokens, event->define.replacement, &body)) {
        return false;
    }
    CPreprocessorDefine *define = &event->define;
    Tokenization *tokens = event->tokens;
    if (define->function_like
        && count != c_preprocessor_define_parameter_count(tokens, define)) {
        return false;
    }
    for (int32 i = 0; i < body.count; i += 1) {
        CsaneMacroToken token = body.items[i];
        if (csane_macro_is(token, "#") && i + 1 < body.count) {
            int32 param = csane_macro_parameter(event, body.items[i + 1]);
            if (param >= 0 && args[param].end - args[param].first == 1) {
                CsaneMacroToken word = input->items[args[param].first];
                if (word.kind != TOKEN_IDENT || word.len >= 253) {
                    return false;
                }
                char buffer[256];
                buffer[0] = '"';
                memcpy64(buffer + 1, word.text, word.len);
                buffer[word.len + 1] = '"';
                int32 len = word.len + 2;
                char *string = csane_macro_scratch(r, buffer, len);
                CsaneMacroToken literal = {string, len, TOKEN_LITERAL};
                if (string == NULL || !csane_macro_add(&substituted, literal)) {
                    return false;
                }
                i += 1;
                continue;
            }
            return false;
        }
        if (csane_macro_is(token, "##")) {
            if (!substituted.count || i + 1 >= body.count) {
                return false;
            }
            CsaneMacroToken rhs = body.items[++i];
            int32 param = csane_macro_parameter(event, rhs);
            if (param >= 0) {
                if (args[param].end - args[param].first != 1) {
                    return false;
                }
                rhs = input->items[args[param].first];
            }
            CsaneMacroToken *left = &substituted.items[substituted.count - 1];
            if (!csane_macro_concat(r, *left, rhs, left)) {
                return false;
            }
            continue;
        }
        int32 param = csane_macro_parameter(event, token);
        if (param >= 0) {
            if (!csane_macro_append(&substituted, input, args[param])) {
                return false;
            }
        } else if (!csane_macro_add(&substituted, token)) {
            return false;
        }
    }
    return csane_macro_expand(r, &substituted, output, depth + 1);
}

static bool
csane_macro_expand(CsaneMacroResolver *r, CsaneMacroTokens *input,
                   CsaneMacroTokens *output, int32 depth) {
    if (depth >= CSANE_MACRO_DEPTH) {
        return false;
    }
    for (int32 i = 0; i < input->count; i += 1) {
        CsaneMacroToken token = input->items[i];
        /* Preserve calls that are themselves the subject of a guideline.
         * Expanding STRLIT_LEN would hide its original argument structure. */
        if (csane_macro_is(token, "STRLIT_LEN")) {
            if (!csane_macro_add(output, token)) {
                return false;
            }
            continue;
        }
        CsaneMacroEvent *event = token.kind == TOKEN_IDENT
                                 ? csane_macro_find(r, token) : NULL;
        if (event != NULL && !event->definite) {
            return false;
        }
        if (event != NULL && event->undef) {
            event = NULL;
        }
        bool builtin_cat = csane_macro_is(token, "CAT")
                           && (event == NULL || event->definite);
        bool function_like = event != NULL && event->define.function_like;
        if ((builtin_cat || function_like) && i + 1 < input->count
            && csane_macro_is(input->items[i + 1], "(")) {
            int32 nesting = 0;
            int32 close = -1;
            for (int32 j = i + 1; j < input->count; j += 1) {
                if (csane_macro_is(input->items[j], "(")) {
                    nesting += 1;
                } else if (csane_macro_is(input->items[j], ")")) {
                    nesting -= 1;
                    if (nesting == 0) {
                        close = j;
                        break;
                    }
                }
            }
            if (close < 0) {
                return false;
            }
            TokenRange args[16] = {0};
            int32 count;
            if (!csane_macro_arguments(input, i + 1, close, args, &count)) {
                return false;
            }
            CsaneMacroTokens expanded = {0};
            if (builtin_cat) {
                if (!csane_macro_cat(r, input, args, count, &expanded, depth)) {
                    return false;
                }
            } else {
                for (int32 j = 0; j < r->recursion; j += 1) {
                    if (r->active[j] == event) {
                        return false;
                    }
                }
                if (r->recursion >= CSANE_MACRO_DEPTH) {
                    return false;
                }
                r->active[r->recursion++] = event;
                bool ok = csane_macro_substitute(r, event, input, args,
                                                 count, &expanded, depth);
                r->recursion -= 1;
                if (!ok) {
                    return false;
                }
            }
            if (!csane_macro_append(output, &expanded,
                                    (TokenRange){0, expanded.count})) {
                return false;
            }
            i = close;
            continue;
        }
        if (event != NULL && !function_like) {
            for (int32 j = 0; j < r->recursion; j += 1) {
                if (r->active[j] == event) {
                    return false;
                }
            }
            if (r->recursion >= CSANE_MACRO_DEPTH) {
                return false;
            }
            CsaneMacroTokens expanded = {0};
            r->active[r->recursion++] = event;
            bool ok = csane_macro_substitute(r, event, input, NULL, 0,
                                             &expanded, depth);
            r->recursion -= 1;
            if (!ok || !csane_macro_append(output, &expanded,
                                           (TokenRange){0, expanded.count})) {
                return false;
            }
            continue;
        }
        if (!csane_macro_add(output, token)) {
            return false;
        }
    }
    return true;
}

static bool
csane_macro_changed(CsaneMacroTokens *before, CsaneMacroTokens *after) {
    if (before->count != after->count) {
        return true;
    }
    for (int32 i = 0; i < before->count; i += 1) {
        if (!csane_macro_equal(before->items[i], after->items[i])) {
            return true;
        }
    }
    return false;
}

static void
csane_macro_project(CsaneMacroView *view, CsaneMacroResolver *r,
                    int32 first, int32 end, CsaneMacroTokens *replacement) {
    Tokenization *source = r->source;
    int32 slots[CSANE_MACRO_LIMIT];
    int32 count = 0;
    for (int32 i = first; i < end; i += 1) {
        if (!token_is_trivia(&source->tokens[i])) {
            if (count >= CSANE_MACRO_LIMIT) {
                return;
            }
            slots[count++] = i;
        }
    }
    if (replacement->count == 0 || replacement->count > count) {
        return;
    }
    for (int32 i = 0; i < replacement->count; i += 1) {
        CsaneMacroToken token = replacement->items[i];
        if (token.len <= 0 || token.len >= 256) {
            return;
        }
        if (token.kind == TOKEN_IDENT
            && !csane_macro_identifier(token.text, token.len)) {
            return;
        }
    }
    for (int32 i = 0; i < replacement->count; i += 1) {
        CsaneMacroToken token = replacement->items[i];
        char *text = malloc2(token.len + 1);
        memcpy64(text, token.text, token.len);
        text[token.len] = '\0';
        view->owned[view->owned_count++] = text;
        view->copy[slots[i]].text = text;
        view->copy[slots[i]].len = token.len;
        view->copy[slots[i]].kind = token.kind;
    }
    for (int32 i = replacement->count; i < count; i += 1) {
        view->copy[slots[i]].kind = TOKEN_SPACE;
        view->copy[slots[i]].len = 0;
    }
    view->projected_macros += 1;
    view->projected_tokens += end - first;
}

static void
csane_macro_view_init(CsaneMacroView *view, Tokenization *source,
                      CPreprocessorAnalysis *pp, char *path,
                      CsanePolicy *policy) {
    int32 count = source->token_count;
    *view = (CsaneMacroView){0};
    view->tokens = *source;
    view->copy = malloc2((int64)count*SIZEOF(Token));
    view->owned = malloc2((int64)count*SIZEOF(*view->owned));
    memcpy64(view->copy, source->tokens, (int64)count*SIZEOF(Token));
    view->tokens.tokens = view->copy;

    CsaneMacroResolver r = {.source = source};
    csane_macro_visit(&r, source, pp, path, policy, 0, true, 0);
    TokenRange all = {0, count};
    for (int32 i = token_range_next_significant(source, all, -1);
         !r.overflowed && i < count;
         i = token_range_next_significant(source, all, i)) {
        if (source->tokens[i].kind != TOKEN_IDENT
            || pp->token_directives[i] >= 0) {
            continue;
        }
        r.at = i;
        CsaneMacroToken name = csane_macro_token(&source->tokens[i]);
        CsaneMacroEvent *event = csane_macro_find(&r, name);
        if (event == NULL && !csane_macro_is(name, "CAT")) {
            continue;
        }
        if (event != NULL && (!event->definite || event->undef)) {
            continue;
        }
        bool function_like = (event != NULL && event->define.function_like)
                             || csane_macro_is(name, "CAT");
        int32 end = i + 1;
        if (function_like) {
            int32 open = token_range_next_significant(source, all, i);
            if (open >= count || !TOKEN_IS(&source->tokens[open], "(")) {
                continue;
            }
            int32 close = token_range_matching_delimiter_forward(source,
                                                                 all, open);
            if (close < 0) {
                continue;
            }
            end = close + 1;
        }
        CsaneMacroTokens input = {0};
        CsaneMacroTokens expanded = {0};
        r.scratch_count = 0;
        r.recursion = 0;
        if (csane_macro_collect(source, (TokenRange){i, end}, &input)
            && csane_macro_expand(&r, &input, &expanded, 0)
            && csane_macro_changed(&input, &expanded)) {
            int32 previous = view->owned_count;
            csane_macro_project(view, &r, i, end, &expanded);
            if (view->owned_count > previous) {
                i = end - 1;
            }
        }
    }
    for (CsaneMacroUnit *unit = r.units; unit != NULL;) {
        CsaneMacroUnit *next = unit->next;
        c_preprocessor_analysis_free(&unit->pp);
        free_tokenization(&unit->tokens);
        free2(unit->source, (int64)unit->length + 1);
        free2(unit->path, strlen32(unit->path) + 1);
        free2(unit, SIZEOF(*unit));
        unit = next;
    }
    free2(r.events, (int64)r.capacity*SIZEOF(CsaneMacroEvent));
}

static void
csane_macro_view_free(CsaneMacroView *view) {
    for (int32 i = 0; i < view->owned_count; i += 1) {
        free2(view->owned[i], strlen32(view->owned[i]) + 1);
    }
    free2(view->owned, (int64)view->tokens.token_count*SIZEOF(*view->owned));
    free2(view->copy, (int64)view->tokens.token_count*SIZEOF(Token));
    *view = (CsaneMacroView){0};
}

#endif /* CSANECHECK_MACROS_C */
