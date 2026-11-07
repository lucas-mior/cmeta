// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_RULES_C)
#define CSANECHECK_RULES_C

#include "cbase.h"
#include "csck.h"


static bool
csane_token_text(Token *token, char *text, int32 text_len) {
    return token->len == text_len
           && !memcmp64(token->text, text, text_len);
}

static bool
csane_fixture_path(char *path, int32 len) {
    for (int32 i = 0; i < len; i += 1) {
        if ((i == 0 || path[i - 1] == '/')
            && BEGINS_WITH(path + i, len - i, "tests/fixtures/")) {
            return true;
        }
    }
    return false;
}

void
csane_rule_issue(CsaneFile *file, int32 offset,
                 char *code, int32 code_len, char *message) {
    SourceLocation loc;
    if (csane_policy_rule_allowed(file->policy, file->path, file->path_len,
                                  code, code_len, NULL)) {
        return;
    }
    if (offset < 0) {
        offset = 0;
    }
    if (offset > file->tokens->text_len) {
        offset = file->tokens->text_len;
    }
    loc = tokenization_source_location(file->tokens, offset);
    fmt_printf("%.*s:%d:%d: error[%.*s]: %s\n",
               file->path_len, file->path, loc.line, loc.column + 1,
               code_len, code, message);
    file->violations += 1;
    return;
}

static void
csane_rule_source(CsaneFile *file) {
    char *text = file->tokens->text;
    int32 len = file->tokens->text_len;
    for (int32 start = 0; start < len;) {
        int32 end = start;
        int32 trailing;
        int32 tab = -1;
        while ((end < len) && (text[end] != '\n')) {
            if ((tab < 0) && (text[end] == '\t')) {
                tab = end;
            }
            end += 1;
        }
        trailing = end;
        while ((trailing > start)
               && ((text[trailing - 1] == ' ')
                   || (text[trailing - 1] == '\t')
                   || (text[trailing - 1] == '\r'))) {
            trailing -= 1;
        }
        if (file->column_limit > 0
            && end - start > file->column_limit) {
            int32 columns = 0;
            /* Count UTF-8 scalar starts. Treat invalid UTF-8 as bytes. */
            for (int32 i = start; i < end; i += 1) {
                if (((uint8)text[i] & 0xc0) != 0x80) {
                    columns += 1;
                }
            }
            if (columns > file->column_limit) {
                csane_rule_issue(file, start, STRLIT("SRC-COLUMNS"),
                                 "line exceeds allowed column limit");
            }
        }
        if (tab >= 0) {
            csane_rule_issue(file, tab, STRLIT("SRC-TABS"),
                             "tab character is forbidden");
        }
        if (trailing < end) {
            csane_rule_issue(file, trailing, STRLIT("SRC-TRAILING"),
                             "trailing whitespace");
        }
        start = end + (end < len);
    }
    if (file->is_ryu && (len > 0) && text[len - 1] != '\n') {
        csane_rule_issue(file, len - 1, STRLIT("RYU-FINAL-NEWLINE"),
                         "missing final newline");
    }
    return;
}

/* Perl masks comments and quoted literals, then looks for directives at
 * physical line starts. Match that policy even inside a continued macro
 * replacement, without requiring the preprocessor to accept the directive.
 * Keep the lookup cursor monotonic so the scan is linear in source size.
 */
static bool
csane_source_masked(Tokenization *tokens, int32 *cursor, int32 offset) {
    Token *token;

    while (*cursor < ARRAY_LEN(tokens->tokens)
           && tokens->tokens[*cursor].offset
              + tokens->tokens[*cursor].len <= offset) {
        *cursor += 1;
    }
    if (*cursor >= ARRAY_LEN(tokens->tokens)) {
        return false;
    }
    token = &tokens->tokens[*cursor];
    if (token->offset > offset) {
        return false;
    }
    if (token->kind == TOKEN_COMMENT) {
        return true;
    }
    return token->kind == TOKEN_LITERAL
           && literal_quote_index(token->text, token->len, 0) >= 0;
}

