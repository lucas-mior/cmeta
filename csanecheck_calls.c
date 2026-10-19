// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CALLS_C)
#define CSANECHECK_CALLS_C

#include "cbase.h"
#include "csanecheck.h"
#include "csanecheck_tokens.h"
#include "csanecheck_regions.h"

/* All offsets refer to the original tokenization. Neither argument text nor
 * whitespace is reconstructed, and unparsed regions produce no AST claims. */
static bool
csane_calls_code(CsaneFile *file, int32 index) {
    if (index < 0 || index >= file->tokens->token_count) {
        return false;
    }
    return !file->preprocessor->token_roles
           || file->preprocessor->token_roles[index] == C_PP_TOKEN_CODE
           || file->preprocessor->token_roles[index]
              == C_PP_TOKEN_MACRO_REPLACEMENT;
}

static bool
csane_calls_generated(char *path) {
    int32 len = strlen32(path);
    return len >= 7 && !memcmp64(path + len - 7, ".meta.h", 7);
}

static SourceLocation
csane_calls_location(CsaneFile *file, int32 index) {
    return tokenization_source_location(file->tokens,
                                        file->tokens->tokens[index].offset);
}

static bool
csane_calls_line_break(CsaneFile *file, int32 before, int32 after) {
    return csane_calls_location(file, before).line
           < csane_calls_location(file, after).line;
}

static bool
csane_calls_related(Token *a, Token *b) {
    int32 prefix;
    if (b->len == a->len + 4
        && !memcmp64(b->text, a->text, a->len)
        && !memcmp64(b->text + a->len, "_len", 4)) {
        return true;
    }
    if (a->len >= 1 && a->text[a->len - 1] == 'x') {
        prefix = a->len - 1;
        if ((prefix == 0 || a->text[prefix - 1] == '_')
            && b->len == prefix + 1
            && !memcmp64(a->text, b->text, prefix)
            && b->text[prefix] == 'y') {
            return true;
        }
    }
    if (a->len >= 5 && !memcmp64(a->text + a->len - 5, "width", 5)) {
        prefix = a->len - 5;
        if ((prefix == 0 || a->text[prefix - 1] == '_')
            && b->len == prefix + 6
            && !memcmp64(a->text, b->text, prefix)
            && !memcmp64(b->text + prefix, "height", 6)) {
            return true;
        }
    }
    return false;
}

/* Call-layout and contextual rules use original token ranges rather than
 * requiring an AST for the containing translation unit. */
static int32
csane_call_next(CsaneFile *file, TokenRange range, int32 after) {
    return token_range_next_significant(file->tokens, range, after);
}

static bool
csane_call_string(Token *token) {
    if (token->kind != TOKEN_LITERAL) {
        return false;
    }
    int32 quote = literal_quote_index(token->text, token->len, 0);
    return quote >= 0 && token->text[quote] == '"';
}

static bool
csane_call_format(CsaneFile *file, TokenRange range) {
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (!csane_call_string(token)) {
            continue;
        }
        int32 quote = literal_quote_index(token->text, token->len, 0);
        for (int32 j = quote + 1; j < token->len; j += 1) {
            if (token->text[j] == '\\') {
                j += 1;
            } else if (token->text[j] == '%') {
                return true;
            }
        }
    }
    return false;
}

static bool
csane_call_macro_fragments(CsaneFile *file, TokenRange range) {
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (token->kind == TOKEN_IDENT
            && csane_name_kind(token) == CSANE_NAME_MACRO) {
            return true;
        }
    }
    return false;
}

static void
csane_call_literals(CsaneFile *file, TokenRange range) {
    if (csane_call_macro_fragments(file, range)) {
        return;
    }
    int32 first = -1;
    int32 last = -1;
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (csane_call_string(token)) {
            if (first >= 0 && last >= 0
                && csane_calls_line_break(file, last, i)
                && csane_calls_location(file, first).column
                   != csane_calls_location(file, i).column) {
                csane_rule_issue(file, token->offset, "FMT-LITERAL-ALIGN",
                                 "call string literal is not aligned "
                                 "with first literal");
                return;
            }
            if (first < 0) {
                first = i;
            }
            last = i;
        } else if (token->kind == TOKEN_COMMENT
                   || !token_is_trivia(token)) {
            first = -1;
            last = -1;
        }
    }
}

/* Local argument information is independent of expression parsing. */
typedef struct CsaneCallArgument {
    int32 first;
    int32 last;
    bool format;
    bool identifier;
} CsaneCallArgument;

