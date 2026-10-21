// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOPLEVEL_C)
#define CSANECHECK_TOPLEVEL_C

#include "cbase.h"
#include "csck_toplevel.h"
#include "csck_tokens.h"

typedef struct CsaneTopStream {
    CsaneTopLevels *top;
    int32 owner;
    int32 *indices;
    int32 *leaf;
    int32 *matches;
    int32 *stack;
    int32 count;
    int32 capacity;
} CsaneTopStream;

static bool
csane_top_is(CsaneTopStream *stream, int32 pos, char *name) {
    if (pos < 0 || pos >= stream->count) {
        return false;
    }
    Token *token = &stream->top->regions->tokens->tokens[stream->indices[pos]];
    return TOKEN_IS(token, name);
}

static Token *
csane_top_token(CsaneTopStream *stream, int32 pos) {
    return &stream->top->regions->tokens->tokens[stream->indices[pos]];
}

static bool
csane_top_macro_name(Token *token) {
    if (token->kind != TOKEN_IDENT || token->len <= 0) {
        return false;
    }
    bool has_upper = false;
    for (int32 i = 0; i < token->len; i += 1) {
        char c = token->text[i];
        if (c >= 'a' && c <= 'z') {
            return false;
        }
        if (c >= 'A' && c <= 'Z') {
            has_upper = true;
        }
    }
    return has_upper;
}

static void
csane_top_append(CsaneTopStream *stream, enum CsaneTopKind kind,
                 int32 first, int32 end, int32 brace, int32 name) {
    if (first >= end) {
        return;
    }
    CsaneTopLevels *top = stream->top;
    if (top->count == top->capacity) {
        int32 next = top->capacity ? top->capacity*2 : 64;
        top->items = realloc2(top->items, top->capacity, next,
                              SIZEOF(*top->items));
        top->capacity = next;
    }
    Token *start = csane_top_token(stream, first);
    Token *last = csane_top_token(stream, end - 1);
    CsaneTopItem *item = &top->items[top->count];
    *item = (CsaneTopItem){
        .kind = kind,
        .source = {start->offset, last->offset + last->len},
        .tokens = {stream->indices[first], stream->indices[end - 1] + 1},
        .header = {stream->indices[first], stream->indices[end - 1] + 1},
        .body = {0},
        .owner_region = stream->owner,
        .name_token = name >= 0 ? stream->indices[name] : -1,
    };
    if (kind == CSANE_TOP_FUNCTION_DEFINITION) {
        item->header.end = stream->indices[brace];
        item->body = (TokenRange){stream->indices[brace],
                                  stream->indices[end - 1] + 1};
    }
    if (kind == CSANE_TOP_OPAQUE) {
        top->opaque_count += 1;
    }
    top->count += 1;
    return;
}

static void
csane_top_push(CsaneTopStream *stream, int32 token, int32 leaf) {
    if (stream->count == stream->capacity) {
        int32 next = stream->capacity ? stream->capacity*2 : 64;
        stream->indices = realloc2(stream->indices, stream->capacity,
                                   next, SIZEOF(*stream->indices));
        stream->leaf = realloc2(stream->leaf, stream->capacity, next,
                                SIZEOF(*stream->leaf));
        stream->capacity = next;
    }
    stream->indices[stream->count] = token;
    stream->leaf[stream->count] = leaf;
    stream->count += 1;
    return;
}

