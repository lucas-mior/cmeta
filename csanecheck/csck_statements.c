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
    bool focus_branch;
    SourceRange focus_source;
} CsaneStatementStream;

static Token *
csane_stmt_token(CsaneStatementStream *stream, int32 position) {
    int32 index = stream->indices[position];
    return &stream->statements->toplevel->regions->tokens->tokens[index];
}

static bool
csane_stmt_is(CsaneStatementStream *stream, int32 position,
              char *text, int32 text_len) {
    return position >= 0 && position < ARRAY_LEN(stream->indices)
           && TOKEN_IS(csane_stmt_token(stream, position), text, text_len);
}

static int32
csane_stmt_append(CsaneStatementStream *stream, enum CsaneStatementKind kind,
                  int32 first, int32 end, int32 parent, int32 header_end) {
    CsaneStatements *statements;
    CsaneStatement *owner;
    SourceRange source;
    Token *start;
    Token *last;
    int32 id;
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
    id = ARRAY_LEN(statements->items);
    ARRAY_PUSH(statements->items, (CsaneStatement){
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
    });
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
    return (csane_stmt_is(stream, a, STRLIT("("))
            && csane_stmt_is(stream, b, STRLIT(")")))
           || (csane_stmt_is(stream, a, STRLIT("["))
               && csane_stmt_is(stream, b, STRLIT("]")))
           || (csane_stmt_is(stream, a, STRLIT("{"))
               && csane_stmt_is(stream, b, STRLIT("}")));
}

static bool
csane_stmt_open(CsaneStatementStream *stream, int32 position) {
    return csane_stmt_is(stream, position, STRLIT("("))
           || csane_stmt_is(stream, position, STRLIT("["))
           || csane_stmt_is(stream, position, STRLIT("{"));
}

static bool
csane_stmt_close(CsaneStatementStream *stream, int32 position) {
    return csane_stmt_is(stream, position, STRLIT(")"))
           || csane_stmt_is(stream, position, STRLIT("]"))
           || csane_stmt_is(stream, position, STRLIT("}"));
}

static bool csane_stmt_branch_needs_context(CsaneRegions *, int32);


