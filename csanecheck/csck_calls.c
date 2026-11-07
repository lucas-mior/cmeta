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
    if (index < 0 || index >= ARRAY_LEN(file->tokens->tokens)) {
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

/* An array and LENGTH(array) are related only when the complete array
 * expression is identical on both sides. Ignore whitespace and comments,
 * but not extra operations or arguments to LENGTH. */
static bool
csane_call_matching_length(CsaneFile *file, CsaneTokenView *view,
                           CsaneCallArgument *array,
                           CsaneCallArgument *length) {
    Tokenization *tokens = file->tokens;
    TokenRange array_range = {array->first, array->last + 1};
    TokenRange length_range = {length->first, length->last + 1};
    TokenRange operand;
    int32 open;
    int32 left;
    int32 right;

    if (tokens->tokens[array->first].kind != TOKEN_IDENT
        || !TOKEN_IS(&tokens->tokens[length->first], "LENGTH")) {
        return false;
    }
    open = csane_call_next(file, length_range, length->first);
    if (open >= length_range.end
        || !TOKEN_IS(&tokens->tokens[open], "(")
        || csane_tokens_match(view, open) != length->last) {
        return false;
    }
    operand = (TokenRange){open + 1, length->last};
    array_range = csane_tokens_unwrap(view, array_range);
    operand = csane_tokens_unwrap(view, operand);
    left = csane_call_next(file, array_range, array_range.first - 1);
    right = csane_call_next(file, operand, operand.first - 1);
    while (left < array_range.end && right < operand.end) {
        Token *a = &tokens->tokens[left];
        Token *b = &tokens->tokens[right];
        if (a->kind != b->kind || a->len != b->len
            || memcmp64(a->text, b->text, a->len)) {
            return false;
        }
        left = csane_call_next(file, array_range, left);
        right = csane_call_next(file, operand, right);
    }
    return left == array_range.end && right == operand.end;
}

/* Compare expression tokens, ignoring comments and whitespace but not
 * operators, suffixes or index structure. */
static bool
csane_call_equal_tokens(CsaneFile *file, TokenRange a, TokenRange b) {
    Tokenization *tokens = file->tokens;
    int32 left = csane_call_next(file, a, a.first - 1);
    int32 right = csane_call_next(file, b, b.first - 1);

    while (left < a.end && right < b.end) {
        Token *x = &tokens->tokens[left];
        Token *y = &tokens->tokens[right];
        if (x->kind != y->kind || x->len != y->len
            || memcmp64(x->text, y->text, x->len)) {
            return false;
        }
        left = csane_call_next(file, a, left);
        right = csane_call_next(file, b, right);
    }
    return left == a.end && right == b.end;
}

/* SP(array, index) replaces matching array_lens[index], array[index]
 * arguments, whether standalone variables or struct members. The owner path
 * is empty for standalone arrays. Require identical paths and indices so a
 * merely similar name or index cannot trigger a replacement suggestion. */
static bool
csane_call_sp_pair(CsaneFile *file, CsaneTokenView *view,
                   CsaneCallArgument *length, CsaneCallArgument *string) {
    Tokenization *tokens = file->tokens;
    TokenRange lrange = {length->first, length->last + 1};
    TokenRange srange = {string->first, string->last + 1};
    int32 lopen;
    int32 sopen;
    int32 lname;
    int32 sname;
    Token *length_token = NULL;
    Token *string_token = NULL;

    if (!TOKEN_IS(&tokens->tokens[length->last], "]")
        || !TOKEN_IS(&tokens->tokens[string->last], "]")) {
        return false;
    }
    lopen = csane_tokens_match(view, length->last);
    sopen = csane_tokens_match(view, string->last);
    if (lopen < 0 || sopen < 0) {
        return false;
    }
    lname = csane_call_previous(file, lrange, lopen);
    sname = csane_call_previous(file, srange, sopen);
    if (lname < length->first || sname < string->first) {
        return false;
    }
    length_token = &tokens->tokens[lname];
    string_token = &tokens->tokens[sname];
    if (length_token->kind != TOKEN_IDENT || string_token->kind != TOKEN_IDENT
        || string_token->len < 1
        || string_token->text[string_token->len - 1] != 's'
        || length_token->len != string_token->len + 5
        || memcmp64(length_token->text, string_token->text, string_token->len)
        || !ENDS_WITH(length_token->text, length_token->len, "_lens")) {
        return false;
    }
    return csane_call_equal_tokens(file,
                                   (TokenRange){length->first, lname},
                                   (TokenRange){string->first, sname})
           && csane_call_equal_tokens(file,
                                      (TokenRange){lopen + 1, length->last},
                                      (TokenRange){sopen + 1, string->last});
}

/* Follow format conversions to find the two arguments consumed by %.*s.
 * The existing format-string recognition chooses the format argument; this
 * parser only handles literal formats, without guessing dynamic expressions. */
static void
csane_call_sp_format(CsaneFile *file, CsaneTokenView *view,
                     CsaneCallArgument *args, int32 count, int32 format) {
    Tokenization *tokens = file->tokens;
    TokenRange range = {args[format].first, args[format].last + 1};
    int32 next_arg = format + 1;

    for (int32 i = csane_call_next(file, range, range.first - 1);
         i < range.end; i = csane_call_next(file, range, i)) {
        Token *literal = &tokens->tokens[i];
        int32 quote;
        int32 end;

        if (!csane_call_string(literal)) {
            return;
        }
        quote = literal_quote_index(literal->text, literal->len, 0);
        end = literal->len - 1;
        for (int32 j = quote + 1; j < end;) {
            int32 start;
            int32 stars = 0;
            char conversion;

            if (literal->text[j] == '\\') {
                j += 2;
                continue;
            }
            if (literal->text[j] != '%') {
                j += 1;
                continue;
            }
            start = j;
            j += 1;
            if (j < end && literal->text[j] == '%') {
                j += 1;
                continue;
            }
            while (j < end && (literal->text[j] == '-'
                               || literal->text[j] == '+'
                               || literal->text[j] == ' '
                               || literal->text[j] == '#'
                               || literal->text[j] == '0'
                               || literal->text[j] == '\'')) {
                j += 1;
            }
            if (j < end && literal->text[j] == '*') {
                stars += 1;
                j += 1;
            } else {
                while (j < end && literal->text[j] >= '0'
                       && literal->text[j] <= '9') {
                    j += 1;
                }
            }
            if (j < end && literal->text[j] == '.') {
                j += 1;
                if (j < end && literal->text[j] == '*') {
                    stars += 1;
                    j += 1;
                } else {
                    while (j < end && literal->text[j] >= '0'
                           && literal->text[j] <= '9') {
                        j += 1;
                    }
                }
            }
            if (j < end && (literal->text[j] == 'h'
                            || literal->text[j] == 'l')) {
                char modifier = literal->text[j];
                j += 1;
                if (j < end && literal->text[j] == modifier) {
                    j += 1;
                }
            } else if (j < end && (literal->text[j] == 'j'
                                   || literal->text[j] == 'z'
                                   || literal->text[j] == 't'
                                   || literal->text[j] == 'L')) {
                j += 1;
            } else if (j < end && literal->text[j] == 'w') {
                j += 1;
                while (j < end && literal->text[j] >= '0'
                       && literal->text[j] <= '9') {
                    j += 1;
                }
            }
            if (j >= end) {
                return;
            }
            conversion = literal->text[j];
            if (conversion != 'd' && conversion != 'i'
                && conversion != 'u' && conversion != 'o'
                && conversion != 'x' && conversion != 'X'
                && conversion != 'f' && conversion != 'F'
                && conversion != 'e' && conversion != 'E'
                && conversion != 'g' && conversion != 'G'
                && conversion != 'a' && conversion != 'A'
                && conversion != 'c' && conversion != 's'
                && conversion != 'p' && conversion != 'n') {
                return;
            }
            if (j == start + 3 && conversion == 's'
                && literal->text[start + 1] == '.'
                && literal->text[start + 2] == '*'
                && next_arg + 1 < count
                && csane_call_sp_pair(file, view, &args[next_arg],
                                      &args[next_arg + 1])) {
                csane_rule_issue(file,
                                 tokens->tokens[args[next_arg].first].offset,
                                 STRLIT("STYLE-SP-SOA"),
                                 "use SP(array, index) for %.*s with "
                                 "parallel string arrays");
            }
            next_arg += 1 + stars;
            j += 1;
        }
    }
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


/* Recognized file-scope macro invocations have their own first-argument
 * rule; arbitrary file-scope expressions are not classified as macros. */

/* A free2 size computed with strlen32(pointer) cannot be evaluated when
 * pointer is NULL, even though free2 itself accepts NULL. Match complete
 * argument expressions rather than similarly named variables. */
static bool
csane_call_free2_strlen_operand(CsaneFile *file, CsaneTokenView *view,
                                TokenRange pointer, TokenRange operand) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView expression;
    TokenRange argument;
    int32 name;
    int32 open;
    int32 close;

    operand = csane_tokens_unwrap(view, operand);
    if (!csane_tokens_subview(&expression, view, operand)) {
        return false;
    }
    name = csane_call_next(file, operand, operand.first - 1);
    if (name >= operand.end
        || !TOKEN_IS(&tokens->tokens[name], "strlen32")) {
        return false;
    }
    open = csane_call_next(file, operand, name);
    if (open >= operand.end || !TOKEN_IS(&tokens->tokens[open], "(")) {
        return false;
    }
    close = csane_tokens_match(&expression, open);
    if (close < 0 || csane_call_next(file, operand, close) < operand.end) {
        return false;
    }
    argument = (TokenRange){open + 1, close};
    argument = csane_tokens_unwrap(&expression, argument);
    return csane_call_equal_tokens(file, pointer, argument);
}

static bool
csane_call_free2_strlen_size(CsaneFile *file, CsaneTokenView *view,
                             TokenRange pointer, TokenRange size) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView expression;
    TokenRange first;
    TokenRange second;
    int32 plus;
    int32 one;

    size = csane_tokens_unwrap(view, size);
    if (!csane_tokens_subview(&expression, view, size)) {
        return false;
    }
    plus = csane_tokens_top_level(&expression, size.first - 1, STRLIT("+"));
    if (plus < size.first || plus >= size.end) {
        return false;
    }
    first = csane_tokens_unwrap(&expression,
                                (TokenRange){size.first, plus});
    second = csane_tokens_unwrap(&expression,
                                 (TokenRange){plus + 1, size.end});
    one = csane_call_next(file, second, second.first - 1);
    if (one < second.end && TOKEN_IS(&tokens->tokens[one], "1")
        && csane_call_next(file, second, one) >= second.end) {
        return csane_call_free2_strlen_operand(file, &expression,
                                               pointer, first);
    }
    one = csane_call_next(file, first, first.first - 1);
    return one < first.end && TOKEN_IS(&tokens->tokens[one], "1")
           && csane_call_next(file, first, one) >= first.end
           && csane_call_free2_strlen_operand(file, &expression,
                                              pointer, second);
}