static void
csane_top_collect(CsaneTopStream *stream) {
    CsaneRegions *regions = stream->top->regions;
    Tokenization *tokens = regions->tokens;
    CsaneSourceRegion *owner = &regions->items[stream->owner];
    CsaneTokenPath path;
    CsaneTokenView view;
    int32 child;
    int32 i;

    if (!csane_token_path_init(&path, tokens, owner->tokens)) {
        return;
    }
    child = owner->first_child;
    for (; child >= 0; child = regions->items[child].next_sibling) {
        CsaneSourceRegion *region = &regions->items[child];
        if (region->kind == CSANE_REGION_CODE) {
            (void)csane_token_path_append(&path, region->tokens);
        }
    }
    if (!csane_tokens_path_view(&view, &path)) {
        csane_token_path_free(&path);
        return;
    }
    i = csane_tokens_next(&view, view.range.first - 1);
    while (i >= view.range.first && i < view.range.end) {
        Token *token = &tokens->tokens[i];
        int32 leaf = csane_regions_at_token(regions, i);
        if (token->kind != TOKEN_PREPROC && leaf >= 0) {
            csane_top_push(stream, i, leaf);
        }
        i = csane_tokens_next(&view, i);
    }
    csane_token_path_free(&path);
    return;
}

static bool
csane_top_open(CsaneTopStream *stream, int32 pos) {
    return csane_top_is(stream, pos, "(")
           || csane_top_is(stream, pos, "[")
           || csane_top_is(stream, pos, "{");
}

static bool
csane_top_close(CsaneTopStream *stream, int32 pos) {
    return csane_top_is(stream, pos, ")")
           || csane_top_is(stream, pos, "]")
           || csane_top_is(stream, pos, "}");
}

static bool
csane_top_pair(CsaneTopStream *stream, int32 a, int32 b) {
    return (csane_top_is(stream, a, "(")
            && csane_top_is(stream, b, ")"))
           || (csane_top_is(stream, a, "[")
               && csane_top_is(stream, b, "]"))
           || (csane_top_is(stream, a, "{")
               && csane_top_is(stream, b, "}"));
}

/* Matches are stream-local: directives and nested conditional alternatives
 * are not injected as C tokens into their enclosing code stream. */
/* An unmatched inner delimiter must not hide an enclosing function body.
 * Search the stack for the nearest compatible opener and discard only the
 * damaged nesting above it. Matches outside the damaged span survive. */
static void
csane_top_match(CsaneTopStream *stream) {
    if (stream->count == 0) {
        return;
    }
    int32 size = stream->count*SIZEOF(int32);
    stream->matches = malloc2(size);
    stream->stack = malloc2(size);
    int32 depth = 0;
    for (int32 i = 0; i < stream->count; i += 1) {
        stream->matches[i] = -1;
        if (csane_top_open(stream, i)) {
            stream->stack[depth] = i;
            depth += 1;
        } else if (csane_top_close(stream, i)) {
            int32 found = depth - 1;
            while (found >= 0
                   && !csane_top_pair(stream, stream->stack[found], i)) {
                found -= 1;
            }
            if (found < 0) {
                depth = 0;
                continue;
            }
            int32 open = stream->stack[found];
            stream->matches[open] = i;
            stream->matches[i] = open;
            depth = found;
        }
    }
    return;
}

static bool
csane_top_has(CsaneTopStream *stream, int32 first, int32 end,
              char *name) {
    for (int32 i = first; i < end; i += 1) {
        if (csane_top_is(stream, i, name)) {
            return true;
        }
        if (csane_top_open(stream, i) && stream->matches[i] > i) {
            i = stream->matches[i];
        }
    }
    return false;
}

static bool
csane_top_attribute(CsaneTopStream *stream, int32 pos) {
    if (pos < 0 || pos >= stream->count) {
        return false;
    }
    Token *token = csane_top_token(stream, pos);
    return csane_top_is(stream, pos, "__attribute__")
           || csane_top_is(stream, pos, "__attribute")
           || csane_top_is(stream, pos, "__declspec")
           || csane_top_is(stream, pos, "__asm__")
           || (token->kind == TOKEN_IDENT && token->len > 5
               && !memcmp64(token->text, "ATTR_", 5));
}

/* ATTR_* can itself be a macro-generated function name. Treat it as a
 * trailing annotation only after an earlier parameter list has closed. */