static int32
csane_call_arguments(CsaneFile *file, CsaneTokenView *view,
                     CsaneCallArgument *args, int32 capacity) {
    int32 cursor = view->range.first;
    int32 count = 0;
    while (cursor < view->range.end) {
        int32 comma = csane_tokens_top_level(view, cursor, ",", 1);
        if (comma < 0) {
            return -1;
        }
        int32 end = comma < view->range.end ? comma : view->range.end;
        TokenRange range = {cursor, end};
        int32 first = token_range_first_significant(file->tokens, range);
        int32 last = token_range_last_significant(file->tokens, range);
        if (first < end) {
            if (count >= capacity) {
                return -1;
            }
            args[count].first = first;
            args[count].last = last;
            args[count].format = csane_call_format(file, range);
            args[count].identifier =
                first == last
                && file->tokens->tokens[first].kind == TOKEN_IDENT;
            count += 1;
        }
        if (comma >= view->range.end) {
            break;
        }
        cursor = comma + 1;
    }
    return count;
}

static int32
csane_call_count_on_line(CsaneFile *file, CsaneCallArgument *args,
                         int32 count, int32 line) {
    int32 result = 0;
    for (int32 i = 0; i < count; i += 1) {
        if (csane_calls_location(file, args[i].first).line == line) {
            result += 1;
        }
    }
    return result;
}

static void
csane_call_context(CsaneFile *file, CsaneTokenView *view) {
    /* Calls with an unusually large number of arguments are still examined
     * by the basic layout checks; no contextual conclusion is inferred. */
    CsaneCallArgument args[256];
    int32 count = csane_call_arguments(file, view, args, 256);
    if (count < 1) {
        return;
    }
    Tokenization *tokens = file->tokens;
    int32 format = -1;
    for (int32 i = 0; i < count; i += 1) {
        if (args[i].format) {
            format = i;
            break;
        }
    }
    if (format >= 0) {
        int32 line = csane_calls_location(file, args[format].last).line;
        bool on_line = false;
        bool off_line = false;
        for (int32 i = format + 1; i < count; i += 1) {
            if (csane_calls_location(file, args[i].first).line == line) {
                on_line = true;
            } else {
                off_line = true;
            }
        }
        if (on_line && off_line) {
            csane_rule_issue(file, tokens->tokens[args[format].first].offset,
                             "FMT-FORMAT-LINE",
                             "format-string line must contain all "
                             "format arguments or none");
        }
    }
    if (count < 3) {
        return;
    }
    for (int32 i = 0; i + 1 < count; i += 1) {
        if (!args[i].identifier || !args[i + 1].identifier) {
            continue;
        }
        int32 a = args[i].first;
        int32 b = args[i + 1].first;
        if (!csane_calls_related(&tokens->tokens[a], &tokens->tokens[b])) {
            continue;
        }
        int32 line_a = csane_calls_location(file, a).line;
        int32 line_b = csane_calls_location(file, b).line;
        if (line_a != line_b
            && (csane_call_count_on_line(file, args, count, line_a) != 1
                || csane_call_count_on_line(file, args, count, line_b) != 1)) {
            csane_rule_issue(file, tokens->tokens[b].offset,
                             "FMT-RELATED-ARGS",
                             "related arguments must be the only "
                             "arguments on their lines");
            break;
        }
    }
}

