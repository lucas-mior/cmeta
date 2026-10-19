// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATEMENTS_C)
#define CSANECHECK_STATEMENTS_C

#include "cbase.h"
#include "csanecheck_statements.h"

typedef struct CsaneStatementStream {
    CsaneStatements *statements;
    int32 function;
    int32 owner;
    int32 *indices;
    int32 *matches;
    int32 count;
    int32 capacity;
} CsaneStatementStream;

static Token *
csane_stmt_token(CsaneStatementStream *stream, int32 position) {
    return &stream->statements->toplevel->regions->tokens->tokens[
        stream->indices[position]];
}

static bool
csane_stmt_is(CsaneStatementStream *stream, int32 position, char *text) {
    return position >= 0 && position < stream->count
           && TOKEN_IS(csane_stmt_token(stream, position), text);
}

static int32
csane_stmt_append(CsaneStatementStream *stream, enum CsaneStatementKind kind,
                  int32 first, int32 end, int32 parent, int32 header_end) {
    if (first >= end) {
        return -1;
    }
    CsaneStatements *statements = stream->statements;
    if (statements->count == statements->capacity) {
        int32 next = statements->capacity ? statements->capacity*2 : 64;
        statements->items = realloc2(statements->items, statements->capacity,
                                     next, SIZEOF(*statements->items));
        statements->capacity = next;
    }
    int32 id = statements->count;
    statements->count += 1;
    Token *start = csane_stmt_token(stream, first);
    Token *last = csane_stmt_token(stream, end - 1);
    CsaneStatement *item = &statements->items[id];
    *item = (CsaneStatement){
        .kind = kind,
        .source = {start->offset, last->offset + last->len},
        .tokens = {stream->indices[first], stream->indices[end - 1] + 1},
        .header = header_end > first
                  ? (TokenRange){stream->indices[first],
                                 stream->indices[header_end - 1] + 1}
                  : (TokenRange){0},
        .function = stream->function,
        .owner_region = stream->owner,
        .parent = parent,
        .first_child = -1,
        .last_child = -1,
        .next_sibling = -1,
        .body = -1,
        .alternative = -1,
    };
    if (kind == CSANE_STMT_OPAQUE) {
        statements->opaque_count += 1;
    }
    if (parent >= 0) {
        CsaneStatement *owner = &statements->items[parent];
        if (owner->last_child >= 0) {
            statements->items[owner->last_child].next_sibling = id;
        } else {
            owner->first_child = id;
        }
        owner->last_child = id;
    }
    return id;
}

static bool
csane_stmt_pair(CsaneStatementStream *stream, int32 a, int32 b) {
    return (csane_stmt_is(stream, a, "(") && csane_stmt_is(stream, b, ")"))
           || (csane_stmt_is(stream, a, "[")
               && csane_stmt_is(stream, b, "]"))
           || (csane_stmt_is(stream, a, "{")
               && csane_stmt_is(stream, b, "}"));
}

static bool
csane_stmt_open(CsaneStatementStream *stream, int32 i) {
    return csane_stmt_is(stream, i, "(") || csane_stmt_is(stream, i, "[")
           || csane_stmt_is(stream, i, "{");
}

static bool
csane_stmt_close(CsaneStatementStream *stream, int32 i) {
    return csane_stmt_is(stream, i, ")") || csane_stmt_is(stream, i, "]")
           || csane_stmt_is(stream, i, "}");
}

static void
csane_stmt_match(CsaneStatementStream *stream) {
    int32 *stack = malloc2((int64)stream->count*SIZEOF(*stack));
    stream->matches = malloc2((int64)stream->count*SIZEOF(*stream->matches));
    int32 depth = 0;
    for (int32 i = 0; i < stream->count; i += 1) {
        stream->matches[i] = -1;
        if (csane_stmt_open(stream, i)) {
            stack[depth] = i;
            depth += 1;
        } else if (csane_stmt_close(stream, i)) {
            if (depth > 0 && csane_stmt_pair(stream, stack[depth - 1], i)) {
                int32 open = stack[depth - 1];
                stream->matches[open] = i;
                stream->matches[i] = open;
                depth -= 1;
            } else {
                /* Separate preprocessor fragments may be incomplete. */
                depth = 0;
            }
        }
    }
    free2(stack, (int64)stream->count*SIZEOF(*stack));
}

static enum CsaneStatementKind
csane_stmt_control(CsaneStatementStream *stream, int32 i) {
    if (csane_stmt_is(stream, i, "if")) {
        return CSANE_STMT_IF;
    }
    if (csane_stmt_is(stream, i, "else")) {
        return CSANE_STMT_ELSE;
    }
    if (csane_stmt_is(stream, i, "for")) {
        return CSANE_STMT_FOR;
    }
    if (csane_stmt_is(stream, i, "while")) {
        return CSANE_STMT_WHILE;
    }
    if (csane_stmt_is(stream, i, "do")) {
        return CSANE_STMT_DO;
    }
    if (csane_stmt_is(stream, i, "switch")) {
        return CSANE_STMT_SWITCH;
    }
    return CSANE_STMT_SIMPLE;
}