static bool
csane_top_trailing_attribute(CsaneTopStream *stream, int32 first,
                             int32 pos) {
    if (!csane_top_attribute(stream, pos)) {
        return false;
    }
    for (int32 i = first; i < pos; i += 1) {
        if (csane_top_is(stream, i, ")")
            && stream->matches[i] >= first) {
            return true;
        }
    }
    return false;
}

/* Return the function-name token, or -1. CAT(...)(...) is deliberately
 * supported: its name is the CAT macro, not an expanded identifier. */
static int32
csane_top_function_name(CsaneTopStream *stream, int32 first, int32 end) {
    /* Strip a mixture of attribute calls and object-like attributes in
     * reverse order. They may follow each other in either order. */
    while (end > first) {
        if (csane_top_trailing_attribute(stream, first, end - 1)) {
            end -= 1;
            continue;
        }
        if (csane_top_is(stream, end - 1, ")")) {
            int32 open = stream->matches[end - 1];
            if (open > first
                && csane_top_trailing_attribute(stream, first, open - 1)) {
                end = open - 1;
                continue;
            }
        }
        break;
    }
    if (first >= end || !csane_top_is(stream, end - 1, ")")) {
        return -1;
    }
    int32 open = stream->matches[end - 1];
    if (open <= first) {
        return -1;
    }
    int32 name = open - 1;
    if (csane_top_token(stream, name)->kind == TOKEN_IDENT) {
        return name;
    }
    if (csane_top_is(stream, name, ")")) {
        int32 macro_open = stream->matches[name];
        if (macro_open > first
            && csane_top_token(stream, macro_open - 1)->kind == TOKEN_IDENT
            && csane_top_macro_name(csane_top_token(stream,
                                                    macro_open - 1))) {
            return macro_open - 1;
        }
    }
    return -1;
}

static bool
csane_top_is_function(CsaneTopStream *stream, int32 first,
                      int32 end, int32 *name) {
    *name = -1;
    if (first >= end || csane_top_is(stream, first, "typedef")
        || csane_top_has(stream, first, end, "=")) {
        return false;
    }
    *name = csane_top_function_name(stream, first, end);
    if (*name < 0 || *name == first) {
        return false;
    }
    /* The '*' immediately before a function name is part of its return
     * type, as in char *name(void). Pointer variables instead place the
     * name inside a parenthesized declarator: char (*name)(void). The
     * latter is rejected by csane_top_function_name(). */
    return true;
}

static bool
csane_top_type(CsaneTopStream *stream, int32 first, int32 end) {
    if (csane_top_is(stream, first, "typedef")) {
        return true;
    }
    for (int32 i = first; i < end && i < first + 3; i += 1) {
        if (csane_top_is(stream, i, "struct")
            || csane_top_is(stream, i, "union")
            || csane_top_is(stream, i, "enum")) {
            for (int32 j = i + 1; j < end; j += 1) {
                if (csane_top_is(stream, j, "{")) {
                    return true;
                }
                if (csane_top_is(stream, j, ";")) {
                    break;
                }
            }
        }
    }
    return false;
}

static bool
csane_top_macro_statement(CsaneTopStream *stream, int32 first,
                          int32 end) {
    if (!csane_top_macro_name(csane_top_token(stream, first))
        || !csane_top_is(stream, first + 1, "(")) {
        return false;
    }
    int32 close = stream->matches[first + 1];
    return close >= first + 2
           && (close + 1 == end
               || (close + 2 == end && csane_top_is(stream, end - 1, ";")));
}