static void
csane_call_token_one(CsaneFile *file, int32 open, int32 close,
                     bool macro) {
    Tokenization *tokens = file->tokens;
    TokenRange content = {open + 1, close};
    CsaneTokenView view;
    if (!csane_tokens_view(&view, tokens, content)
        || !csane_tokens_balanced(&view)) {
        return;
    }
    int32 first = csane_call_next(file, content, open);
    if (first >= close) {
        return;
    }
    int32 from = tokens->tokens[open].offset + tokens->tokens[open].len;
    int32 to = tokens->tokens[first].offset;
    bool newline = false;
    bool plain = true;
    for (int32 j = from; j < to; j += 1) {
        char c = tokens->text[j];
        if (c == '\n') {
            newline = true;
        } else if (c != '\r' && c != ' ' && c != '\t') {
            plain = false;
            break;
        }
    }
    if (newline && plain) {
        csane_rule_issue(file, tokens->tokens[open].offset,
                         "FMT-CALL-FIRST",
                         "call breaks before the first argument");
    }
    if (!csane_calls_line_break(file, open, close)) {
        return;
    }
    /* Contextual comparisons are sound for self-contained replacements.
     * Names of macros or parameters can change their argument structure. */
    if (!macro || !csane_call_macro_fragments(file, content)) {
        csane_call_context(file, &view);
    }
    int32 format = -1;
    int32 anchor = first;
    int32 cursor = content.first;
    int32 previous = -1;
    bool misaligned = false;
    while (cursor < close) {
        int32 comma = csane_tokens_top_level(&view, cursor, ",", 1);
        if (comma < 0) {
            return;
        }
        int32 end = comma < close ? comma : close;
        TokenRange argument = {cursor, end};
        int32 current = csane_call_next(file, argument, cursor - 1);
        if (current < end) {
            if (format < 0 && csane_call_format(file, argument)) {
                format = current;
                anchor = current;
            }
            csane_call_literals(file, argument);
            if (previous >= 0
                && csane_calls_line_break(file, previous, current)
                && csane_calls_location(file, current).column
                   != csane_calls_location(file, anchor).column) {
                misaligned = true;
            }
            previous = token_range_last_significant(tokens, argument);
        }
        if (comma >= close) {
            break;
        }
        cursor = comma + 1;
    }
    if (misaligned) {
        /* The legacy rule emits one issue per call. */
        cursor = content.first;
        previous = -1;
        anchor = format >= 0 ? format : first;
        while (cursor < close) {
            int32 comma = csane_tokens_top_level(&view, cursor, ",", 1);
            int32 end = comma < close ? comma : close;
            TokenRange argument = {cursor, end};
            int32 current = csane_call_next(file, argument, cursor - 1);
            if (current < end) {
                if (previous >= 0
                    && csane_calls_line_break(file, previous, current)
                    && csane_calls_location(file, current).column
                       != csane_calls_location(file, anchor).column) {
                    csane_rule_issue(file, tokens->tokens[current].offset,
                                     "FMT-ARG-ALIGN",
                                     "call argument is not aligned "
                                     "with its alignment anchor");
                    break;
                }
                previous = token_range_last_significant(tokens, argument);
            }
            if (comma >= close) {
                break;
            }
            cursor = comma + 1;
        }
    }
}

static bool
csane_call_prototype(CsaneFile *file, int32 name) {
    CsaneTopLevels *top = file->toplevel;
    if (top == NULL) {
        return false;
    }
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->name_token == name
            && (item->kind == CSANE_TOP_FUNCTION_DECLARATION
                || item->kind == CSANE_TOP_FUNCTION_DEFINITION)) {
            return true;
        }
    }
    return false;
}

void
csane_calls_token_check(CsaneFile *file) {
    if (csane_calls_generated(file->path)) {
        return;
    }
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        if (!TOKEN_IS(token, "(") || !csane_calls_code(file, i)) {
            continue;
        }
        CsaneTokenView local = view;
        int32 region = csane_regions_at_token(file->regions, i);
        enum CsaneRegionKind region_kind = CSANE_REGION_MACRO_REPLACEMENT;
        int32 replacement = csane_regions_enclosing(file->regions, region,
                                                    region_kind);
        region_kind = CSANE_REGION_CONDITIONAL_BRANCH;
        int32 branch = csane_regions_enclosing(file->regions, region,
                                               region_kind);
        if (replacement >= 0) {
            if (!csane_tokens_view(&local, tokens,
                                   file->regions->items[replacement].tokens)) {
                continue;
            }
        } else if (branch >= 0) {
            CsaneTokenView candidate;
            if (csane_tokens_view(&candidate, tokens,
                                  file->regions->items[branch].tokens)
                && csane_tokens_match(&candidate, i) >= 0) {
                local = candidate;
            }
        }
        int32 name = csane_tokens_previous(&local, i);
        if (name < 0 || tokens->tokens[name].kind != TOKEN_IDENT
            || csane_call_prototype(file, name)) {
            continue;
        }
        TokenRange range = {0, tokens->token_count};
        int32 before = csane_tokens_previous(&local, name);
        if (before >= 0
            && (csane_type_name(&tokens->tokens[before])
                || TOKEN_IS(&tokens->tokens[before], "*"))) {
            continue;
        }
        enum CsaneLocalKind kind =
            csane_classify_invocation(tokens, range, name);
        if (kind != CSANE_LOCAL_CALL && kind != CSANE_LOCAL_MACRO_INVOCATION) {
            continue;
        }
        int32 close = csane_tokens_match(&local, i);
        if (close < 0) {
            continue;
        }
        bool macro = file->preprocessor->token_roles != NULL
                     && file->preprocessor->token_roles[i]
                        == C_PP_TOKEN_MACRO_REPLACEMENT;
        csane_call_token_one(file, i, close, macro);
    }
}

#endif /* CSANECHECK_CALLS_C */