/* Scan one statement and return its next token. Every failure advances by
 * at least one token; failure is not propagated to unrelated statements. */
static int32
csane_stmt_parse(CsaneStatementStream *stream, int32 first, int32 limit,
                 int32 parent, int32 *result, int32 depth);

static void
csane_stmt_children(CsaneStatementStream *stream, int32 first, int32 end,
                    int32 parent, int32 depth) {
    int32 pos = first;
    while (pos < end) {
        int32 id = -1;
        int32 next = csane_stmt_parse(stream, pos, end, parent, &id, depth);
        if (next <= pos) {
            next = pos + 1;
        }
        pos = next;
    }
}

static int32
csane_stmt_parse(CsaneStatementStream *stream, int32 first, int32 limit,
                 int32 parent, int32 *result, int32 depth) {
    *result = -1;
    if (first >= limit) {
        return limit;
    }
    if (depth > 256) {
        *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE, first,
                                    limit, parent, first);
        return limit;
    }
    if (csane_stmt_is(stream, first, "{")) {
        int32 close = stream->matches[first];
        if (close < first || close >= limit) {
            *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE, first,
                                        limit, parent, first);
            return limit;
        }
        *result = csane_stmt_append(stream, CSANE_STMT_BLOCK, first,
                                    close + 1, parent, first + 1);
        csane_stmt_children(stream, first + 1, close, *result, depth + 1);
        return close + 1;
    }
    if (csane_stmt_is(stream, first, "case")
        || csane_stmt_is(stream, first, "default")) {
        for (int32 i = first + 1; i < limit; i += 1) {
            if (csane_stmt_is(stream, i, ":")) {
                bool is_case = csane_stmt_is(stream, first, "case");
                enum CsaneStatementKind kind = is_case ? CSANE_STMT_CASE
                                                      : CSANE_STMT_DEFAULT;
                *result = csane_stmt_append(stream, kind, first, i + 1,
                                            parent, i + 1);
                return i + 1;
            }
            if (stream->matches[i] > i) {
                i = stream->matches[i];
            }
        }
    }
    if (csane_stmt_token(stream, first)->kind == TOKEN_IDENT
        && csane_stmt_is(stream, first + 1, ":")) {
        *result = csane_stmt_append(stream, CSANE_STMT_LABEL, first,
                                    first + 2, parent, first + 2);
        return first + 2;
    }
    enum CsaneStatementKind control = csane_stmt_control(stream, first);
    if (control != CSANE_STMT_SIMPLE) {
        int32 header_end = first + 1;
        if (control != CSANE_STMT_ELSE && control != CSANE_STMT_DO) {
            if (!csane_stmt_is(stream, header_end, "(")
                || stream->matches[header_end] < header_end) {
                *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE,
                                            first, header_end, parent, first);
                return header_end;
            }
            header_end = stream->matches[header_end] + 1;
        }
        int32 id = csane_stmt_append(stream, control, first, header_end,
                                     parent, header_end);
        int32 body = -1;
        int32 end = csane_stmt_parse(stream, header_end, limit, id,
                                     &body, depth + 1);
        /* realloc2 during recursive parsing can move the item array. */
        stream->statements->items[id].body = body;
        if (control == CSANE_STMT_IF && csane_stmt_is(stream, end, "else")) {
            int32 alternative = -1;
            end = csane_stmt_parse(stream, end, limit, id,
                                   &alternative, depth + 1);
            stream->statements->items[id].alternative = alternative;
        }
        if (control == CSANE_STMT_DO && csane_stmt_is(stream, end, "while")) {
            int32 tail = end + 1;
            if (csane_stmt_is(stream, tail, "(")
                && stream->matches[tail] > tail) {
                tail = stream->matches[tail] + 1;
                if (csane_stmt_is(stream, tail, ";")) {
                    tail += 1;
                }
                end = tail;
            }
        }
        Token *last = csane_stmt_token(stream, end > first ? end - 1 : first);
        stream->statements->items[id].tokens.end = stream->indices[
            end > first ? end - 1 : first] + 1;
        stream->statements->items[id].source.end = last->offset + last->len;
        *result = id;
        return end;
    }
    for (int32 i = first; i < limit; i += 1) {
        if (csane_stmt_is(stream, i, ";")) {
            *result = csane_stmt_append(stream, CSANE_STMT_SIMPLE, first,
                                        i + 1, parent, first);
            return i + 1;
        }
        if (stream->matches[i] > i) {
            i = stream->matches[i];
        } else if (csane_stmt_is(stream, i, "}")) {
            break;
        }
    }
    *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE, first,
                                limit, parent, first);
    return limit;
}