static enum CsaneTopKind
csane_top_classify(CsaneTopStream *stream, int32 first, int32 end,
                   int32 *name) {
    *name = -1;
    if (first >= end) {
        return CSANE_TOP_OPAQUE;
    }
    if (csane_top_macro_statement(stream, first, end)) {
        *name = first;
        return CSANE_TOP_MACRO_INVOCATION;
    }
    if (csane_top_type(stream, first, end)) {
        return CSANE_TOP_TYPE_DECLARATION;
    }
    if (csane_top_is_function(stream, first, end - 1, name)
        && csane_top_is(stream, end - 1, ";")) {
        return CSANE_TOP_FUNCTION_DECLARATION;
    }
    if (csane_top_is(stream, end - 1, ";")) {
        return CSANE_TOP_DECLARATION;
    }
    return CSANE_TOP_OPAQUE;
}

static bool
csane_top_new_line(CsaneTopStream *stream, int32 a, int32 b) {
    Token *first = csane_top_token(stream, a);
    Token *next = csane_top_token(stream, b);
    Tokenization *tokens = stream->top->regions->tokens;
    for (int32 i = first->offset + first->len; i < next->offset; i += 1) {
        if (tokens->text[i] == '\n') {
            return true;
        }
    }
    return false;
}

static bool
csane_top_macro_boundary(CsaneTopStream *stream, int32 first,
                         int32 close) {
    if (stream->matches[first + 1] != close
        || !csane_top_macro_name(csane_top_token(stream, first))) {
        return false;
    }
    int32 next = close + 1;
    if (next >= stream->count) {
        return true;
    }
    if (csane_top_is(stream, next, ";")
        || csane_top_is(stream, next, "(")
        || csane_top_is(stream, next, "{")
        || csane_top_is(stream, next, ",")) {
        return false;
    }
    return csane_top_new_line(stream, close, next);
}

/* A damaged top-level construct may have no matching delimiter. Recover
 * first at a semicolon (for an incomplete expression), or at a new function
 * definition beginning at column zero (for an unclosed function body).
 * Without a credible boundary, only the current code leaf is abandoned. */
static int32
csane_top_recover(CsaneTopStream *stream, int32 first, int32 broken) {
    int32 leaf_end = broken + 1;
    while (leaf_end < stream->count
           && stream->leaf[leaf_end] == stream->leaf[broken]) {
        leaf_end += 1;
    }
    if (!csane_top_is(stream, broken, "{")) {
        for (int32 i = broken + 1; i < leaf_end; i += 1) {
            if (csane_top_is(stream, i, ";")) {
                return i + 1;
            }
            if (stream->matches[i] > i) {
                i = stream->matches[i];
            }
        }
    }
    for (int32 i = broken + 1; i < stream->count; i += 1) {
        int32 offset = csane_top_token(stream, i)->offset;
        Tokenization *tokens = stream->top->regions->tokens;
        if (offset > 0 && tokens->text[offset - 1] != '\n') {
            continue;
        }
        for (int32 j = i + 1; j < stream->count; j += 1) {
            if (csane_top_is(stream, j, ";")) {
                break;
            }
            if (csane_top_is(stream, j, "{") && stream->matches[j] > j) {
                int32 name = -1;
                if (csane_top_is_function(stream, i, j, &name)) {
                    return i;
                }
                break;
            }
            if (stream->matches[j] > j) {
                j = stream->matches[j];
            }
        }
    }
    return leaf_end > first ? leaf_end : first + 1;
}

