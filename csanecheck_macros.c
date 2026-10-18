// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_MACROS_C)
#define CSANECHECK_MACROS_C

#include "cbase.h"
#include "meta.h"
#include "csanecheck.h"

/* A conservative, location-preserving view for syntax analysis only.
 * A macro invocation is projected only when its complete expansion can be
 * represented by ONE identifier or literal. Other expansions remain original.
 * The number and indices of tokens never change. In particular, formatting
 * rules continue to inspect the original tokenization, not this view.
 */

typedef struct CsaneMacroResolver {
    Tokenization *source;
    CPreprocessorAnalysis *pp;
    int32 guard;
    int32 at;
    int32 stack[16];
    int32 depth;
} CsaneMacroResolver;

static bool
csane_macro_equal(Token *a, Token *b) {
    return (a->len == b->len) && !memcmp64(a->text, b->text, a->len);
}

static int32
csane_macro_next(Tokenization *tokens, TokenRange range, int32 after) {
    return token_range_next_significant(tokens, range, after);
}

static bool
csane_macro_name(Token *token, char *name) {
    int32 len = strlen32(name);
    return token->kind == TOKEN_IDENT && token->len == len
           && !memcmp64(token->text, name, len);
}

/* Guard branches are unconditional for the purposes of syntax discovery.
 * All other conditional definitions are ambiguous and cannot be selected. */
static bool
csane_macro_definite(CsaneMacroResolver *r, int32 directive) {
    int32 hash = r->pp->directives[directive].hash_token;
    int32 branch = r->pp->token_branches[hash];
    if (branch < 0) {
        return true;
    }
    if (r->guard < 0) {
        return false;
    }
    return branch == r->pp->groups[r->guard].first_branch;
}

static int32
csane_macro_find(CsaneMacroResolver *r, Token *name) {
    for (int32 i = r->pp->directive_count - 1; i >= 0; i -= 1) {
        CPreprocessorDirective *directive = &r->pp->directives[i];
        CPreprocessorDefine define;
        int32 ident;
        if (directive->source.start >= r->at) {
            continue;
        }
        /* A conditional redefinition makes the current value unknown. */
        bool definite = csane_macro_definite(r, i);
        if (directive->kind == C_PREPROCESSOR_DIRECTIVE_UNDEF) {
            ident = csane_macro_next(r->source, directive->tokens,
                                     directive->keyword_token);
            if (ident < directive->tokens.end
                && csane_macro_equal(name, &r->source->tokens[ident])) {
                return definite ? -1 : -2;
            }
        }
        if (directive->kind != C_PREPROCESSOR_DIRECTIVE_DEFINE
            || !c_preprocessor_define_info(r->source, directive, &define)) {
            continue;
        }
        if (csane_macro_equal(name, &r->source->tokens[define.name_token])) {
            return definite ? i : -2;
        }
    }
    return -1;
}

static bool
csane_macro_put(char *out, int32 *len, Token *token) {
    if (token->len <= 0 || token->len >= 256 - *len) {
        return false;
    }
    memcpy64(out + *len, token->text, token->len);
    *len += token->len;
    out[*len] = '\0';
    return true;
}

/* Success means a single syntactically usable replacement token, not an
 * arbitrary successful preprocessor expansion. No variadic or recursive
 * expansion is attempted beyond the fixed depth bound. */
static bool csane_macro_resolve(CsaneMacroResolver *, TokenRange,
                                char *, enum TokenKind *, int32 *);

static bool
csane_macro_arg(CsaneMacroResolver *r, TokenRange arguments,
                int32 position, TokenRange *result) {
    TokenDelimiterDepth zero = {0};
    int32 first = csane_macro_next(r->source, arguments, arguments.first - 1);
    for (int32 i = 0; first <= arguments.end; i += 1) {
        int32 comma = token_range_next_text_at_depth(r->source, arguments,
                                                     first - 1, zero, ",", 1);
        if (i == position) {
            *result = (TokenRange){first, MIN(comma, arguments.end)};
            return true;
        }
        if (comma >= arguments.end) {
            break;
        }
        first = csane_macro_next(r->source, arguments, comma);
    }
    return false;
}

