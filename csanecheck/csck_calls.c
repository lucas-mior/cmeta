// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_CALLS_C)
#define CSANECHECK_CALLS_C

#include "cbase.h"
#include "csck.h"
#include "csck_tokens.h"
#include "csck_regions.h"

/* All offsets refer to the original tokenization. Neither argument text nor
 * whitespace is reconstructed, and unparsed regions produce no AST claims. */
bool
csane_calls_code(CsaneFile *file, int32 index) {
    if (index < 0 || index >= file->tokens->token_count) {
        return false;
    }
    return !file->preprocessor->token_roles
           || file->preprocessor->token_roles[index] == C_PP_TOKEN_CODE
           || file->preprocessor->token_roles[index]
              == C_PP_TOKEN_MACRO_REPLACEMENT;
}

bool
csane_calls_generated(char *path, int32 path_len) {
    return ENDS_WITH(path, path_len, ".meta.h");
}

SourceLocation
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
        && ENDS_WITH(b->text, b->len, "_len")) {
        return true;
    }
    if (a->len > 0 && b->len == a->len + 1
        && b->text[0] == 'n'
        && !memcmp64(b->text + 1, a->text, a->len)) {
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
    if (ENDS_WITH(a->text, a->len, "width")) {
        prefix = a->len - 5;
        if ((prefix == 0 || a->text[prefix - 1] == '_')
            && b->len == prefix + 6
            && !memcmp64(a->text, b->text, prefix)
            && ENDS_WITH(b->text, b->len, "height")) {
            return true;
        }
    }
    return false;
}

/* Call-layout and contextual rules use original token ranges rather than
 * requiring an AST for the containing translation unit. */
static int32
csane_call_next(CsaneFile *file, TokenRange range, int32 after) {
    int32 result;

    result = token_range_next_significant(file->tokens, range, after);
    while (result < range.end
           && c_preprocessor_token_is_line_splice(file->tokens, result)) {
        result = token_range_next_significant(file->tokens, range, result);
    }
    return result;
}

static int32
csane_call_previous(CsaneFile *file, TokenRange range, int32 before) {
    int32 result;

    result = token_range_previous_significant(file->tokens, range, before);
    while (result >= range.first
           && c_preprocessor_token_is_line_splice(file->tokens, result)) {
        result = token_range_previous_significant(file->tokens, range, result);
    }
    return result;
}

static bool
csane_call_string(Token *token) {
    int32 quote;

    if (token->kind != TOKEN_LITERAL) {
        return false;
    }
    quote = literal_quote_index(token->text, token->len, 0);
    return quote >= 0 && token->text[quote] == '"';
}

