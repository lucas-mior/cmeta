// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_STATEMENTS_C)
#define CSANECHECK_STATEMENTS_C

#include "cbase.h"
#include "csck_statements.h"
#include "csck_tokens.h"

typedef struct CsaneStatementStream {
    CsaneStatements *statements;
    int32 function;
    int32 owner;
    int32 *indices;
    int32 *matches;
    int32 count;
    int32 capacity;
    bool focus_branch;
    SourceRange focus_source;
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
    CsaneStatements *statements;
    CsaneStatement *item;
    CsaneStatement *owner;
    SourceRange source;
    Token *start;
    Token *last;
    int32 id;
    int32 next;
    if (first >= end) {
        return -1;
    }
    start = csane_stmt_token(stream, first);
    last = csane_stmt_token(stream, end - 1);
    source = (SourceRange){start->offset, last->offset + last->len};
    statements = stream->statements;
    if (stream->focus_branch
        && (source.end <= stream->focus_source.start
            || source.start >= stream->focus_source.end)) {
        bool parent_focused = parent >= 0
                              && statements->items[parent].source.end
                                 > stream->focus_source.start
                              && statements->items[parent].source.start
                                 < stream->focus_source.end;
        if (!parent_focused) {
            return -1;
        }
    }
    if (statements->count == statements->capacity) {
        next = statements->capacity ? statements->capacity*2 : 64;
        statements->items = realloc2(statements->items, statements->capacity,
                                     next, SIZEOF(*statements->items));
        statements->capacity = next;
    }
    id = statements->count;
    statements->count += 1;
    item = &statements->items[id];
    *item = (CsaneStatement){
        .kind = kind,
        .source = source,
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
        owner = &statements->items[parent];
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

static bool csane_stmt_branch_needs_context(CsaneRegions *, int32);

static bool
csane_stmt_path_has_branch(CsaneRegions *regions, int32 branch, int32 owner) {
    for (int32 r = owner; r >= 0; r = regions->items[r].parent) {
        if (r == branch) {
            return true;
        }
    }
    return false;
}

static bool
csane_stmt_path_selects(CsaneRegions *regions, int32 region, int32 owner) {
    for (int32 r = region; r >= 0; r = regions->items[r].parent) {
        if (regions->items[r].kind == CSANE_REGION_CONDITIONAL_BRANCH
            && !csane_stmt_path_has_branch(regions, r, owner)) {
            return false;
        }
    }
    return true;
}

static bool
csane_stmt_branch_owns_close(CsaneStatementStream *stream, int32 outer_pos,
                             int32 close_pos) {
    CsaneRegions *regions = stream->statements->toplevel->regions;
    Tokenization *tokens = regions->tokens;
    int32 outer = stream->indices[outer_pos];
    int32 close = stream->indices[close_pos];
    TokenRange bounds = {outer, close + 1};

    for (int32 owner = 0; owner < regions->count; owner += 1) {
        CsaneSourceRegion *branch = &regions->items[owner];
        CsaneTokenPath path;
        CsaneTokenView view;
        int32 part_first = -1;
        bool owns = false;

        if (branch->kind != CSANE_REGION_CONDITIONAL_BRANCH
            || !csane_stmt_branch_needs_context(regions, owner)
            || branch->tokens.end <= outer || branch->tokens.first > close
            || !csane_token_path_init(&path, tokens, bounds)) {
            continue;
        }
        for (int32 i = bounds.first; i < bounds.end; i += 1) {
            int32 region = csane_regions_at_token(regions, i);
            bool selected = region >= 0
                            && regions->items[region].kind == CSANE_REGION_CODE
                            && csane_stmt_path_selects(regions, region, owner);
            if (selected && part_first < 0) {
                part_first = i;
            } else if (!selected && part_first >= 0) {
                (void)csane_token_path_append(&path,
                                              (TokenRange){part_first, i});
                part_first = -1;
            }
        }
        if (part_first >= 0) {
            (void)csane_token_path_append(&path,
                                          (TokenRange){part_first, bounds.end});
        }
        if (csane_tokens_path_view(&view, &path)) {
            int32 i = csane_tokens_next(&view, branch->tokens.first - 1);
            while (i >= branch->tokens.first && i < branch->tokens.end
                   && i < bounds.end) {
                if (TOKEN_IS(&tokens->tokens[i], "{")
                    && csane_tokens_match(&view, i) == close) {
                    owns = true;
                    break;
                }
                i = csane_tokens_next(&view, i);
            }
        }
        csane_token_path_free(&path);
        if (owns) {
            return true;
        }
    }
    return false;
}

static void
csane_stmt_drop_branch_closers(CsaneStatementStream *stream) {
    int32 allocated = stream->count;
    int32 *kept = malloc2((int64)allocated*SIZEOF(*kept));
    int32 *stack = malloc2((int64)allocated*SIZEOF(*stack));
    int32 kept_count = 0;
    int32 depth = 0;

    for (int32 read = 0; read < stream->count; read += 1) {
        int32 token = stream->indices[read];
        bool drop = false;
        if (csane_stmt_open(stream, read)) {
            stack[depth] = token;
            depth += 1;
        } else if (csane_stmt_close(stream, read)) {
            int32 found = depth - 1;
            Token *close = &stream->statements->toplevel->regions->tokens
                                ->tokens[token];
            while (found >= 0) {
                Token *open = &stream->statements->toplevel->regions->tokens
                                   ->tokens[stack[found]];
                if ((TOKEN_IS(open, "(") && TOKEN_IS(close, ")"))
                    || (TOKEN_IS(open, "[") && TOKEN_IS(close, "]"))
                    || (TOKEN_IS(open, "{") && TOKEN_IS(close, "}"))) {
                    break;
                }
                found -= 1;
            }
            if (found >= 0 && found == 0 && TOKEN_IS(close, "}")) {
                drop = csane_stmt_branch_owns_close(stream, 0, read);
            }
            if (!drop && found >= 0) {
                depth = found;
            }
        }
        if (!drop) {
            kept[kept_count] = token;
            kept_count += 1;
        }
    }
    memcpy64(stream->indices, kept, (int64)kept_count*SIZEOF(*stream->indices));
    stream->count = kept_count;
    free2(stack, (int64)allocated*SIZEOF(*stack));
    free2(kept, (int64)allocated*SIZEOF(*kept));
    return;
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
            int32 found = depth - 1;
            while (found >= 0 && !csane_stmt_pair(stream, stack[found], i)) {
                found -= 1;
            }
            if (found < 0) {
                depth = 0;
                continue;
            }
            int32 open = stack[found];
            stream->matches[open] = i;
            stream->matches[i] = open;
            depth = found;
        }
    }
    free2(stack, (int64)stream->count*SIZEOF(*stack));
    return;
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

/* Recover locally at a statement boundary without consuming unrelated
 * siblings. Never advance past the closing brace of the containing block. */
static int32
csane_stmt_recover(CsaneStatementStream *stream, int32 first, int32 limit) {
    for (int32 i = first; i < limit; i += 1) {
        if (csane_stmt_is(stream, i, ";")) {
            return i + 1;
        }
        if (csane_stmt_is(stream, i, "}")) {
            return i > first ? i : i + 1;
        }
        if (stream->matches[i] > i) {
            i = stream->matches[i];
        }
    }
    return limit;
}

static bool
csane_stmt_new_line(CsaneStatementStream *stream, int32 a, int32 b) {
    Token *first = csane_stmt_token(stream, a);
    Token *next = csane_stmt_token(stream, b);
    Tokenization *tokens = stream->statements->toplevel->regions->tokens;
    for (int32 i = first->offset + first->len; i < next->offset; i += 1) {
        if (tokens->text[i] == '\n') {
            return true;
        }
    }
    return false;
}

static int32
csane_stmt_macro_fragment_end(CsaneStatementStream *stream, int32 first,
                              int32 limit) {
    int32 close;
    if (!csane_token_macro_name(csane_stmt_token(stream, first))) {
        return -1;
    }
    if (first + 1 >= limit || !csane_stmt_is(stream, first + 1, "(")) {
        return first + 1;
    }
    close = stream->matches[first + 1];
    if (close < first + 1 || close >= limit) {
        return -1;
    }
    return close + 1;
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
    int32 macro_end = csane_stmt_macro_fragment_end(stream, first, limit);
    if (macro_end > first
        && (macro_end == limit
            || csane_stmt_is(stream, macro_end, "}")
            || (!csane_stmt_is(stream, macro_end, ";")
                && !csane_stmt_is(stream, macro_end, ",")
                && csane_stmt_new_line(stream, macro_end - 1, macro_end)))) {
        *result = csane_stmt_append(stream, CSANE_STMT_MACRO_FRAGMENT,
                                    first, macro_end, parent, first);
        return macro_end;
    }
    if (csane_stmt_is(stream, first, "{")) {
        int32 close = stream->matches[first];
        if (close < first || close >= limit) {
            int32 end = csane_stmt_recover(stream, first + 1, limit);
            *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE, first,
                                        end, parent, first);
            return end;
        }
        *result = csane_stmt_append(stream, CSANE_STMT_BLOCK, first,
                                    close + 1, parent, first + 1);
        csane_stmt_children(stream, first + 1, close, *result, depth + 1);
        return close + 1;
    }
    if (csane_stmt_is(stream, first, "case")
        || csane_stmt_is(stream, first, "default")) {
        int32 ternary = 0;
        for (int32 i = first + 1; i < limit; i += 1) {
            if (csane_stmt_is(stream, i, "?")) {
                ternary += 1;
            } else if (csane_stmt_is(stream, i, ":") && ternary > 0) {
                ternary -= 1;
            } else if (csane_stmt_is(stream, i, ":")) {
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
                int32 end = csane_stmt_recover(stream, first + 1, limit);
                /* An unrecognized control header can still have a complete
                 * brace-delimited body. Retain that body as one opaque
                 * fragment, rather than swallowing the next statement. */
                for (int32 i = first + 1; i < end; i += 1) {
                    if (csane_stmt_is(stream, i, "{")
                        && stream->matches[i] > i) {
                        end = stream->matches[i] + 1;
                        break;
                    }
                }
                *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE,
                                            first, end, parent, first);
                return end;
            }
            header_end = stream->matches[header_end] + 1;
        }
        int32 id = csane_stmt_append(stream, control, first, header_end,
                                     parent, header_end);
        int32 body = -1;
        int32 end = csane_stmt_parse(stream, header_end, limit, id,
                                     &body, depth + 1);
        /* realloc2 during recursive parsing can move the item array. */
        if (id >= 0) {
            stream->statements->items[id].body = body;
        }
        if (control == CSANE_STMT_IF && csane_stmt_is(stream, end, "else")) {
            int32 alternative = -1;
            end = csane_stmt_parse(stream, end, limit, id,
                                   &alternative, depth + 1);
            if (id >= 0) {
                stream->statements->items[id].alternative = alternative;
            }
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
        if (id >= 0) {
            Token *last = csane_stmt_token(stream,
                                           end > first ? end - 1 : first);
            stream->statements->items[id].tokens.end = stream->indices[
                end > first ? end - 1 : first] + 1;
            stream->statements->items[id].source.end = last->offset
                                                       + last->len;
        }
        *result = id;
        return end;
    }
    bool malformed = false;
    for (int32 i = first; i < limit; i += 1) {
        if (csane_stmt_is(stream, i, ";")) {
            enum CsaneStatementKind kind = malformed ? CSANE_STMT_OPAQUE
                                                     : CSANE_STMT_SIMPLE;
            *result = csane_stmt_append(stream, kind, first, i + 1,
                                        parent, first);
            return i + 1;
        }
        if (stream->matches[i] > i) {
            i = stream->matches[i];
        } else if (csane_stmt_open(stream, i)
                   || csane_stmt_close(stream, i)) {
            malformed = true;
            if (csane_stmt_is(stream, i, "}")) {
                int32 end = i > first ? i : i + 1;
                *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE,
                                            first, end, parent, first);
                return end;
            }
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

static bool
csane_stmt_branch_on_path(CsaneRegions *regions, int32 branch, int32 owner) {
    for (int32 r = owner; r >= 0; r = regions->items[r].parent) {
        if (r == branch) {
            return true;
        }
    }
    return false;
}

static bool
csane_stmt_region_selected(CsaneRegions *regions, int32 region, int32 owner) {
    bool has_branch = false;
    for (int32 r = region; r >= 0; r = regions->items[r].parent) {
        if (regions->items[r].kind != CSANE_REGION_CONDITIONAL_BRANCH) {
            continue;
        }
        has_branch = true;
        if (!csane_stmt_branch_on_path(regions, r, owner)) {
            return false;
        }
    }
    return !has_branch || owner >= 0;
}

static bool
csane_stmt_branch_needs_context(CsaneRegions *regions, int32 owner) {
    CsaneSourceRegion *branch = &regions->items[owner];
    CsaneTokenPath path;
    CsaneTokenView view;
    int32 first;
    int32 last;
    bool needs_context = false;

    if (branch->kind != CSANE_REGION_CONDITIONAL_BRANCH
        || !csane_token_path_init(&path, regions->tokens, branch->tokens)) {
        return false;
    }
    for (int32 child = branch->first_child; child >= 0;
         child = regions->items[child].next_sibling) {
        if (regions->items[child].kind == CSANE_REGION_CODE) {
            (void)csane_token_path_append(&path, regions->items[child].tokens);
        }
    }
    if (csane_tokens_path_view(&view, &path)) {
        first = csane_tokens_next(&view, view.range.first - 1);
        last = csane_tokens_previous(&view, view.range.end);
        if (first >= view.range.first && first < view.range.end
            && last >= view.range.first && last < view.range.end) {
            needs_context = TOKEN_IS(&regions->tokens->tokens[first], ",")
                            || (TOKEN_IS(&regions->tokens->tokens[first], "if")
                                && TOKEN_IS(&regions->tokens->tokens[last],
                                            "{"));
        }
    }
    csane_token_path_free(&path);
    return needs_context;
}

static void
csane_stmt_stream(CsaneStatements *statements, int32 function,
                  int32 owner) {
    CsaneTopItem *top = &statements->toplevel->items[function];
    CsaneRegions *regions = statements->toplevel->regions;
    Tokenization *tokens = regions->tokens;
    CsaneStatementStream stream = {
        .statements = statements,
        .function = function,
        .owner = owner,
    };
    CsaneTokenPath path;
    CsaneTokenView view;
    int32 part_first = -1;
    int32 region;
    int32 i;
    bool branch_owner;
    bool selected;

    branch_owner = csane_stmt_branch_needs_context(regions, owner);
    if (branch_owner) {
        stream.focus_branch = true;
        stream.focus_source = regions->items[owner].source;
    }
    if (!csane_token_path_init(&path, tokens, top->body)) {
        return;
    }
    for (i = top->body.first; i < top->body.end; i += 1) {
        region = csane_regions_at_token(regions, i);
        selected = region >= 0
                        && regions->items[region].kind == CSANE_REGION_CODE
                        && (branch_owner
                            ? csane_stmt_region_selected(regions, region, owner)
                            : csane_stmt_owner(regions, region) == owner);
        if (selected && part_first < 0) {
            part_first = i;
        } else if (!selected && part_first >= 0) {
            (void)csane_token_path_append(&path,
                                          (TokenRange){part_first, i});
            part_first = -1;
        }
    }
    if (part_first >= 0) {
        (void)csane_token_path_append(&path,
                                      (TokenRange){part_first, top->body.end});
    }
    if (csane_tokens_path_view(&view, &path)) {
        i = csane_tokens_next(&view, view.range.first - 1);
        while (i >= view.range.first && i < view.range.end) {
            if (tokens->tokens[i].kind != TOKEN_PREPROC) {
                csane_stmt_push(&stream, i);
            }
            i = csane_tokens_next(&view, i);
        }
    }
    csane_token_path_free(&path);
    if (stream.count > 0) {
        int32 first = 0;
        int32 end;
        if (owner == top->owner_region) {
            csane_stmt_drop_branch_closers(&stream);
        }
        end = stream.count;
        csane_stmt_match(&stream);
        /* A function's outermost braces belong to the base stream. Branch
         * streams may contain only fragments of the function body. */
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