static bool
csane_source_horizontal(char c) {
    return c == ' ' || c == '\t';
}

/* Return the first character of the directive keyword, or -1. */

static bool
csane_source_keyword(char *text, int32 end, int32 index,
                     char *word, int32 word_len) {
    return index >= 0 && index + word_len <= end
           && !memcmp64(text + index, word, word_len)
           && (index + word_len == end
               || !char_is_identifier_body(text[index + word_len]));
}

static void
csane_rule_preprocessor(CsaneFile *file) {
    char *text = file->tokens->text;
    int32 len = file->tokens->text_len;
    int32 cursor = 0;
    for (int32 start = 0; start < len;) {
        int32 end = start;
        int32 keyword;

        while (end < len && text[end] != '\n') {
            end += 1;
        }
        int32 _i46_result;
        do {
            int32 *_i46_cursor = &cursor;

            char *text = file->tokens->text;
            int32 i = start;
            while (i < end && (csane_source_horizontal(text[i])
                               || csane_source_masked(file->tokens,
                                                      _i46_cursor, i))) {
                i += 1;
            }
            if (i >= end || text[i] != '#') {
                _i46_result = -1;
                goto _i46_done;
            }
            i += 1;
            while (i < end && (csane_source_horizontal(text[i])
                               || csane_source_masked(file->tokens,
                                                      _i46_cursor, i))) {
                i += 1;
            }
            if (i >= end || csane_source_masked(file->tokens, _i46_cursor, i)) {
                _i46_result = -1;
                goto _i46_done;
            }
            _i46_result = i;
            goto _i46_done;
            _i46_done: ;
        } while (false);
        keyword = _i46_result;
        if (csane_source_keyword(text, end, keyword, STRLIT("ifdef"))
            || csane_source_keyword(text, end, keyword, STRLIT("ifndef"))) {
            csane_rule_issue(file, start, STRLIT("PP-DEFINED"),
                             "use #if defined or #if !defined");
        }
        /* The Ryu rule is a raw-line check in Perl, not a parsed-directive
         * check. Comments between # and endif therefore do not qualify.
         */
        if (file->is_ryu) {
            int32 i = start;
            while (i < end && char_is_horizontal_space(text[i])) {
                i += 1;
            }
            if (i < end && text[i] == '#') {
                i += 1;
                while (i < end && char_is_horizontal_space(text[i])) {
                    i += 1;
                }
                if (csane_source_keyword(text, end, i, STRLIT("endif"))) {
                    for (int32 j = i + 5; j + 1 < end; j += 1) {
                        if (text[j] == '/' && text[j + 1] == '/') {
                            csane_rule_issue(file, j,
                                             STRLIT("RYU-ENDIF-COMMENT"),
                                             "use a block comment after "
                                             "#endif");
                            break;
                        }
                    }
                }
            }
        }
        start = end + (end < len);
    }
    return;
}


/* Avoid treating an isolated macro alias or keyword table entry as a
 * sizeof operator. The next token must be a plausible unary operand.
 */