static void
csane_top_scan(CsaneTopStream *stream) {
    int32 first = 0;
    while (first < stream->count) {
        int32 name = -1;
        bool emitted = false;
        for (int32 i = first; i < stream->count; i += 1) {
            if (i == first + 2 && csane_top_is(stream, first, "extern")
                && csane_top_is(stream, first + 1, "\"C\"")
                && csane_top_is(stream, i, "{")
                && stream->matches[i] < 0
                && stream->top->regions->items[stream->owner].kind
                   == CSANE_REGION_CONDITIONAL_BRANCH) {
                csane_top_append(stream, CSANE_TOP_LINKAGE_FRAGMENT,
                                 first, i + 1, -1, -1);
                first = i + 1;
                emitted = true;
                break;
            }
            if (i == first && csane_top_is(stream, i, "}")
                && stream->top->regions->items[stream->owner].kind
                   == CSANE_REGION_CONDITIONAL_BRANCH) {
                csane_top_append(stream, CSANE_TOP_LINKAGE_FRAGMENT,
                                 first, i + 1, -1, -1);
                first = i + 1;
                emitted = true;
                break;
            }
            if (csane_top_open(stream, i)) {
                if (stream->matches[i] < 0) {
                    /* A fragment spanning an unbalanced preprocessor
                     * boundary must not swallow the following code leaf. */
                    int32 end = csane_top_recover(stream, first, i);
                    csane_top_append(stream, CSANE_TOP_OPAQUE, first, end,
                                     -1, -1);
                    first = end;
                    emitted = true;
                    break;
                }
                if (csane_top_is(stream, i, "{")
                    && csane_top_is_function(stream, first, i, &name)) {
                    int32 end = stream->matches[i] + 1;
                    csane_top_append(stream, CSANE_TOP_FUNCTION_DEFINITION,
                                     first, end, i, name);
                    first = end;
                    emitted = true;
                    break;
                }
                if (i == first + 1 && csane_top_is(stream, i, "(")
                    && csane_top_macro_boundary(stream, first,
                                                stream->matches[i])) {
                    int32 end = stream->matches[i] + 1;
                    csane_top_append(stream, CSANE_TOP_MACRO_INVOCATION,
                                     first, end, -1, first);
                    first = end;
                    emitted = true;
                    break;
                }
                i = stream->matches[i];
            } else if (csane_top_close(stream, i)) {
                csane_top_append(stream, CSANE_TOP_OPAQUE, first, i + 1,
                                 -1, -1);
                first = i + 1;
                emitted = true;
                break;
            } else if (csane_top_is(stream, i, ";")) {
                enum CsaneTopKind kind =
                    csane_top_classify(stream, first, i + 1, &name);
                csane_top_append(stream, kind, first, i + 1, -1, name);
                first = i + 1;
                emitted = true;
                break;
            } else if (i == first
                       && csane_top_macro_name(csane_top_token(stream, i))
                       && (i + 1 == stream->count
                           || (csane_top_new_line(stream, i, i + 1)
                               && !csane_top_is(stream, i + 1, "(")))) {
                csane_top_append(stream, CSANE_TOP_MACRO_INVOCATION,
                                 first, i + 1, -1, first);
                first = i + 1;
                emitted = true;
                break;
            }
        }
        if (!emitted) {
            csane_top_append(stream, CSANE_TOP_OPAQUE, first,
                             stream->count, -1, -1);
            break;
        }
    }
    return;
}

/* Locate the narrowest previously recognized enclosing declaration.
 * Function headers and bodies are distinguished below; neither requires
 * parsing or selecting a preprocessor alternative. */
static CsaneTopItem *
csane_top_enclosing(CsaneTopStream *stream) {
    CsaneRegions *regions = stream->top->regions;
    SourceRange source = regions->items[stream->owner].source;
    CsaneTopItem *best = NULL;
    for (int32 i = 0; i < stream->top->count; i += 1) {
        CsaneTopItem *item = &stream->top->items[i];
        if (item->kind == CSANE_TOP_OPAQUE
            || item->kind == CSANE_TOP_MACRO_INVOCATION
            || item->kind == CSANE_TOP_MEMBER_DECLARATION
            || item->kind == CSANE_TOP_ENUMERATOR
            || item->kind == CSANE_TOP_PARAMETER_FRAGMENT) {
            continue;
        }
        if (source.start < item->source.start
            || source.end > item->source.end) {
            continue;
        }
        if (best == NULL || item->source.end - item->source.start
                            < best->source.end - best->source.start) {
            best = item;
        }
    }
    return best;
}