static bool
csane_call_format(CsaneFile *file, TokenRange range) {
    for (int32 i = range.first; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        int32 quote;
        int32 end;

        if (!csane_call_string(token)) {
            continue;
        }
        quote = literal_quote_index(token->text, token->len, 0);
        end = quote + 1;
        while (end < token->len) {
            if (token->text[end] == '\\') {
                end += 2;
            } else if (token->text[end] == '"') {
                break;
            } else {
                end += 1;
            }
        }
        for (int32 j = quote + 1; j < end; j += 1) {
            if (token->text[j] == '\\') {
                j += 1;
            } else if (token->text[j] == '%' && j + 1 < end) {
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

/* Macro parameters need not be uppercase. Read the corresponding
 * definition rather than guessing their spelling from identifier case. */
static bool
csane_call_macro_parameters(CsaneFile *file, int32 open, TokenRange content) {
    CsaneRegions *regions = file->regions;
    int32 region = csane_regions_at_token(regions, open);
    int32 definition = csane_regions_enclosing(regions, region,
                                               CSANE_REGION_MACRO_DEFINE);
    int32 parameters = -1;
    Tokenization *tokens;
    TokenRange names;

    if (definition < 0) {
        return false;
    }
    for (int32 child = regions->items[definition].first_child;
         child >= 0; child = regions->items[child].next_sibling) {
        if (regions->items[child].kind == CSANE_REGION_MACRO_PARAMETERS) {
            parameters = child;
            break;
        }
    }
    if (parameters < 0) {
        return false;
    }
    tokens = file->tokens;
    names = regions->items[parameters].tokens;
    for (int32 i = content.first; i < content.end; i += 1) {
        Token *candidate = &tokens->tokens[i];
        if (candidate->kind != TOKEN_IDENT) {
            continue;
        }
        for (int32 j = names.first; j < names.end; j += 1) {
            Token *name = &tokens->tokens[j];
            if (name->kind == TOKEN_IDENT
                && candidate->len == name->len
                && !memcmp64(candidate->text, name->text, name->len)) {
                return true;
            }
        }
    }
    return false;
}

/* Literal alignment applies only to adjacent literals at the start of
 * one argument. A nested call owns its literals; the enclosing call must not
 * report them a second time. Perl likewise ignores literals buried in an
 * expression or separated by comments/other tokens. */
static void
csane_call_literals(CsaneFile *file, TokenRange range) {
    int32 first;
    int32 previous;

    if (csane_call_macro_fragments(file, range)) {
        return;
    }
    first = token_range_first_significant(file->tokens, range);
    if (first >= range.end
        || !csane_call_string(&file->tokens->tokens[first])) {
        return;
    }
    previous = first;
    for (int32 i = first + 1; i < range.end; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (token->kind == TOKEN_COMMENT) {
            return;
        }
        if (token_is_trivia(token)) {
            continue;
        }
        if (!csane_call_string(token)) {
            return;
        }
        if (csane_calls_line_break(file, previous, i)
            && csane_calls_location(file, first).column
               != csane_calls_location(file, i).column) {
            csane_rule_issue(file, token->offset, STRLIT("FMT-LITERAL-ALIGN"),
                             "call string literal is not aligned "
                             "with first literal");
            return;
        }
        previous = i;
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
    TokenRange ranges[256];
    int32 parts;
    int32 count = 0;

    if (capacity > 256 || capacity < 0) {
        return -1;
    }
    parts = csane_tokens_split(view, STRLIT(","), ranges, capacity);
    if (parts < 0 || parts > capacity) {
        return -1;
    }
    for (int32 i = 0; i < parts; i += 1) {
        TokenRange range = ranges[i];
        int32 first = csane_call_next(file, range, range.first - 1);
        int32 last = csane_call_previous(file, range, range.end);
        if (first >= range.end) {
            continue;
        }
        args[count].first = first;
        args[count].last = last;
        args[count].format = csane_call_format(file, range);
        args[count].identifier =
            (first == last
             && file->tokens->tokens[first].kind == TOKEN_IDENT)
            || (TOKEN_IS(&file->tokens->tokens[first], "&")
                && file->tokens->tokens[last].kind == TOKEN_IDENT
                && csane_call_next(file, range, first) == last);
        count += 1;
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
    Tokenization *tokens;
    int32 format = -1;
    int32 first_string = -1;
    int32 second_string = -1;
    int32 string_count = 0;
    int32 first_line;
    int32 second_line;

    if (count < 1) {
        return;
    }
    tokens = file->tokens;
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
                             STRLIT("FMT-FORMAT-LINE"),
                             "format-string line must contain all "
                             "format arguments or none");
        }
        if (!on_line && format + 2 < count) {
            SourceLocation first_location =
                csane_calls_location(file, args[format + 1].first);
            bool split = false;
            bool safe = true;

            for (int32 i = format + 1; i < count; i += 1) {
                SourceLocation first_location_i =
                    csane_calls_location(file, args[i].first);
                SourceLocation last_location_i =
                    csane_calls_location(file, args[i].last);

                if (first_location_i.line != last_location_i.line) {
                    safe = false;
                    break;
                }
                if (first_location_i.line != first_location.line) {
                    split = true;
                }
            }
            if (safe && split) {
                int32 start =
                    tokens->tokens[args[format + 1].first].offset;
                int32 close = view->range.end;
                int32 close_end = tokens->tokens[close].offset
                                  + tokens->tokens[close].len;
                SourceLocation close_location =
                    csane_calls_location(file, close);
                int32 end =
                    tokenization_physical_line_end_offset(tokens,
                                                          close_location.line);
                int32 max_width = file->column_limit;

                if (end > close_end && tokens->text[end - 1] == '\n') {
                    end -= 1;
                }
                while (end > close_end
                       && (tokens->text[end - 1] == ' '
                           || tokens->text[end - 1] == '\t'
                           || tokens->text[end - 1] == '\r')) {
                    end -= 1;
                }
                if (max_width > 0) {
                    max_width -= first_location.column + end - close_end;
                    safe = max_width > 0;
                }
                if (safe) {
                    int32 width;

                    width = csane_wrap_flat_length(tokens->text, start,
                                                   close_end, max_width);

                    if (width >= 0
                        && (max_width <= 0 || width <= max_width)) {
                        for (int32 i = args[format + 1].first;
                             i <= close; i += 1) {
                            if (tokens->tokens[i].kind == TOKEN_COMMENT) {
                                safe = false;
                                break;
                            }
                        }
                        if (safe) {
                            StrFlex *rule =
                                SFLIT("FMT-FORMAT-ARGS-WRAP");
                            char *message =
                                "format arguments fit on one line but "
                                "are split across multiple lines";

                            csane_rule_issue(file, start, rule->data,
                                             rule->len, message);
                        }
                    }
                }
            }
        }
    }
    if (count < 3) {
        return;
    }
    /* When exactly two adjacent arguments are string literals, keep them
     * together or put each on a line without any other argument. */
    for (int32 i = 0; i < count; i += 1) {
        if (args[i].first != args[i].last
            || !csane_call_string(&tokens->tokens[args[i].first])) {
            continue;
        }
        string_count += 1;
        if (string_count == 1) {
            first_string = i;
        } else if (string_count == 2) {
            second_string = i;
        }
    }
    if (string_count == 2 && second_string == first_string + 1) {
        first_line = csane_calls_location(file, args[first_string].first).line;
        second_line = csane_calls_location(file,
                                           args[second_string].first).line;
        if (first_line != second_line) {
            for (int32 i = 0; i < count; i += 1) {
                int32 begin;
                int32 end;

                if (i == first_string || i == second_string) {
                    continue;
                }
                begin = csane_calls_location(file, args[i].first).line;
                end = csane_calls_location(file, args[i].last).line;
                if ((begin <= first_line && first_line <= end)
                    || (begin <= second_line && second_line <= end)) {
                    int32 token = args[second_string].first;

                    csane_rule_issue(file, tokens->tokens[token].offset,
                                     STRLIT("FMT-STRING-PAIR"),
                                     "related string arguments must share "
                                     "a line or each be on its own line");
                    break;
                }
            }
        }
    }
    for (int32 i = 0; i + 1 < count; i += 1) {
        int32 a;
        int32 b;
        int32 line_a;
        int32 line_b;

        if (!args[i].identifier || !args[i + 1].identifier) {
            continue;
        }
        a = args[i].last;
        b = args[i + 1].last;
        if (!csane_calls_related(&tokens->tokens[a], &tokens->tokens[b])) {
            continue;
        }
        line_a = csane_calls_location(file, a).line;
        line_b = csane_calls_location(file, b).line;
        if (line_a != line_b
            && (csane_call_count_on_line(file, args, count, line_a) != 1
                || csane_call_count_on_line(file, args, count, line_b) != 1)) {
            csane_rule_issue(file, tokens->tokens[b].offset,
                             STRLIT("FMT-RELATED-ARGS"),
                             "related arguments must be the only "
                             "arguments on their lines");
            break;
        }
    }
}

/* Recognized file-scope macro invocations have their own first-argument
 * rule; arbitrary file-scope expressions are not classified as macros. */
static bool
csane_call_top_macro(CsaneFile *file, int32 name) {
    CsaneTopLevels *top = file->toplevel;

    if (top == NULL) {
        return false;
    }
    for (int32 i = 0; i < top->count; i += 1) {
        CsaneTopItem *item = &top->items[i];
        if (item->kind == CSANE_TOP_MACRO_INVOCATION
            && item->name_token == name) {
            return true;
        }
    }
    return false;
}

static void
csane_call_token_one(CsaneFile *file, int32 name, int32 open,
                     int32 close, bool macro) {
    Tokenization *tokens = file->tokens;
    TokenRange content = {open + 1, close};
    CsaneTokenView view;
    int32 first;
    int32 from;
    int32 to;
    bool newline = false;
    bool plain = true;
    int32 format = -1;
    int32 anchor;
    int32 cursor;
    int32 previous = -1;
    bool misaligned = false;
    bool macro_parameters;

    if (!csane_tokens_view(&view, tokens, content)
        || !csane_tokens_balanced(&view)) {
        return;
    }
    first = csane_call_next(file, content, open);
    if (first >= close) {
        return;
    }
    from = tokens->tokens[open].offset + tokens->tokens[open].len;
    to = tokens->tokens[first].offset;
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
        bool top_macro = !macro && csane_call_top_macro(file, name);
        StrFlex *rule = top_macro ? SFLIT("FMT-CALL-FIRST-TOPLEVEL")
                                  : SFLIT("FMT-CALL-FIRST");

        if (!top_macro
            || !csane_policy_macro_allowed(file->policy,
                                           file->path, file->path_len,
                                           STRLIT("FMT-CALL-FIRST-TOPLEVEL"),
                                           &tokens->tokens[name])) {
            csane_rule_issue(file, tokens->tokens[open].offset,
                             rule->data, rule->len,
                             "call breaks before the first argument");
        }
    }
    if (!csane_calls_line_break(file, open, close)) {
        return;
    }
    macro_parameters = macro
                       && csane_call_macro_parameters(file, open, content);
    /* Contextual comparisons are sound for self-contained replacements.
     * Names of macros or parameters can change their argument structure. */
    if (!macro
        || (!csane_call_macro_fragments(file, content)
            && !macro_parameters)) {
        csane_call_context(file, &view);
    }
    anchor = first;
    cursor = content.first;
    while (cursor < close) {
        int32 comma = csane_tokens_top_level(&view, cursor, STRLIT(","));
        int32 end;
        TokenRange argument;
        int32 current;
        if (comma < 0) {
            return;
        }
        if (comma < close) {
            end = comma;
        } else {
            end = close;
        }
        argument = (TokenRange){cursor, end};
        current = csane_call_next(file, argument, cursor - 1);
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
            previous = csane_call_previous(file, argument, argument.end);
        }
        if (comma >= close) {
            break;
        }
        cursor = comma + 1;
    }
    /* Parameters in macro replacements can be expanded into arbitrary
     * argument layouts. Continuation-line indentation is not a reliable
     * alignment anchor for those calls. */
    if (misaligned && !macro_parameters) {
        /* The legacy rule emits one issue per call. */
        cursor = content.first;
        previous = -1;
        if (format >= 0) {
            anchor = format;
        } else {
            anchor = first;
        }
        while (cursor < close) {
            int32 comma = csane_tokens_top_level(&view, cursor, STRLIT(","));
            int32 end;
            TokenRange argument;
            int32 current;
            if (comma < close) {
                end = comma;
            } else {
                end = close;
            }
            argument = (TokenRange){cursor, end};
            current = csane_call_next(file, argument, cursor - 1);
            if (current < end) {
                if (previous >= 0
                    && csane_calls_line_break(file, previous, current)
                    && csane_calls_location(file, current).column
                       != csane_calls_location(file, anchor).column) {
                    csane_rule_issue(file, tokens->tokens[current].offset,
                                     STRLIT("FMT-ARG-ALIGN"),
                                     "call argument is not aligned "
                                     "with its alignment anchor");
                    break;
                }
                previous = csane_call_previous(file, argument, argument.end);
            }
            if (comma >= close) {
                break;
            }
            cursor = comma + 1;
        }
    }
}

static bool
csane_call_spacing_gap(CsaneFile *file, Token *name, Token *open) {
    char *text = file->tokens->text;
    int32 start = name->offset + name->len;
    for (int32 i = start; i < open->offset; i += 1) {
        if (text[i] != ' ' && text[i] != '\t'
            && text[i] != '\r' && text[i] != '\n') {
            return false;
        }
    }
    return start < open->offset;
}

static StrFlex *csane_call_keywords[] = {
    SFLIT("if"), SFLIT("for"), SFLIT("while"),
    SFLIT("switch"), SFLIT("return"), SFLIT("sizeof"),
    SFLIT("_Alignof"), SFLIT("_Generic"), SFLIT("_Static_assert"),
};

static bool
csane_call_keyword(Token *token) {
    int32 count = LENGTH(csane_call_keywords);
    for (int32 i = 0; i < count; i += 1) {
        StrFlex *word = csane_call_keywords[i];
        if (TOKEN_IS(token, word->data, word->len)) {
            return true;
        }
    }
    return false;
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
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);

    if (csane_calls_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 i = 0; i < tokens->token_count; i += 1) {
        Token *token = &tokens->tokens[i];
        CsaneTokenView local;
        int32 region;
        enum CsaneRegionKind region_kind;
        int32 replacement;
        int32 branch;
        int32 name;
        TokenRange range;
        int32 before;
        enum CsaneLocalKind kind;
        int32 close;
        bool macro;

        if (!TOKEN_IS(token, "(") || !csane_calls_code(file, i)) {
            continue;
        }
        local = view;
        region = csane_regions_at_token(file->regions, i);
        region_kind = CSANE_REGION_MACRO_REPLACEMENT;
        replacement = csane_regions_enclosing(file->regions, region,
                                              region_kind);
        region_kind = CSANE_REGION_CONDITIONAL_BRANCH;
        branch = csane_regions_enclosing(file->regions, region, region_kind);
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
        name = csane_tokens_previous(&local, i);
        if (name < 0 || tokens->tokens[name].kind != TOKEN_IDENT
            || !csane_calls_code(file, name)
            || csane_call_keyword(&tokens->tokens[name])
            || csane_call_prototype(file, name)) {
            continue;
        }
        range = (TokenRange){0, tokens->token_count};
        before = csane_tokens_previous(&local, name);
        if (before >= 0
            && (csane_type_name(&tokens->tokens[before])
                || TOKEN_IS(&tokens->tokens[before], "*"))) {
            continue;
        }
        kind = csane_classify_invocation(tokens, range, name);
        if (kind != CSANE_LOCAL_CALL && kind != CSANE_LOCAL_MACRO_INVOCATION) {
            continue;
        }
        if (kind == CSANE_LOCAL_CALL
            && csane_call_spacing_gap(file, &tokens->tokens[name], token)) {
            csane_rule_issue(file, token->offset, STRLIT("FMT-CALL-SPACING"),
                             "no whitespace before call parenthesis");
        }
        close = csane_tokens_match(&local, i);
        if (close < 0) {
            continue;
        }
        macro = file->preprocessor->token_roles != NULL
                && file->preprocessor->token_roles[i]
                   == C_PP_TOKEN_MACRO_REPLACEMENT;
        csane_call_token_one(file, name, i, close, macro);
    }
}

#endif /* CSANECHECK_CALLS_C */