static void
csane_rule_sizeof(CsaneFile *file) {
    if (file->is_ryu) {
        return;
    }
    for (int32 i = 0; i < ARRAY_LEN(file->tokens->tokens); i += 1) {
        Token *token = &file->tokens->tokens[i];
        bool _i47_predicate;
        _i47_predicate = !csane_token_text(token, STRLIT("sizeof"));
        if (!_i47_predicate) {
            _i47_predicate = token->kind != TOKEN_IDENT;
        }
        if (!_i47_predicate) {
            bool _i47_result;
            do {
                int32 _i47_token_index = i;

                CPreprocessorAnalysis *pp = file->preprocessor;
                int32 directive_index;
                CPreprocessorDirective *directive;
                CPreprocessorDefine define;
                Token *name;

                if (pp->token_directives == NULL) {
                    _i47_result = false;
                    goto _i47_done;
                }
                directive_index = pp->token_directives[_i47_token_index];
                if (directive_index < 0
                    || directive_index >= ARRAY_LEN(pp->directives)) {
                    _i47_result = false;
                    goto _i47_done;
                }
                directive = &pp->directives[directive_index];
                if (directive->kind != C_PREPROCESSOR_DIRECTIVE_DEFINE
                    || !c_preprocessor_define_info(file->tokens, directive,
                                                   &define)
                    || define.name_token < 0) {
                    _i47_result = false;
                    goto _i47_done;
                }
                name = &file->tokens->tokens[define.name_token];
                _i47_result = csane_token_text(name, STRLIT("SIZEOF"));
                goto _i47_done;
                _i47_done: ;
            } while (false);
            _i47_predicate = _i47_result;
        }
        if (!_i47_predicate) {
            bool _i48_result;
            do {
                int32 _i48_index = i;

                Tokenization *tokens = file->tokens;
                int32 next = tokenization_next_significant(tokens, _i48_index);
                Token *token;
                if (next >= ARRAY_LEN(tokens->tokens)) {
                    _i48_result = false;
                    goto _i48_done;
                }
                if ((file->preprocessor->token_roles != NULL)
                    && (file->preprocessor->token_roles[_i48_index]
                        == C_PP_TOKEN_MACRO_REPLACEMENT)
                    && (file->preprocessor->token_directives[_i48_index]
                        != file->preprocessor->token_directives[next])) {
                    _i48_result = false;
                    goto _i48_done;
                }
                token = &tokens->tokens[next];
                if ((token->kind == TOKEN_IDENT)
                    || (token->kind == TOKEN_LITERAL)) {
                    _i48_result = true;
                    goto _i48_done;
                }
                _i48_result = csane_token_text(token, STRLIT("("))
                       || csane_token_text(token, STRLIT("*"))
                       || csane_token_text(token, STRLIT("&"))
                       || csane_token_text(token, STRLIT("+"))
                       || csane_token_text(token, STRLIT("-"))
                       || csane_token_text(token, STRLIT("!"))
                       || csane_token_text(token, STRLIT("~"));
                goto _i48_done;
                _i48_done: ;
            } while (false);
            _i47_predicate = !_i48_result;
        }
        if (_i47_predicate) {
            continue;
        }
        /* Perl checks directive conditions and macro names as raw code too.
         * Exclude only the SIZEOF wrapper's own definition and vendored Ryu.
         */
        csane_rule_issue(file, token->offset, STRLIT("PP-SIZEOF"),
                         "use SIZEOF instead of sizeof");
    }
    return;
}

/* API checks use original tokens, even when unrelated syntax is opaque.
 * A function-like name is only a call when it is not a member, declarator,
 * macro parameter, or named top-level function declaration/definition.
 */
static bool
csane_api_known_type(CsaneFile *file, int32 index) {
    Token *token;
    if (index < 0 || index >= ARRAY_LEN(file->tokens->tokens)) {
        return false;
    }
    token = &file->tokens->tokens[index];
    return csane_type_name(token)
           || csane_type_hints_typedef(file->type_hints, token);
}