static bool
csane_stmt_path_selects(CsaneRegions *regions, int32 region, int32 owner) {
    for (int32 r = region; r >= 0; r = regions->items[r].parent) {
        bool _i50_predicate;
        _i50_predicate =
            regions->items[r].kind == CSANE_REGION_CONDITIONAL_BRANCH;
        if (_i50_predicate) {
            bool _i50_result;
            do {
                int32 _i50_branch = r;

                for (int32 r = owner; r >= 0; r = regions->items[r].parent) {
                    if (r == _i50_branch) {
                        _i50_result = true;
                        goto _i50_done;
                    }
                }
                _i50_result = false;
                goto _i50_done;
                _i50_done: ;
            } while (false);
            _i50_predicate = !_i50_result;
        }
        if (_i50_predicate) {
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

    for (int32 owner = 0; owner < ARRAY_LEN(regions->items); owner += 1) {
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
    int32 allocated = ARRAY_LEN(stream->indices);
    int32 *kept = malloc2(allocated*SIZEOF(*kept));
    int32 *stack = malloc2(allocated*SIZEOF(*stack));
    int32 kept_count = 0;
    int32 depth = 0;

    for (int32 read = 0; read < ARRAY_LEN(stream->indices); read += 1) {
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
    memcpy64(stream->indices, kept, kept_count*SIZEOF(*stream->indices));
    ARRAY_SET_COUNT(stream->indices, kept_count);
    free2(stack, allocated*SIZEOF(*stack));
    free2(kept, allocated*SIZEOF(*kept));
    return;
}



/* Recover locally at a statement boundary without consuming unrelated
 * siblings. Never advance past the closing brace of the containing block. */
static int32
csane_stmt_recover(CsaneStatementStream *stream, int32 first, int32 limit) {
    for (int32 i = first; i < limit; i += 1) {
        if (csane_stmt_is(stream, i, STRLIT(";"))) {
            return i + 1;
        }
        if (csane_stmt_is(stream, i, STRLIT("}"))) {
            return i > first ? i : i + 1;
        }
        if (stream->matches[i] > i) {
            i = stream->matches[i];
        }
    }
    return limit;
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
    int32 macro_end;
    enum CsaneStatementKind control;
    bool malformed = false;

    *result = -1;
    if (first >= limit) {
        return limit;
    }
    if (depth > 256) {
        *result = csane_stmt_append(stream, CSANE_STMT_OPAQUE, first,
                                    limit, parent, first);
        return limit;
    }
    int32 in_result_end;
    do {
        int32 close;
        if (!csane_token_macro_name(csane_stmt_token(stream, first))) {
            in_result_end = -1;
            break;
        }
        if (first + 1 >= limit
            || !csane_stmt_is(stream, first + 1, STRLIT("("))) {
            in_result_end = first + 1;
            break;
        }
        close = stream->matches[first + 1];
        if (close < first + 1 || close >= limit) {
            in_result_end = -1;
            break;
        }
        in_result_end = close + 1;
    } while (false);
    macro_end = in_result_end;
    bool _i51_predicate;
    _i51_predicate = macro_end > first;
    if (_i51_predicate) {
        _i51_predicate = macro_end == limit;
        if (!_i51_predicate) {
            _i51_predicate = csane_stmt_is(stream, macro_end, STRLIT("}"));
        }
        if (!_i51_predicate) {
            _i51_predicate = !csane_stmt_is(stream, macro_end, STRLIT(";"));
            if (_i51_predicate) {
                _i51_predicate = !csane_stmt_is(stream, macro_end, STRLIT(","));
            }
            if (_i51_predicate) {
                bool _i51_result;
                do {
                    int32 _i51_a = macro_end - 1;
                    int32 _i51_b = macro_end;

                    Token *first = csane_stmt_token(stream, _i51_a);
                    Token *next = csane_stmt_token(stream, _i51_b);
                    Tokenization *tokens =
                        stream->statements->toplevel->regions->tokens;
                    for (int32 i =
                        first->offset + first->len; i < next->offset; i += 1) {
                        if (tokens->text[i] == '\n') {
                            _i51_result = true;
                            goto _i51_done;
                        }
                    }
                    _i51_result = false;
                    goto _i51_done;
                    _i51_done: ;
                } while (false);
                _i51_predicate = _i51_result;
            }
        }
    }
    if (_i51_predicate) {
        *result = csane_stmt_append(stream, CSANE_STMT_MACRO_FRAGMENT,
                                    first, macro_end, parent, first);
        return macro_end;
    }
    if (csane_stmt_is(stream, first, STRLIT("{"))) {
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
    if (csane_stmt_is(stream, first, STRLIT("case"))
        || csane_stmt_is(stream, first, STRLIT("default"))) {
        int32 ternary = 0;
        for (int32 i = first + 1; i < limit; i += 1) {
            if (csane_stmt_is(stream, i, STRLIT("?"))) {
                ternary += 1;
            } else if (csane_stmt_is(stream, i, STRLIT(":")) && ternary > 0) {
                ternary -= 1;
            } else if (csane_stmt_is(stream, i, STRLIT(":"))) {
                bool is_case = csane_stmt_is(stream, first, STRLIT("case"));
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
        && csane_stmt_is(stream, first + 1, STRLIT(":"))) {
        *result = csane_stmt_append(stream, CSANE_STMT_LABEL, first,
                                    first + 2, parent, first + 2);
        return first + 2;
    }
    enum CsaneStatementKind in_result_control;
    do {
        int32 in_position = first;

        if (csane_stmt_is(stream, in_position, STRLIT("if"))) {
            in_result_control = CSANE_STMT_IF;
            break;
        }
        if (csane_stmt_is(stream, in_position, STRLIT("else"))) {
            in_result_control = CSANE_STMT_ELSE;
            break;
        }
        if (csane_stmt_is(stream, in_position, STRLIT("for"))) {
            in_result_control = CSANE_STMT_FOR;
            break;
        }
        if (csane_stmt_is(stream, in_position, STRLIT("while"))) {
            in_result_control = CSANE_STMT_WHILE;
            break;
        }
        if (csane_stmt_is(stream, in_position, STRLIT("do"))) {
            in_result_control = CSANE_STMT_DO;
            break;
        }
        if (csane_stmt_is(stream, in_position, STRLIT("switch"))) {
            in_result_control = CSANE_STMT_SWITCH;
            break;
        }
        in_result_control = CSANE_STMT_SIMPLE;
    } while (false);
    control = in_result_control;
    if (control != CSANE_STMT_SIMPLE) {
        int32 header_end = first + 1;
        int32 id;
        int32 body = -1;
        int32 end;

        if (control != CSANE_STMT_ELSE && control != CSANE_STMT_DO) {
            if (!csane_stmt_is(stream, header_end, STRLIT("("))
                || stream->matches[header_end] < header_end) {
                end = csane_stmt_recover(stream, first + 1, limit);
                /* An unrecognized control header can still have a complete
                 * brace-delimited body. Retain that body as one opaque
                 * fragment, rather than swallowing the next statement. */
                for (int32 i = first + 1; i < end; i += 1) {
                    if (csane_stmt_is(stream, i, STRLIT("{"))
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
        id = csane_stmt_append(stream, control, first, header_end,
                               parent, header_end);
        end = csane_stmt_parse(stream, header_end, limit, id,
                               &body, depth + 1);
        /* ARRAY_PUSH during recursive parsing can move the item array. */
        if (id >= 0) {
            stream->statements->items[id].body = body;
        }
        if (control == CSANE_STMT_IF
            && csane_stmt_is(stream, end, STRLIT("else"))) {
            int32 alternative = -1;
            end = csane_stmt_parse(stream, end, limit, id,
                                   &alternative, depth + 1);
            if (id >= 0) {
                stream->statements->items[id].alternative = alternative;
            }
        }
        if (control == CSANE_STMT_DO
            && csane_stmt_is(stream, end, STRLIT("while"))) {
            int32 tail = end + 1;
            if (csane_stmt_is(stream, tail, STRLIT("("))
                && stream->matches[tail] > tail) {
                tail = stream->matches[tail] + 1;
                if (csane_stmt_is(stream, tail, STRLIT(";"))) {
                    tail += 1;
                }
                end = tail;
            }
        }
        if (id >= 0) {
            int32 last_index = end > first ? end - 1 : first;
            Token *last = csane_stmt_token(stream, last_index);
            stream->statements->items[id].tokens.end =
                stream->indices[last_index] + 1;
            stream->statements->items[id].source.end = last->offset
                                                       + last->len;
        }
        *result = id;
        return end;
    }
    for (int32 i = first; i < limit; i += 1) {
        if (csane_stmt_is(stream, i, STRLIT(";"))) {
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
            if (csane_stmt_is(stream, i, STRLIT("}"))) {
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
    if (stream->indices == NULL) {
        ARRAY_INIT(stream->indices, 128);
    }
    ARRAY_PUSH(stream->indices, index);
    return;
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
csane_stmt_stream(CsaneStatements *statements, int32 function, int32 owner) {
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
    int32 i;
    int32 first_region;
    bool branch_owner;

    branch_owner = csane_stmt_branch_needs_context(regions, owner);
    if (branch_owner) {
        stream.focus_branch = true;
        stream.focus_source = regions->items[owner].source;
    }
    if (!csane_token_path_init(&path, tokens, top->body)) {
        return;
    }
    /* Code regions are leaves with contiguous token ranges. Start at the
     * region owning the first body token, not at the start of the file. */
    first_region = csane_regions_at_token(regions, top->body.first);
    if (first_region < 0) {
        first_region = 0;
    }
    for (int32 r = first_region; r < ARRAY_LEN(regions->items); r += 1) {
        CsaneSourceRegion *region = &regions->items[r];
        TokenRange part = region->tokens;

        if (part.first >= top->body.end) {
            break;
        }
        bool _i52_predicate;
        _i52_predicate = region->kind != CSANE_REGION_CODE;
        if (!_i52_predicate) {
            _i52_predicate = part.end <= top->body.first;
        }
        if (!_i52_predicate) {
            _i52_predicate = part.first >= top->body.end;
        }
        if (!_i52_predicate) {
            int32 _i52_result;
            do {
                int32 _i52_region = r;

                for (int32 i = _i52_region; i >= 0; i =
                    regions->items[i].parent) {
                    if (regions->items[i].kind
                        == CSANE_REGION_CONDITIONAL_BRANCH
                        || regions->items[i].kind == CSANE_REGION_FILE) {
                        _i52_result = i;
                        goto _i52_done;
                    }
                }
                _i52_result = -1;
                goto _i52_done;
                _i52_done: ;
            } while (false);
                 bool _i54_result;
                 do {
                     int32 _i54_region = r;

                     bool has_branch = false;
                     for (int32 r = _i54_region; r >= 0; r =
                         regions->items[r].parent) {
                         if (regions->items[r].kind
                             != CSANE_REGION_CONDITIONAL_BRANCH) {
                             continue;
                         }
                         has_branch = true;
                         bool _i53_predicate;
                         bool _i53_result;
                         do {
                             int32 _i53_branch = r;

                             for (int32 r = owner; r >= 0; r =
                                 regions->items[r].parent) {
                                 if (r == _i53_branch) {
                                     _i53_result = true;
                                     goto _i53_done;
                                 }
                             }
                             _i53_result = false;
                             goto _i53_done;
                             _i53_done: ;
                         } while (false);
                         _i53_predicate = !_i53_result;
                         if (_i53_predicate) {
                             _i54_result = false;
                             goto _i54_done;
                         }
                     }
                     _i54_result = !has_branch || owner >= 0;
                     goto _i54_done;
                     _i54_done: ;
                 } while (false);
            _i52_predicate = branch_owner
                 ? _i54_result
                 : _i52_result == owner;
            _i52_predicate = !_i52_predicate;
        }
        if (_i52_predicate) {
            continue;
        }
        /* Region token bounds include tokens overlapping their source
         * edges. The stream owns tokens by their starting offset. */
        if (part.first < ARRAY_LEN(tokens->tokens)
            && tokens->tokens[part.first].offset < region->source.start) {
            part.first += 1;
        }
        if (part.end < ARRAY_LEN(tokens->tokens)
            && tokens->tokens[part.end].offset < region->source.end) {
            part.end += 1;
        }
        if (part.first < top->body.first) {
            part.first = top->body.first;
        }
        if (part.end > top->body.end) {
            part.end = top->body.end;
        }
        (void)csane_token_path_append(&path, part);
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
    if (ARRAY_LEN(stream.indices) > 0) {
        int32 first = 0;
        int32 end;
        if (owner == top->owner_region) {
            csane_stmt_drop_branch_closers(&stream);
        }
        end = ARRAY_LEN(stream.indices);

        int32 *stack = malloc2(ARRAY_LEN(stream.indices)*SIZEOF(*stack));
        int32 depth = 0;

        stream.matches = malloc2(ARRAY_LEN(stream.indices)
                                  *SIZEOF(*stream.matches));
        for (int32 i = 0; i < ARRAY_LEN(stream.indices); i += 1) {
            stream.matches[i] = -1;
            if (csane_stmt_open(&stream, i)) {
                stack[depth] = i;
                depth += 1;
            } else if (csane_stmt_close(&stream, i)) {
                int32 found = depth - 1;
                int32 open;

                while (found >= 0
                       && !csane_stmt_pair(&stream, stack[found], i)) {
                    found -= 1;
                }
                if (found < 0) {
                    depth = 0;
                    continue;
                }
                open = stack[found];
                stream.matches[open] = i;
                stream.matches[i] = open;
                depth = found;
            }
        }
        free2(stack, ARRAY_LEN(stream.indices)*SIZEOF(*stack));

        /* A function's outermost braces belong to the base stream. Branch
         * streams may contain only fragments of the function body. */
        if (owner == top->owner_region
            && csane_stmt_is(&stream, first, STRLIT("{"))
            && csane_stmt_is(&stream, end - 1, STRLIT("}"))) {
            first += 1;
            end -= 1;
        }
        csane_stmt_children(&stream, first, end, -1, 0);
    }
    free2(stream.matches, ARRAY_LEN(stream.indices)*SIZEOF(*stream.matches));
    ARRAY_FREE(stream.indices);
    return;
}

bool
csane_statements_init(CsaneStatements *statements, CsaneTopLevels *top) {
    CsaneRegions *regions;

    if (statements == NULL) {
        return false;
    }
    *statements = (CsaneStatements){0};
    if (top == NULL || top->regions == NULL || top->regions->tokens == NULL) {
        return false;
    }
    statements->toplevel = top;
    regions = top->regions;
    for (int32 i = 0; i < ARRAY_LEN(top->items); i += 1) {
        SourceRange body = top->items[i].source;

        if (top->items[i].kind != CSANE_TOP_FUNCTION_DEFINITION) {
            continue;
        }
        csane_stmt_stream(statements, i, top->items[i].owner_region);
        for (int32 r = 0; r < ARRAY_LEN(regions->items); r += 1) {
            SourceRange span;

            if (regions->items[r].kind != CSANE_REGION_CONDITIONAL_BRANCH) {
                continue;
            }
            span = regions->items[r].source;
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
    ARRAY_FREE(statements->items);
    *statements = (CsaneStatements){0};
}

int32
csane_statements_at_token(CsaneStatements *statements, int32 function,
                          int32 token) {
    Tokenization *tokens;
    int32 offset;
    int32 best = -1;

    if (statements == NULL || statements->toplevel == NULL) {
        return -1;
    }
    tokens = statements->toplevel->regions->tokens;
    if (token < 0 || token >= ARRAY_LEN(tokens->tokens)) {
        return -1;
    }
    offset = tokens->tokens[token].offset;
    for (int32 i = 0; i < ARRAY_LEN(statements->items); i += 1) {
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