static int32
csane_macro_arg_count(CsaneMacroResolver *r, TokenRange args) {
    TokenDelimiterDepth zero = {0};
    int32 first = csane_macro_next(r->source, args, args.first - 1);
    int32 count = 0;
    if (first >= args.end) {
        return 0;
    }
    for (int32 i = first; i < args.end;) {
        int32 comma = token_range_next_text_at_depth(r->source, args,
                                                     i - 1, zero, ",", 1);
        count += 1;
        if (comma >= args.end || count >= 32) {
            break;
        }
        i = csane_macro_next(r->source, args, comma);
    }
    return count;
}

static bool
csane_macro_component(CsaneMacroResolver *r, TokenRange replacement,
                      CPreprocessorDefine *define, TokenRange arguments,
                      char *out, enum TokenKind *kind, int32 *len) {
    int32 first = csane_macro_next(r->source, replacement,
                                   replacement.first - 1);
    int32 after;
    if (first >= replacement.end) {
        return false;
    }
    after = csane_macro_next(r->source, replacement, first);
    if (after < replacement.end) {
        return false;
    }
    Token *token = &r->source->tokens[first];
    if (define != NULL) {
        int32 count = c_preprocessor_define_parameter_count(r->source, define);
        for (int32 p = 0; p < count; p += 1) {
            TokenRange param;
            TokenRange actual;
            int32 name;
            bool found = c_preprocessor_define_parameter(r->source, define,
                                                         p, &param);
            if (!found) {
                return false;
            }
            name = csane_macro_next(r->source, param, param.first - 1);
            if (name >= param.end
                || !csane_macro_equal(token, &r->source->tokens[name])) {
                continue;
            }
            if (!csane_macro_arg(r, arguments, p, &actual)) {
                return false;
            }
            return csane_macro_resolve(r, actual, out, kind, len);
        }
    }
    if (token->kind == TOKEN_IDENT) {
        int32 macro = csane_macro_find(r, token);
        if (macro == -2) {
            return false;
        }
        if (macro >= 0) {
            CPreprocessorDefine alias;
            if (!c_preprocessor_define_info(r->source,
                                            &r->pp->directives[macro],
                                            &alias) || alias.function_like) {
                return false;
            }
            for (int32 i = 0; i < r->depth; i += 1) {
                if (r->stack[i] == macro) {
                    return false;
                }
            }
            if (r->depth >= 15) {
                return false;
            }
            r->stack[r->depth++] = macro;
            bool ok = csane_macro_resolve(r, alias.replacement, out, kind, len);
            r->depth -= 1;
            return ok;
        }
    }
    *kind = token->kind;
    if (*kind != TOKEN_IDENT && *kind != TOKEN_LITERAL) {
        return false;
    }
    return csane_macro_put(out, len, token);
}

static bool
csane_macro_paste(CsaneMacroResolver *r, TokenRange parts,
                  CPreprocessorDefine *define, TokenRange arguments,
                  char *out, enum TokenKind *kind, int32 *len) {
    int32 first = csane_macro_next(r->source, parts, parts.first - 1);
    int32 paste = csane_macro_next(r->source, parts, first);
    int32 second = csane_macro_next(r->source, parts, paste);
    int32 end = csane_macro_next(r->source, parts, second);
    enum TokenKind right_kind;
    if (first >= parts.end || paste >= parts.end
        || second >= parts.end || end < parts.end
        || !TOKEN_IS(&r->source->tokens[paste], "##")) {
        return false;
    }
    if (!csane_macro_component(r, (TokenRange){first, paste}, define,
                               arguments, out, kind, len)
        || !csane_macro_component(r, (TokenRange){second, end}, define,
                                  arguments, out, &right_kind, len)) {
        return false;
    }
    *kind = TOKEN_IDENT;
    return right_kind == TOKEN_IDENT;
}