enum CsaneApiUse
csane_api_classify(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *name = &tokens->tokens[index];
    int32 next;
    int32 call_open;
    int32 before;
    TokenRange range;
    if (name->kind != TOKEN_IDENT) {
        return CSANE_API_NOT_CALL;
    }
    if (file->preprocessor->token_roles != NULL) {
        enum CPreprocessorTokenRole role =
            file->preprocessor->token_roles[index];
        if (role == C_PP_TOKEN_DIRECTIVE
            || role == C_PP_TOKEN_MACRO_PARAMETERS) {
            return CSANE_API_NOT_CALL;
        }
    }
    next = tokenization_next_significant(tokens, index);
    if (next >= ARRAY_LEN(tokens->tokens)) {
        return CSANE_API_NOT_CALL;
    }
    if (TOKEN_IS(&tokens->tokens[next], ")")) {
        int32 opening = tokenization_previous_significant(tokens, index);
        if (opening < 0 || !TOKEN_IS(&tokens->tokens[opening], "(")) {
            return CSANE_API_NOT_CALL;
        }
        next = tokenization_next_significant(tokens, next);
    }
    if (next >= ARRAY_LEN(tokens->tokens)
        || !TOKEN_IS(&tokens->tokens[next], "(")) {
        return CSANE_API_NOT_CALL;
    }
    call_open = next;
    if (file->preprocessor->token_roles != NULL) {
        enum CPreprocessorTokenRole role =
            file->preprocessor->token_roles[index];
        if (role == C_PP_TOKEN_MACRO_REPLACEMENT
            && file->preprocessor->token_directives[index]
               != file->preprocessor->token_directives[call_open]) {
            return CSANE_API_UNRESOLVED;
        }
    }
    before = tokenization_previous_significant(tokens, index);
    if (before >= 0
        && (TOKEN_IS(&tokens->tokens[before], ".")
            || TOKEN_IS(&tokens->tokens[before], "->"))) {
        return CSANE_API_NOT_CALL;
    }
    range = (TokenRange){0, ARRAY_LEN(tokens->tokens)};
    if (token_range_matching_delimiter_forward(tokens, range, call_open) < 0) {
        return CSANE_API_UNRESOLVED;
    }
    enum CsaneApiUse _i49_result;
    do {
        int32 _i49_name = index;

        Tokenization *tokens = file->tokens;
        int32 in_result_token;
        {
            CsaneFile *in_file = file;
            int32 in_index = _i49_name;

            Tokenization *tokens = in_file->tokens;
            int32 previous = tokenization_previous_significant(tokens,
                                                               in_index);
            while (previous >= 0 && in_file->preprocessor->token_roles != NULL
                   && in_file->preprocessor->token_roles[previous]
                          == C_PP_TOKEN_DIRECTIVE) {
                previous = tokenization_previous_significant(tokens, previous);
            }
            in_result_token = previous;
        }
        int32 before = in_result_token;
        if (before < 0) {
            _i49_result = CSANE_API_DIRECT_CALL;
            goto _i49_done;
        }
        if (csane_api_known_type(file, before)) {
            _i49_result = CSANE_API_NOT_CALL;
            goto _i49_done;
        }
        if (TOKEN_IS(&tokens->tokens[before], "*")) {
            int32 earlier = tokenization_previous_significant(tokens, before);
            if (earlier >= 0
                && (csane_api_known_type(file, earlier)
                    || TOKEN_IS(&tokens->tokens[earlier], "("))) {
                _i49_result = CSANE_API_NOT_CALL;
                goto _i49_done;
            }
        }
        for (int32 i = 0; i < ARRAY_LEN(file->toplevel->items); i += 1) {
            CsaneTopItem *item = &file->toplevel->items[i];
            if (item->name_token == _i49_name
                && (item->kind == CSANE_TOP_FUNCTION_DEFINITION
                    || item->kind == CSANE_TOP_FUNCTION_DECLARATION)) {
                _i49_result = CSANE_API_NOT_CALL;
                goto _i49_done;
            }
        }
        if (tokens->tokens[before].kind == TOKEN_IDENT
            && !TOKEN_IS(&tokens->tokens[before], "return")
            && !TOKEN_IS(&tokens->tokens[before], "case")
            && !TOKEN_IS(&tokens->tokens[before], "sizeof")) {
            _i49_result = CSANE_API_UNRESOLVED;
            goto _i49_done;
        }
        _i49_result = CSANE_API_DIRECT_CALL;
        goto _i49_done;
        _i49_done: ;
    } while (false);
    return _i49_result;
}

/* CG039: libc's printf-family calls belong in cbase wrappers, not in
 * project code. This includes ISO C, POSIX/GNU, and wide-character forms.
 * Match exact identifiers so project formatters such as fmt_printf and
 * str_printf are unaffected. */