static StrFlex *csane_call_keywords[] = {
    SFLIT("if"), SFLIT("for"), SFLIT("while"),
    SFLIT("switch"), SFLIT("return"), SFLIT("sizeof"),
    SFLIT("_Alignof"), SFLIT("_Generic"), SFLIT("_Static_assert"),
};



void
csane_calls_token_check(CsaneFile *file) {
    Tokenization *tokens = file->tokens;
    CsaneTokenView view = csane_tokens_full(tokens);

    if (csane_calls_generated(file->path, file->path_len)) {
        return;
    }
    for (int32 call_i = 0; call_i < ARRAY_LEN(tokens->tokens); call_i += 1) {
        bool _i10_predicate;
        bool _i11_predicate;
        Token *token = &tokens->tokens[call_i];
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

        if (!TOKEN_IS(token, "(") || !csane_calls_code(file, call_i)) {
            continue;
        }
        local = view;
        region = csane_regions_at_token(file->regions, call_i);
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
                && csane_tokens_match(&candidate, call_i) >= 0) {
                local = candidate;
            }
        }
        name = csane_tokens_previous(&local, call_i);
        _i11_predicate = name < 0;
        if (!_i11_predicate) {
            _i11_predicate = tokens->tokens[name].kind != TOKEN_IDENT;
        }
        if (!_i11_predicate) {
            _i11_predicate = !csane_calls_code(file, name);
        }
        if (!_i11_predicate) {
            bool _i11_result;
            do {
                Token *_i11_token = &tokens->tokens[name];

                int32 count = LENGTH(csane_call_keywords);
                for (int32 i = 0; i < count; i += 1) {
                    StrFlex *word = csane_call_keywords[i];
                    if (TOKEN_IS(_i11_token, word->data, word->len)) {
                        _i11_result = true;
                        goto _i11_done;
                    }
                }
                _i11_result = false;
                goto _i11_done;
                _i11_done: ;
            } while (false);
            _i11_predicate = _i11_result;
        }
        if (!_i11_predicate) {
            bool _i12_result;
            do {
                CsaneTopLevels *top = file->toplevel;
                if (top == NULL) {
                    _i12_result = false;
                    goto _i12_done;
                }
                for (int32 i = 0; i < ARRAY_LEN(top->items); i += 1) {
                    CsaneTopItem *item = &top->items[i];
                    if (item->name_token == name
                        && (item->kind == CSANE_TOP_FUNCTION_DECLARATION
                            || item->kind == CSANE_TOP_FUNCTION_DEFINITION)) {
                        _i12_result = true;
                        goto _i12_done;
                    }
                }
                _i12_result = false;
                goto _i12_done;
                _i12_done: ;
            } while (false);
            _i11_predicate = _i12_result;
        }
        if (_i11_predicate) {
            continue;
        }
        range = (TokenRange){0, ARRAY_LEN(tokens->tokens)};
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
        _i10_predicate = kind == CSANE_LOCAL_CALL;
        if (_i10_predicate) {
            bool _i10_result;
            do {
                Token *_i10_name = &tokens->tokens[name];
                Token *_i10_open = token;

                char *text = file->tokens->text;
                int32 start = _i10_name->offset + _i10_name->len;
                for (int32 i = start; i < _i10_open->offset; i += 1) {
                    if (text[i] != ' ' && text[i] != '\t'
                        && text[i] != '\r' && text[i] != '\n') {
                        _i10_result = false;
                        goto _i10_done;
                    }
                }
                _i10_result = start < _i10_open->offset;
                goto _i10_done;
                _i10_done: ;
            } while (false);
            _i10_predicate = _i10_result;
        }
        if (_i10_predicate) {
            csane_rule_issue(file, token->offset, STRLIT("FMT-CALL-SPACING"),
                             "no whitespace before call parenthesis");
        }
        close = csane_tokens_match(&local, call_i);
        if (close < 0) {
            continue;
        }
        do {
            CsaneFile *in_file = file;
            CsaneTokenView *in_view = &local;
            int32 in_open = call_i;

            TokenRange args[2];
            int32 count;

            if (!TOKEN_IS(&in_file->tokens->tokens[name], "free2")) {
                break;
            }
            count = csane_tokens_arguments(in_view, in_open,
                                           args, LENGTH(args));
            if (count != 2) {
                break;
            }
            args[0] = csane_tokens_unwrap(in_view, args[0]);
            if (!csane_call_free2_strlen_size(in_file, in_view,
                                              args[0], args[1])) {
                break;
            }
            csane_rule_issue(in_file, in_file->tokens->tokens[name].offset,
                             STRLIT("STYLE-FREE2-STRLEN"),
                             "use opt_strlen32(pointer) in the free2 size "
                             "argument");
        } while (false);
        macro = file->preprocessor->token_roles != NULL
                && file->preprocessor->token_roles[call_i]
                   == C_PP_TOKEN_MACRO_REPLACEMENT;
        do {
            bool _i6_result;
            int32 _i9_open = call_i;

            Tokenization *call_tokens = file->tokens;
            TokenRange content = {_i9_open + 1, close};
            CsaneTokenView nested_view;
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

            if (!csane_tokens_view(&nested_view, call_tokens, content)
                || !csane_tokens_balanced(&nested_view)) {
                goto _i9_done;
            }
            first = csane_call_next(file, content, _i9_open);
            if (first >= close) {
                goto _i9_done;
            }
            from = call_tokens->tokens[_i9_open].offset;
            from += call_tokens->tokens[_i9_open].len;
            to = call_tokens->tokens[first].offset;
            for (int32 j = from; j < to; j += 1) {
                char c = call_tokens->text[j];
                if (c == '\n') {
                    newline = true;
                } else if (c != '\r' && c != ' ' && c != '\t') {
                    plain = false;
                    break;
                }
            }
            if (newline && plain) {
                StrFlex *rule = SFLIT("FMT-CALL-FIRST");
                bool top_macro;
                bool _i8_result;
                do {
                    CsaneTopLevels *top = file->toplevel;

                    if (top == NULL) {
                        _i8_result = false;
                        goto _i8_done;
                    }
                    for (int32 i = 0; i < ARRAY_LEN(top->items); i += 1) {
                        CsaneTopItem *item = &top->items[i];
                        if (item->kind == CSANE_TOP_MACRO_INVOCATION
                            && item->name_token == name) {
                            _i8_result = true;
                            goto _i8_done;
                        }
                    }
                    _i8_result = false;
                    goto _i8_done;
                    _i8_done: ;
                } while (false);
                top_macro = !macro && _i8_result;
                rule = top_macro ? SFLIT("FMT-CALL-FIRST-TOPLEVEL")
                                          : SFLIT("FMT-CALL-FIRST");

                if (!top_macro
                    || !csane_policy_macro_allowed(file->policy,
                                                   file->path, file->path_len,
                                                   rule->data, rule->len,
                                                   &tokens->tokens[name])) {
                    csane_rule_issue(file, call_tokens->tokens[_i9_open].offset,
                                     rule->data, rule->len,
                                     "call breaks before the first argument");
                }
            }
            do {
                TokenRange names;
                Tokenization *macro_tokens = file->tokens;
                int32 parameters = -1;
                int32 definition;
                int32 nested_region;
                CsaneRegions *regions = file->regions;
                int32 _i6_open;
                if (!macro) {
                    _i6_result = false;
                    break;
                }
                _i6_open = _i9_open;

                nested_region = csane_regions_at_token(regions, _i6_open);
                definition = csane_regions_enclosing(regions, nested_region,
                                                     CSANE_REGION_MACRO_DEFINE);

                if (definition < 0) {
                    _i6_result = false;
                    goto _i6_done;
                }
                for (int32 child = regions->items[definition].first_child;
                     child >= 0; child =
                         regions->items[child].next_sibling) {
                    if (regions->items[child].kind
                        == CSANE_REGION_MACRO_PARAMETERS) {
                        parameters = child;
                        break;
                    }
                }
                if (parameters < 0) {
                    _i6_result = false;
                    goto _i6_done;
                }
                names = regions->items[parameters].tokens;
                for (int32 i =
                    content.first; i < content.end; i += 1) {
                    Token *candidate = &macro_tokens->tokens[i];
                    if (candidate->kind != TOKEN_IDENT) {
                        continue;
                    }
                    for (int32 j =
                        names.first; j < names.end; j += 1) {
                        Token *nested_name = &macro_tokens->tokens[j];
                        if (nested_name->kind == TOKEN_IDENT
                            && candidate->len == nested_name->len
                            && !memcmp64(candidate->text,
                                         nested_name->text, nested_name->len)) {
                            _i6_result = true;
                            goto _i6_done;
                        }
                    }
                }
                _i6_result = false;
                goto _i6_done;
                _i6_done: ;
            } while (false);
            macro_parameters = macro
                               && _i6_result;
            if (!macro
                || (!csane_call_macro_fragments(file, content)
                    && !macro_parameters)) {
                do {
                    CsaneTokenView *in_view = &nested_view;

                    CsaneCallArgument args[256];
                    int32 count = csane_call_arguments(file, in_view,
                                                       args, 256);

                    if (count < 3) {
                        break;
                    }
                    for (int32 i = 0; i < count; i += 1) {
                        if (args[i].format) {
                            csane_call_sp_format(file, in_view, args, count, i);
                            break;
                        }
                    }
                } while (false);
            }
            if (!csane_calls_line_break(file, _i9_open, close)) {
                goto _i9_done;
            }
            /* Contextual comparisons are sound for self-contained replacements.
              * Names of macros or parameters can change their argument
              * structure. */
            do {
                int32 second_line;
                int32 first_line;
                int32 string_count = 0;
                int32 second_string = -1;
                int32 first_string = -1;
                int32 fmt_i = -1;
                Tokenization *ftokens = file->tokens;
                int32 count;
                CsaneCallArgument args[256];
                CsaneTokenView *_i7_view = &nested_view;
                if (macro
                    && (csane_call_macro_fragments(file, content)
                        || macro_parameters)) {
                    break;
                }

                /* Calls with an unusually large number of arguments are
                 * still examined
                  * by the basic layout checks; no contextual conclusion is
                  * inferred. */
                count = csane_call_arguments(file, _i7_view, args, 256);

                if (count < 1) {
                    goto _i7_done;
                }
                for (int32 i = 0; i < count; i += 1) {
                    if (args[i].format) {
                        fmt_i = i;
                        break;
                    }
                }
                if (fmt_i >= 0) {
                    int32 line =
                        csane_calls_location(file,
                                             args[fmt_i].last).line;
                    bool on_line = false;
                    bool off_line = false;
                    for (int32 i = fmt_i + 1; i < count; i += 1) {
                        if (csane_calls_location(file, args[i].first).line
                            == line) {
                            on_line = true;
                        } else {
                            off_line = true;
                        }
                    }
                    if (on_line && off_line) {
                        int32 format_offset =
                            ftokens->tokens[args[fmt_i].first]
                                              .offset;
                        csane_rule_issue(file, format_offset,
                                         STRLIT("FMT-FORMAT-LINE"),
                                         "format-string line must contain all "
                                         "format arguments or none");
                    }
                    if (!on_line && fmt_i + 2 < count) {
                        SourceLocation first_location =
                            csane_calls_location(file,
                                                 args[fmt_i + 1].first);
                        bool split = false;
                        bool safe = true;

                        for (int32 i = fmt_i + 1; i < count; i += 1) {
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
                                ftokens->tokens[args[fmt_i + 1].first].offset;
                            int32 close_i = _i7_view->range.end;
                            int32 close_end = ftokens->tokens[close_i].offset;
                            SourceLocation close_location =
                                csane_calls_location(file, close_i);
                            int32 line_no = close_location.line;
                            int32 end =
                                tokenization_physical_line_end_offset(ftokens,
                                                                      line_no);
                            int32 max_width = file->column_limit;

                            close_end += ftokens->tokens[close_i].len;
                            if (end > close_end
                                && ftokens->text[end - 1] == '\n') {
                                end -= 1;
                            }
                            while (end > close_end
                                   && (ftokens->text[end - 1] == ' '
                                       || ftokens->text[end - 1] == '\t'
                                       || ftokens->text[end - 1]
                                           == '\r')) {
                                end -= 1;
                            }
                            if (max_width > 0) {
                                int32 suffix = end - close_end;
                                max_width -= first_location.column + suffix;
                                safe = max_width > 0;
                            }
                            if (safe) {
                                int32 width;

                                char *source = ftokens->text;
                                width = csane_wrap_flat_length(source, start,
                                                               close_end,
                                                               max_width);

                                if (width >= 0
                                    && (max_width <= 0
                                        || width <= max_width)) {
                                    for (int32 i = args[fmt_i + 1].first;
                                         i <= close_i; i += 1) {
                                        if (ftokens->tokens[i].kind
                                            == TOKEN_COMMENT) {
                                            safe = false;
                                            break;
                                        }
                                    }
                                    if (safe) {
                                        StrFlex *rule =
                                            SFLIT("FMT-FORMAT-ARGS-WRAP");
                                        char *message =
                                            "format arguments fit on "
                                            "one line but are split "
                                            "across multiple lines";

                                        csane_rule_issue(file, start,
                                                         rule->data,
                                                         rule->len, message);
                                    }
                                }
                            }
                        }
                    }
                }
                if (count < 3) {
                    goto _i7_done;
                }
                /* When exactly two adjacent arguments are string literals,
                 * keep them
                  * together or put each on a line without any other
                  * argument. */
                for (int32 i = 0; i < count; i += 1) {
                    int32 first_arg = args[i].first;

                    if (first_arg != args[i].last
                        || !csane_call_string(&ftokens->tokens[first_arg])) {
                        continue;
                    }
                    string_count += 1;
                    if (string_count == 1) {
                        first_string = i;
                    } else if (string_count == 2) {
                        second_string = i;
                    }
                }
                if (string_count == 2
                    && second_string == first_string + 1) {
                        int32 second_token;
                    first_line =
                        csane_calls_location(file,
                                             args[first_string].first).line;
                    second_token = args[second_string].first;
                    second_line = csane_calls_location(file, second_token).line;
                    if (first_line != second_line) {
                        for (int32 i = 0; i < count; i += 1) {
                            int32 begin;
                            int32 end;

                            if (i == first_string || i == second_string) {
                                continue;
                            }
                            begin = csane_calls_location(file,
                                                         args[i].first).line;
                            end = csane_calls_location(file,
                                                       args[i].last).line;
                            if ((begin <= first_line && first_line <= end)
                                || (begin <= second_line
                                    && second_line <= end)) {
                                int32 tok_i = args[second_string].first;

                                csane_rule_issue(file,
                                                 ftokens->tokens[tok_i].offset,
                                                 STRLIT("FMT-STRING-PAIR"),
                                                 "related string arguments "
                                                 "must share a line or each "
                                                 "be on its own line");
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
                    bool related = false;

                    a = args[i].last;
                    b = args[i + 1].last;
                    if (args[i].identifier && args[i + 1].identifier) {
                        related = csane_calls_related(&ftokens->tokens[a],
                                                      &ftokens->tokens[b]);
                    }
                    if (!related) {
                        related = csane_call_matching_length(file,
                                                             _i7_view, &args[i],
                                                             &args[i + 1]);
                    }
                    if (!related) {
                        continue;
                    }
                    line_a = csane_calls_location(file, a).line;
                    line_b = csane_calls_location(file, b).line;
                    if (line_a != line_b
                        && (csane_call_count_on_line(file, args, count,
                                                     line_a) != 1
                            || csane_call_count_on_line(file, args, count,
                                                        line_b) != 1)) {
                        csane_rule_issue(file, ftokens->tokens[b].offset,
                                         STRLIT("FMT-RELATED-ARGS"),
                                         "related arguments must be the only "
                                         "arguments on their lines");
                        break;
                    }
                }
                _i7_done: ;
            } while (false);
            anchor = first;
            cursor = content.first;
            while (cursor < close) {
                int32 comma = csane_tokens_top_level(&nested_view, cursor,
                                                     STRLIT(","));
                int32 end;
                TokenRange argument;
                int32 current;
                if (comma < 0) {
                    goto _i9_done;
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
                    previous = csane_call_previous(file, argument,
                                                   argument.end);
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
                    int32 comma = csane_tokens_top_level(&nested_view, cursor,
                                                         STRLIT(","));
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
                            Token *current_token =
                                &call_tokens->tokens[current];
                            csane_rule_issue(file, current_token->offset,
                                             STRLIT("FMT-ARG-ALIGN"),
                                             "call argument is not aligned "
                                             "with its alignment anchor");
                            break;
                        }
                        previous = csane_call_previous(file, argument,
                                                       argument.end);
                    }
                    if (comma >= close) {
                        break;
                    }
                    cursor = comma + 1;
                }
            }
            _i9_done: ;
        } while (false);
    }
}

#endif /* CSANECHECK_CALLS_C */