static bool
csane_macro_stringify(CsaneMacroResolver *r, TokenRange replacement,
                      CPreprocessorDefine *define, TokenRange arguments,
                      char *out, enum TokenKind *kind, int32 *len) {
    int32 hash = csane_macro_next(r->source, replacement,
                                  replacement.first - 1);
    int32 name = csane_macro_next(r->source, replacement, hash);
    int32 end = csane_macro_next(r->source, replacement, name);
    if (define == NULL || hash >= replacement.end
        || name >= replacement.end || end < replacement.end
        || !TOKEN_IS(&r->source->tokens[hash], "#")) {
        return false;
    }
    int32 count = c_preprocessor_define_parameter_count(r->source, define);
    for (int32 p = 0; p < count; p += 1) {
        TokenRange param;
        TokenRange actual;
        int32 ident;
        if (!c_preprocessor_define_parameter(r->source, define, p, &param)) {
            return false;
        }
        ident = csane_macro_next(r->source, param, param.first - 1);
        if (ident >= param.end
            || !csane_macro_equal(&r->source->tokens[name],
                                  &r->source->tokens[ident])) {
            continue;
        }
        if (!csane_macro_arg(r, arguments, p, &actual)) {
            return false;
        }
        int32 token = csane_macro_next(r->source, actual, actual.first - 1);
        if (token >= actual.end
            || csane_macro_next(r->source, actual, token) < actual.end
            || r->source->tokens[token].kind != TOKEN_IDENT) {
            return false;
        }
        out[0] = '"';
        *len = 1;
        if (!csane_macro_put(out, len, &r->source->tokens[token])
            || *len >= 254) {
            return false;
        }
        out[(*len)++] = '"';
        out[*len] = '\0';
        *kind = TOKEN_LITERAL;
        return true;
    }
    return false;
}