static void
csane_rule_calls(CsaneFile *file) {
    for (int32 i = 0; i < ARRAY_LEN(file->tokens->tokens); i += 1) {
        Token *token = &file->tokens->tokens[i];
        bool never;
        CsanePaths *names = &file->policy->cbase_names;
        bool restricted;
        bool libc_printf;

        if (token->kind != TOKEN_IDENT) {
            continue;
        }
        never = csane_token_name_in(token, &file->policy->never_names);
        restricted = !file->in_cbase && csane_token_name_in(token, names);
        libc_printf = !file->in_cbase
                      && (TOKEN_IS(token, "printf")
                          || TOKEN_IS(token, "fprintf")
                          || TOKEN_IS(token, "sprintf")
                          || TOKEN_IS(token, "snprintf")
                          || TOKEN_IS(token, "vprintf")
                          || TOKEN_IS(token, "vfprintf")
                          || TOKEN_IS(token, "vsprintf")
                          || TOKEN_IS(token, "vsnprintf")
                          || TOKEN_IS(token, "dprintf")
                          || TOKEN_IS(token, "vdprintf")
                          || TOKEN_IS(token, "asprintf")
                          || TOKEN_IS(token, "vasprintf")
                          || TOKEN_IS(token, "wprintf")
                          || TOKEN_IS(token, "fwprintf")
                          || TOKEN_IS(token, "swprintf")
                          || TOKEN_IS(token, "vwprintf")
                          || TOKEN_IS(token, "vfwprintf")
                          || TOKEN_IS(token, "vswprintf"));
        if ((!never && !restricted && !libc_printf)
            || csane_api_classify(file, i) != CSANE_API_DIRECT_CALL) {
            continue;
        }
        if (never) {
            csane_rule_issue(file, token->offset, STRLIT("API-NEVER"),
                             "function must never be used");
        } else if (restricted) {
            csane_rule_issue(file, token->offset, STRLIT("API-CBASE-ONLY"),
                             "function is allowed only inside cbase");
        } else {
            csane_rule_issue(file, token->offset,
                             STRLIT("API-LIBC-PRINTF"),
                             "use cbase formatting instead of libc printf");
        }
    }
}

/* Analysis passes require only the source and its local token structures.
 * The ordering within each pass retains
 * the legacy dispatch order and its rule-specific exemptions. */
static CsaneRule csane_rules[] = {
    {STRLIT("SRC"), csane_rule_source, CSANE_PASS_SOURCE,
     CSANE_TARGET_LINES, false},
    {STRLIT("PP"), csane_rule_preprocessor, CSANE_PASS_TOKEN,
     CSANE_TARGET_DIRECTIVES, false},
    {STRLIT("PP-SIZEOF"), csane_rule_sizeof, CSANE_PASS_TOKEN,
     CSANE_TARGET_SIZEOF, false},
    {STRLIT("AST-DECL-ONE"), csane_declarations_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_DECL_ONE, false},
    {STRLIT("API"), csane_rule_calls, CSANE_PASS_TOKEN,
     CSANE_TARGET_APIS, true},
    {STRLIT("CONTROL"), csane_control_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CONTROL, false},
    {STRLIT("LAYOUT"), csane_layout_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_OPERATORS, false},
    {STRLIT("ASSIGN-EQ"), csane_layout_assignment_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_ASSIGNMENTS, false},
    {STRLIT("FUNC-HEADERS"), csane_declarations_headers, CSANE_PASS_TOKEN,
     CSANE_TARGET_FUNC_HEADERS, false},
    {STRLIT("STYLE-STATIC-ONCE"), csane_single_use_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_STATIC_SINGLE_USE, false},
    {STRLIT("CALL-BASIC"), csane_calls_token_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CALLS, false},
    {STRLIT("STYLE-EXPRESSIONS"), csane_expression_style_check,
     CSANE_PASS_TOKEN, CSANE_TARGET_STYLE_CALLS, false},
    {STRLIT("STYLE-BOUNDS"), csane_bounds_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_STATEMENTS, false},
    {STRLIT("STYLE-STATUS"), csane_status_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_STYLE_STATUS, false},
    {STRLIT("CALL-WRAP"), csane_wrap_call_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CALL_WRAP, false},
    {STRLIT("ASSIGN-WRAP"), csane_wrap_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_ASSIGN_WRAP, false},
};