/* Find an aggregate's opening brace before the branch. The enclosing
 * type declaration must itself have been recognized by the parent stream.
 * Other compound constructs inside its body are not guessed to be fields. */
static enum CsaneTopKind
csane_top_aggregate(CsaneTopStream *stream, CsaneTopItem *item) {
    Tokenization *tokens = stream->top->regions->tokens;
    int32 offset = stream->top->regions->items[stream->owner].source.start;
    bool struct_or_union = false;
    bool is_enum = false;
    for (int32 i = item->tokens.first; i < item->tokens.end; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token->offset >= offset) {
            break;
        }
        if (TOKEN_IS(token, "struct") || TOKEN_IS(token, "union")) {
            struct_or_union = true;
            is_enum = false;
        } else if (TOKEN_IS(token, "enum")) {
            struct_or_union = false;
            is_enum = true;
        } else if (TOKEN_IS(token, "{")) {
            if (struct_or_union || is_enum) {
                return is_enum ? CSANE_TOP_ENUMERATOR
                               : CSANE_TOP_MEMBER_DECLARATION;
            }
            break;
        } else if (TOKEN_IS(token, ";")) {
            struct_or_union = false;
            is_enum = false;
        }
    }
    return CSANE_TOP_OPAQUE;
}

/* A fragment of a function header must be enclosed by a real parameter
 * pair. In particular, a guard before the return type is not a parameter. */
static bool
csane_top_parameters(CsaneTopStream *stream, CsaneTopItem *item) {
    Tokenization *tokens = stream->top->regions->tokens;
    SourceRange source = stream->top->regions->items[stream->owner].source;
    int32 limit = item->kind == CSANE_TOP_FUNCTION_DEFINITION
                  ? item->body.first : item->header.end;
    CsaneTokenView view = csane_tokens_full(tokens);
    for (int32 i = item->header.first; i < limit; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token->offset >= source.start) {
            break;
        }
        if (!TOKEN_IS(token, "(")) {
            continue;
        }
        int32 close = csane_tokens_match(&view, i);
        if (close >= 0 && close < limit
            && tokens->tokens[close].offset >= source.end) {
            return true;
        }
    }
    return false;
}

/* Do not interpret a damaged branch as a valid declaration. The fragment
 * scanner checks bounded, independently balanced units, retaining any
 * unrecognized remainder as OPAQUE for accurate rule coverage. */
static bool
csane_top_fragment_valid(CsaneTopStream *stream, int32 first, int32 end,
                         enum CsaneTopKind kind, bool terminated) {
    if (first >= end) {
        return kind == CSANE_TOP_PARAMETER_FRAGMENT;
    }
    if (csane_top_token(stream, first)->kind != TOKEN_IDENT
        || csane_top_is(stream, first, "return")
        || csane_top_is(stream, first, "if")
        || csane_top_is(stream, first, "for")
        || csane_top_is(stream, first, "while")
        || csane_top_is(stream, first, "switch")
        || csane_top_is(stream, first, "case")) {
        return false;
    }
    int32 identifiers = 0;
    for (int32 i = first; i < end; i += 1) {
        if (csane_top_token(stream, i)->kind == TOKEN_IDENT) {
            identifiers += 1;
        }
        if (csane_top_open(stream, i)) {
            if (stream->matches[i] <= i || stream->matches[i] >= end) {
                return false;
            }
            i = stream->matches[i];
        } else if (csane_top_close(stream, i)) {
            return false;
        }
        if (csane_top_is(stream, i, ";")) {
            return false;
        }
    }
    /* Count identifiers inside balanced groups, including pointer
     * declarators like void (*callback)(int32). */
    identifiers = 0;
    for (int32 i = first; i < end; i += 1) {
        identifiers += csane_top_token(stream, i)->kind == TOKEN_IDENT;
    }
    if (kind == CSANE_TOP_MEMBER_DECLARATION) {
        return identifiers >= 2;
    }
    if (kind == CSANE_TOP_ENUMERATOR) {
        /* A lone macro name may expand to an unknown number of entries. */
        return terminated || identifiers > 1 || end > first + 1;
    }
    return true;
}