static bool
csane_macro_resolve(CsaneMacroResolver *r, TokenRange range,
                    char *out, enum TokenKind *kind, int32 *len) {
    int32 first;
    int32 next;
    Tokenization *tokens = r->source;
    if (r->depth >= 15) {
        return false;
    }
    first = csane_macro_next(tokens, range, range.first - 1);
    if (first >= range.end) {
        return false;
    }
    next = csane_macro_next(tokens, range, first);
    if (next >= range.end) {
        return csane_macro_component(r, range, NULL,
                                     (TokenRange){0, 0}, out, kind, len);
    }
    /* Resolve CAT(a, b, ...) from the documented cbase convention. Each
     * component must resolve to one identifier; no arbitrary token pastes. */
    if (TOKEN_IS(&tokens->tokens[next], "(")
        && tokens->tokens[first].kind == TOKEN_IDENT) {
        int32 close = token_range_matching_delimiter_forward(tokens,
                                                             range, next);
        if (close < 0 || csane_macro_next(tokens, range, close) < range.end) {
            return false;
        }
        TokenRange arguments = {next + 1, close};
        Token *name = &tokens->tokens[first];
        int32 macro = csane_macro_find(r, name);
        CPreprocessorDefine define;
        bool builtin_cat = csane_macro_name(name, "CAT") && macro == -1;
        if (!builtin_cat && (macro < 0
            || !c_preprocessor_define_info(tokens, &r->pp->directives[macro],
                                           &define) || !define.function_like)) {
            return false;
        }
        int32 provided = csane_macro_arg_count(r, arguments);
        if (builtin_cat) {
            enum TokenKind part_kind;
            if (provided < 2 || provided > 8) {
                return false;
            }
            for (int32 p = 0; p < provided; p += 1) {
                TokenRange arg;
                if (!csane_macro_arg(r, arguments, p, &arg)) {
                    break;
                }
                if (!csane_macro_resolve(r, arg, out, &part_kind, len)
                    || part_kind != TOKEN_IDENT) {
                    return false;
                }
            }
            *kind = TOKEN_IDENT;
            return true;
        }
        int32 expected = c_preprocessor_define_parameter_count(tokens, &define);
        if (provided != expected) {
            return false;
        }
        for (int32 i = 0; i < r->depth; i += 1) {
            if (r->stack[i] == macro) {
                return false;
            }
        }
        r->stack[r->depth++] = macro;
        int32 initial = *len;
        bool ok = csane_macro_paste(r, define.replacement, &define,
                                    arguments, out, kind, len);
        if (!ok) {
            *len = initial;
            ok = csane_macro_stringify(r, define.replacement, &define,
                                       arguments, out, kind, len);
        }
        if (!ok) {
            *len = initial;
            ok = csane_macro_component(r, define.replacement, &define,
                                       arguments, out, kind, len);
        }
        r->depth -= 1;
        return ok;
    }
    return false;
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

static void
csane_macro_project(CsaneMacroView *view, CsaneMacroResolver *r,
                    int32 first, int32 end, char *text,
                    enum TokenKind kind, int32 len) {
    if (len == 0 || len >= 256
        || (kind == TOKEN_IDENT && !csane_macro_identifier(text, len))) {
        return;
    }
    char *owned = malloc2(len + 1);
    memcpy64(owned, text, len + 1);
    view->owned[view->owned_count++] = owned;
    view->copy[first].text = owned;
    view->copy[first].len = len;
    view->copy[first].kind = kind;
    for (int32 i = first + 1; i < end; i += 1) {
        view->copy[i].kind = TOKEN_SPACE;
        view->copy[i].len = 0;
    }
    view->projected_tokens += end - first;
    (void)r;
}

static void
csane_macro_view_init(CsaneMacroView *view, Tokenization *source,
                      CPreprocessorAnalysis *pp) {
    int32 count = source->token_count;
    *view = (CsaneMacroView){0};
    view->tokens = *source;
    view->copy = malloc2((int64)count*SIZEOF(Token));
    view->owned = malloc2((int64)count*SIZEOF(*view->owned));
    memcpy64(view->copy, source->tokens, (int64)count*SIZEOF(Token));
    view->tokens.tokens = view->copy;
    CsaneMacroResolver r = {.source = source, .pp = pp, .guard = -1};
    r.guard = csane_types_guard(pp);
    TokenRange all = {0, count};
    for (int32 i = csane_macro_next(source, all, -1); i < count;
         i = csane_macro_next(source, all, i)) {
        Token *token = &source->tokens[i];
        if (token->kind != TOKEN_IDENT || pp->token_directives[i] >= 0) {
            continue;
        }
        r.at = token->offset;
        int32 macro = csane_macro_find(&r, token);
        if (macro == -2 || (macro < 0
                            && !csane_macro_name(token, "CAT"))) {
            continue;
        }
        int32 next = csane_macro_next(source, all, i);
        int32 end = i + 1;
        if (next < count && TOKEN_IS(&source->tokens[next], "(")) {
            bool function_like = macro < 0;
            CPreprocessorDefine define;
            if (macro >= 0) {
                CPreprocessorDirective *directive = &pp->directives[macro];
                bool found = c_preprocessor_define_info(source, directive,
                                                        &define);
                function_like = found && define.function_like;
            }
            if (function_like) {
                int32 close =
                    token_range_matching_delimiter_forward(source, all, next);
                if (close < 0) {
                    continue;
                }
                end = close + 1;
            }
        }
        char text[256] = {0};
        enum TokenKind kind = TOKEN_UNKNOWN;
        int32 len = 0;
        r.at = token->offset;
        r.depth = 0;
        if (csane_macro_resolve(&r, (TokenRange){i, end},
                                text, &kind, &len)) {
            csane_macro_project(view, &r, i, end, text, kind, len);
            i = end - 1;
        }
    }
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
