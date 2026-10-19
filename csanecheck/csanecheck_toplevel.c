// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_TOPLEVEL_C)
#define CSANECHECK_TOPLEVEL_C

#include "cbase.h"
#include "csanecheck_toplevel.h"

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
    int32 child = regions->items[stream->owner].first_child;
    for (; child >= 0; child = regions->items[child].next_sibling) {
        CsaneSourceRegion *region = &regions->items[child];
        if (region->kind != CSANE_REGION_CODE) {
            continue;
        }
        for (int32 i = region->tokens.first; i < region->tokens.end;
             i += 1) {
            Token *token = &tokens->tokens[i];
            if (!token_is_trivia(token) && token->kind != TOKEN_PREPROC
                && token->offset >= region->source.start
                && token->offset < region->source.end) {
                csane_top_push(stream, i, child);
            }
        }
    }
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
        } else if (csane_top_close(stream, i) && depth > 0
                   && csane_top_pair(stream, stream->stack[depth - 1], i)) {
            int32 open = stream->stack[depth - 1];
            stream->matches[open] = i;
            stream->matches[i] = open;
            depth -= 1;
        } else if (csane_top_close(stream, i)) {
            /* Do not let a mismatched preprocessor fragment consume later
             * independent top-level declarations. */
            depth = 0;
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

/* Return the function-name token, or -1. CAT(...)(...) is deliberately
 * supported: its name is the CAT macro, not an expanded identifier. */
static int32
csane_top_function_name(CsaneTopStream *stream, int32 first, int32 end) {
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
    /* A declaration of a function pointer is not a function prototype. */
    if (csane_top_is(stream, *name - 1, "*")) {
        return false;
    }
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

static void
csane_top_scan(CsaneTopStream *stream) {
    int32 first = 0;
    while (first < stream->count) {
        int32 name = -1;
        bool emitted = false;
        for (int32 i = first; i < stream->count; i += 1) {
            if (csane_top_open(stream, i)) {
                if (stream->matches[i] < 0) {
                    /* A fragment spanning an unbalanced preprocessor
                     * boundary must not swallow the following code leaf. */
                    int32 end = i + 1;
                    while (end < stream->count
                           && stream->leaf[end] == stream->leaf[i]) {
                        end += 1;
                    }
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

/* A conditional branch may be embedded inside a function, type, or
 * declaration. Its statements must not be misreported as file-scope
 * declarations merely because the enclosing directive was split out. */
static bool
csane_top_nested(CsaneTopStream *stream) {
    CsaneRegions *regions = stream->top->regions;
    SourceRange source = regions->items[stream->owner].source;
    Tokenization *tokens = regions->tokens;
    for (int32 i = 0; i < stream->top->count; i += 1) {
        CsaneTopItem *item = &stream->top->items[i];
        if (item->kind == CSANE_TOP_OPAQUE
            || item->kind == CSANE_TOP_MACRO_INVOCATION) {
            continue;
        }
        SourceRange enclosing = item->source;
        if (item->kind == CSANE_TOP_FUNCTION_DEFINITION) {
            Token *brace = &tokens->tokens[item->body.first];
            enclosing.start = brace->offset;
        }
        if (source.start >= enclosing.start && source.end <= enclosing.end) {
            return true;
        }
    }
    return false;
}

static void
csane_top_owner(CsaneTopLevels *top, int32 owner) {
    CsaneTopStream stream = {.top = top, .owner = owner};
    csane_top_collect(&stream);
    csane_top_match(&stream);
    if (csane_top_nested(&stream)) {
        csane_top_append(&stream, CSANE_TOP_OPAQUE, 0, stream.count, -1, -1);
    } else {
        csane_top_scan(&stream);
    }
    free2(stream.stack, (int64)stream.count*SIZEOF(*stream.stack));
    free2(stream.matches, (int64)stream.count*SIZEOF(*stream.matches));
    free2(stream.indices, (int64)stream.capacity*SIZEOF(*stream.indices));
    free2(stream.leaf, (int64)stream.capacity*SIZEOF(*stream.leaf));
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