void
csane_rules_prepare(CsaneFile *file) {
    ASSERT(file != NULL);
    ASSERT(file->tokens != NULL);
    file->violations = 0;
    file->in_cbase = csane_policy_in_cbase(file->policy, file->path,
                                           file->path_len);
    file->is_ryu = csane_policy_in_ryu(file->policy, file->path,
                                       file->path_len);
    file->column_limit = csane_policy_columns(file->policy, file->path,
                                              file->path_len);
}

void
csane_rules_run(CsaneFile *file, enum CsanePass pass) {
    bool fixture;

    ASSERT(file != NULL);
    fixture = csane_fixture_path(file->path, file->path_len);
    for (int32 i = 0; i < LENGTH(csane_rules); i += 1) {
        CsaneRule *rule = &csane_rules[i];
        if (rule->pass == pass
            && (!fixture || rule->include_fixtures)) {
            rule->check(file);
        }
    }
}

/* Each group reports its own eligible and unresolved local targets.
 * File exclusions are distinct from zero eligible targets. */

int32
csane_rules_incomplete(CsaneFile *file, bool report, bool report_unresolved,
                       CsaneRuleCounts *totals) {
    int32 incomplete = 0;
    CsaneRuleCounts sum = {0};
    for (int32 i = 0; i < LENGTH(csane_rules); i += 1) {
        CsaneRule *rule = &csane_rules[i];
        CsaneRuleCounts counts =
            csane_coverage_rule(file, rule->target, rule->id,
                                rule->id_len, report_unresolved);
        bool in_result_excluded;
        do {
            int32 len = file->path_len;
            bool generated = ENDS_WITH(file->path, len, ".meta.h");
            if (csane_fixture_path(file->path, file->path_len)
                && !rule->include_fixtures) {
                in_result_excluded = true;
                break;
            }
            if (generated
                && (STREQUAL(rule->id, rule->id_len, "LAYOUT")
                    || STREQUAL(rule->id, rule->id_len, "AST-DECL-ONE")
                    || STREQUAL(rule->id, rule->id_len, "FUNC-HEADERS")
                    || STREQUAL(rule->id, rule->id_len, "CALL-BASIC")
                    || STREQUAL(rule->id, rule->id_len, "CALL-WRAP")
                    || STREQUAL(rule->id, rule->id_len, "ASSIGN-WRAP"))) {
                in_result_excluded = true;
                break;
            }
            if (file->is_ryu
                && (STREQUAL(rule->id, rule->id_len, "LAYOUT")
                    || STREQUAL(rule->id, rule->id_len, "AST-DECL-ONE")
                    || STREQUAL(rule->id, rule->id_len, "CONTROL")
                    || STREQUAL(rule->id, rule->id_len, "PP-SIZEOF")
                    || STREQUAL(rule->id, rule->id_len, "STYLE-BOUNDS")
                    || STREQUAL(rule->id, rule->id_len, "ASSIGN-EQ")
                    || STREQUAL(rule->id, rule->id_len, "ASSIGN-WRAP"))) {
                in_result_excluded = true;
                break;
            }
            in_result_excluded = false;
        } while (false);
        bool excluded = in_result_excluded;
        char *status = "checked";

        if (excluded) {
            counts.excluded = counts.eligible;
            counts.checked = 0;
            counts.unresolved = 0;
        }
        sum.eligible += counts.eligible;
        sum.checked += counts.checked;
        sum.excluded += counts.excluded;
        sum.unresolved += counts.unresolved;
        if (excluded) {
            status = "excluded";
        } else if (counts.unresolved > 0) {
            status = counts.checked > 0 ? "partial" : "not_checked";
            incomplete += 1;
        }
        if (report) {
            fmt_printf("rule_coverage file=%.*s group=%.*s "
                       "status=%s pass=%d "
                       "eligible=%d checked=%d excluded=%d unresolved=%d\n",
                       file->path_len, file->path, rule->id_len,
                       rule->id, status, (int32)rule->pass,
                       counts.eligible, counts.checked, counts.excluded,
                       counts.unresolved);
        }
    }
    if (totals) {
        *totals = sum;
    }
    return incomplete;
}

#endif /* CSANECHECK_RULES_C */