static void
csane_top_fragments(CsaneTopStream *stream, enum CsaneTopKind kind) {
    char *separator = kind == CSANE_TOP_MEMBER_DECLARATION ? ";" : ",";
    int32 first = 0;
    while (first < stream->count) {
        int32 begin = first;
        int32 end = stream->count;
        bool terminated = false;
        int32 scan = first;
        if (kind == CSANE_TOP_PARAMETER_FRAGMENT
            && csane_top_is(stream, scan, ",")) {
            scan += 1;
        }
        for (int32 i = scan; i < stream->count; i += 1) {
            if (csane_top_open(stream, i)) {
                if (stream->matches[i] <= i) {
                    end = i + 1;
                    break;
                }
                i = stream->matches[i];
            } else if (csane_top_close(stream, i)) {
                end = i + 1;
                break;
            } else if (csane_top_is(stream, i, separator)) {
                end = i + 1;
                terminated = true;
                break;
            }
        }
        int32 content_first = begin;
        int32 content_end = terminated ? end - 1 : end;
        if (kind == CSANE_TOP_PARAMETER_FRAGMENT
            && csane_top_is(stream, content_first, ",")) {
            content_first += 1;
        }
        bool complete = kind != CSANE_TOP_MEMBER_DECLARATION || terminated;
        enum CsaneTopKind result =
            complete && csane_top_fragment_valid(stream, content_first,
                                                 content_end, kind,
                                                 terminated)
            ? kind : CSANE_TOP_OPAQUE;
        csane_top_append(stream, result, begin, end, -1, -1);
        first = end;
    }
}

static void
csane_top_owner(CsaneTopLevels *top, int32 owner) {
    CsaneTopStream stream = {.top = top, .owner = owner};
    csane_top_collect(&stream);
    csane_top_match(&stream);
    CsaneTopItem *enclosing = csane_top_enclosing(&stream);
    if (enclosing == NULL) {
        csane_top_scan(&stream);
    } else if (enclosing->kind == CSANE_TOP_FUNCTION_DEFINITION
               || enclosing->kind == CSANE_TOP_FUNCTION_DECLARATION) {
        Tokenization *tokens = top->regions->tokens;
        bool in_body = enclosing->kind == CSANE_TOP_FUNCTION_DEFINITION
                       && top->regions->items[owner].source.start
                          >= tokens->tokens[enclosing->body.first].offset;
        if (!in_body) {
            if (csane_top_parameters(&stream, enclosing)) {
                csane_top_fragments(&stream, CSANE_TOP_PARAMETER_FRAGMENT);
            } else {
                csane_top_append(&stream, CSANE_TOP_OPAQUE,
                                 0, stream.count, -1, -1);
            }
        }
        /* Complete body alternatives belong to the statement scanner. */
    } else if (enclosing->kind == CSANE_TOP_TYPE_DECLARATION) {
        enum CsaneTopKind kind = csane_top_aggregate(&stream, enclosing);
        if (kind == CSANE_TOP_OPAQUE) {
            csane_top_append(&stream, kind, 0, stream.count, -1, -1);
        } else {
            csane_top_fragments(&stream, kind);
        }
    } else {
        csane_top_append(&stream, CSANE_TOP_OPAQUE, 0, stream.count, -1, -1);
    }
    free2(stream.stack, (int64)stream.count*SIZEOF(*stream.stack));
    free2(stream.matches, (int64)stream.count*SIZEOF(*stream.matches));
    free2(stream.indices, (int64)stream.capacity*SIZEOF(*stream.indices));
    free2(stream.leaf, (int64)stream.capacity*SIZEOF(*stream.leaf));
    return;
}