static void
csane_stmt_push(CsaneStatementStream *stream, int32 index) {
    if (stream->count == stream->capacity) {
        int32 next = stream->capacity ? stream->capacity*2 : 128;
        stream->indices = realloc2(stream->indices, stream->capacity,
                                   next, SIZEOF(*stream->indices));
        stream->capacity = next;
    }
    stream->indices[stream->count] = index;
    stream->count += 1;
}

static int32
csane_stmt_owner(CsaneRegions *regions, int32 region) {
    for (int32 i = region; i >= 0; i = regions->items[i].parent) {
        if (regions->items[i].kind == CSANE_REGION_CONDITIONAL_BRANCH
            || regions->items[i].kind == CSANE_REGION_FILE) {
            return i;
        }
    }
    return -1;
}

static void
csane_stmt_stream(CsaneStatements *statements, int32 function,
                  int32 owner) {
    CsaneTopItem *top = &statements->toplevel->items[function];
    CsaneRegions *regions = statements->toplevel->regions;
    Tokenization *tokens = regions->tokens;
    CsaneStatementStream stream = {.statements = statements,
                                   .function = function, .owner = owner};
    for (int32 i = top->body.first; i < top->body.end; i += 1) {
        Token *token = &tokens->tokens[i];
        if (token_is_trivia(token) || token->kind == TOKEN_PREPROC) {
            continue;
        }
        int32 region = csane_regions_at_token(regions, i);
        if (region < 0 || regions->items[region].kind != CSANE_REGION_CODE
            || csane_stmt_owner(regions, region) != owner) {
            continue;
        }
        csane_stmt_push(&stream, i);
    }
    if (stream.count > 0) {
        csane_stmt_match(&stream);
        /* A function's outermost braces belong to the base stream. Branch
         * streams may contain only fragments of the function body. */
        int32 first = 0;
        int32 end = stream.count;
        if (owner == top->owner_region
            && csane_stmt_is(&stream, first, "{")
            && csane_stmt_is(&stream, end - 1, "}")) {
            first += 1;
            end -= 1;
        }
        csane_stmt_children(&stream, first, end, -1, 0);
    }
    free2(stream.indices, (int64)stream.capacity*SIZEOF(*stream.indices));
    free2(stream.matches, (int64)stream.count*SIZEOF(*stream.matches));
}

bool
csane_statements_init(CsaneStatements *statements, CsaneTopLevels *top) {
    if (statements == NULL) {
        return false;
    }
    *statements = (CsaneStatements){0};
    if (top == NULL || top->regions == NULL || top->regions->tokens == NULL) {
        return false;
    }
    statements->toplevel = top;
    for (int32 i = 0; i < top->count; i += 1) {
        if (top->items[i].kind != CSANE_TOP_FUNCTION_DEFINITION) {
            continue;
        }
        csane_stmt_stream(statements, i, top->items[i].owner_region);
        CsaneRegions *regions = top->regions;
        for (int32 r = 0; r < regions->count; r += 1) {
            if (regions->items[r].kind != CSANE_REGION_CONDITIONAL_BRANCH) {
                continue;
            }
            SourceRange span = regions->items[r].source;
            SourceRange body = top->items[i].source;
            if (span.start >= body.start && span.end <= body.end) {
                csane_stmt_stream(statements, i, r);
            }
        }
    }
    return true;
}

void
csane_statements_free(CsaneStatements *statements) {
    if (statements == NULL) {
        return;
    }
    free2(statements->items,
          (int64)statements->capacity*SIZEOF(*statements->items));
    *statements = (CsaneStatements){0};
}

int32
csane_statements_at_token(CsaneStatements *statements, int32 function,
                          int32 token) {
    if (statements == NULL || statements->toplevel == NULL) {
        return -1;
    }
    Tokenization *tokens = statements->toplevel->regions->tokens;
    if (token < 0 || token >= tokens->token_count) {
        return -1;
    }
    int32 offset = tokens->tokens[token].offset;
    int32 best = -1;
    for (int32 i = 0; i < statements->count; i += 1) {
        CsaneStatement *item = &statements->items[i];
        if (item->function != function || offset < item->source.start
            || offset >= item->source.end) {
            continue;
        }
        if (best < 0 || item->source.end - item->source.start
                        < statements->items[best].source.end
                          - statements->items[best].source.start) {
            best = i;
        }
    }
    return best;
}

#endif /* CSANECHECK_STATEMENTS_C */