/* Only recognize a wrapper when the fragment belongs to a complete
 * #if __cplusplus conditional. Other unmatched braces remain unresolved. */
static bool
csane_top_cpp_guard(CsaneTopLevels *top, CsaneTopItem *item) {
    CsaneRegions *regions = top->regions;
    CsaneSourceRegion *region = &regions->items[item->owner_region];
    CPreprocessorAnalysis *pp = regions->preprocessor;
    if (region->kind != CSANE_REGION_CONDITIONAL_BRANCH
        || region->branch_index < 0 || region->group_index < 0
        || !region->complete) {
        return false;
    }
    CPreprocessorGroup *group = &pp->groups[region->group_index];
    if (region->branch_index != group->first_branch
        || group->endif_directive < 0) {
        return false;
    }
    int32 directive = pp->branches[group->first_branch].directive_index;
    SourceRange source = pp->directives[directive].source;
    char *text = regions->tokens->text + source.start;
    return memmem64(text, source.end - source.start,
                    "__cplusplus", strlen32("__cplusplus")) != NULL;
}

/* Opening and closing extern "C" braces can live in separate conditional
 * groups. A missing counterpart remains opaque instead of contributing
 * false coverage. This is a guarded linkage idiom, not general brace
 * balancing across independently compiled preprocessor alternatives. */
static void
csane_top_linkage_pairs(CsaneTopLevels *top) {
    int32 pending = -1;
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->kind != CSANE_TOP_LINKAGE_FRAGMENT) {
            continue;
        }
        Token *token = &top->regions->tokens->tokens[item->tokens.first];
        bool guarded = csane_top_cpp_guard(top, item);
        if (TOKEN_IS(token, "extern")) {
            if (pending >= 0) {
                top->items[pending].kind = CSANE_TOP_OPAQUE;
                top->opaque_count += 1;
            }
            pending = guarded ? i : -1;
            if (!guarded) {
                item->kind = CSANE_TOP_OPAQUE;
                top->opaque_count += 1;
            }
        } else if (pending >= 0 && guarded
                   && item->source.start > top->items[pending].source.end) {
            pending = -1;
        } else {
            item->kind = CSANE_TOP_OPAQUE;
            top->opaque_count += 1;
        }
    }
    if (pending >= 0) {
        top->items[pending].kind = CSANE_TOP_OPAQUE;
        top->opaque_count += 1;
    }
    return;
}

bool
csane_toplevel_init(CsaneTopLevels *top, CsaneRegions *regions) {
    if (top == NULL) {
        return false;
    }
    *top = (CsaneTopLevels){0};
    if (regions == NULL || regions->tokens == NULL || regions->count <= 0) {
        return false;
    }
    top->regions = regions;
    /* Iteration is parent-first because Step 3 appends regions in that order.
     * All conditional alternatives are scanned, never selected. */
    for (int32 i = 0; i < regions->count; i += 1) {
        enum CsaneRegionKind kind = regions->items[i].kind;
        if (kind == CSANE_REGION_FILE
            || kind == CSANE_REGION_CONDITIONAL_BRANCH) {
            csane_top_owner(top, i);
        }
    }
    csane_top_linkage_pairs(top);
    return true;
}

void
csane_toplevel_free(CsaneTopLevels *top) {
    if (top == NULL) {
        return;
    }
    free2(top->items, (int64)top->capacity*SIZEOF(*top->items));
    *top = (CsaneTopLevels){0};
    return;
}

int32
csane_toplevel_at_token(CsaneTopLevels *top, int32 owner, int32 token) {
    if (top == NULL || top->regions == NULL || token < 0
        || token >= top->regions->tokens->token_count || owner < 0
        || owner >= top->regions->count) {
        return -1;
    }
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->owner_region == owner && token >= item->tokens.first
            && token < item->tokens.end) {
            return i;
        }
    }
    return -1;
}

#endif /* CSANECHECK_TOPLEVEL_C */
