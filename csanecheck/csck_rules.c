// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_RULES_C)
#define CSANECHECK_RULES_C

#include "cbase.h"
#include "csck.h"


static bool
csane_token_text(Token *token, char *text) {
    int32 len = strlen32(text);
    return token->len == len && !memcmp64(token->text, text, len);
}

static bool
csane_fixture_path(char *path, int32 len) {
    for (int32 i = 0; i + 15 <= len; i += 1) {
        if ((i == 0 || path[i - 1] == '/')
            && !memcmp64(path + i, "tests/fixtures/", 15)) {
            return true;
        }
    }
    return false;
}

void
csane_rule_issue(CsaneFile *file, int32 offset, char *code, char *message) {
    SourceLocation loc;
    if (csane_policy_rule_allowed(file->policy, file->path, code, NULL)) {
        return;
    }
    if (offset < 0) {
        offset = 0;
    }
    if (offset > file->tokens->text_len) {
        offset = file->tokens->text_len;
    }
    loc = tokenization_source_location(file->tokens, offset);
    printf("%s:%d:%d: error[%s]: %s\n",
           file->path, loc.line, loc.column + 1, code, message);
    file->violations += 1;
    return;
}

/* Count UTF-8 scalar starts, not bytes, as the legacy checker does. Invalid
 * UTF-8 is conservatively treated as single-byte characters; encoding
 * validity itself is outside this phase's rule set.
 */
static int32
csane_columns(char *text, int32 len) {
    int32 count = 0;
    for (int32 i = 0; i < len; i += 1) {
        if (((uint8)text[i] & 0xc0) != 0x80) {
            count += 1;
        }
    }
    return count;
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
        if ((file->column_limit > 0)
            && (end - start > file->column_limit)
            && (csane_columns(text + start, end - start)
                > file->column_limit)) {
            csane_rule_issue(file, start, "SRC-COLUMNS",
                             "line exceeds allowed column limit");
        }
        if (tab >= 0) {
            csane_rule_issue(file, tab, "SRC-TABS",
                             "tab character is forbidden");
        }
        if (trailing < end) {
            csane_rule_issue(file, trailing, "SRC-TRAILING",
                             "trailing whitespace");
        }
        start = end + (end < len);
    }
    if (file->is_ryu && (len > 0) && text[len - 1] != '\n') {
        csane_rule_issue(file, len - 1, "RYU-FINAL-NEWLINE",
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

    while (*cursor < tokens->token_count
           && tokens->tokens[*cursor].offset
              + tokens->tokens[*cursor].len <= offset) {
        *cursor += 1;
    }
    if (*cursor >= tokens->token_count) {
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
static int32
csane_source_directive(CsaneFile *file, int32 start, int32 end,
                       int32 *cursor) {
    char *text = file->tokens->text;
    int32 i = start;
    while (i < end && (csane_source_horizontal(text[i])
                       || csane_source_masked(file->tokens, cursor, i))) {
        i += 1;
    }
    if (i >= end || text[i] != '#') {
        return -1;
    }
    i += 1;
    while (i < end && (csane_source_horizontal(text[i])
                       || csane_source_masked(file->tokens, cursor, i))) {
        i += 1;
    }
    if (i >= end || csane_source_masked(file->tokens, cursor, i)) {
        return -1;
    }
    return i;
}

static bool
csane_source_keyword(char *text, int32 end, int32 index, char *word) {
    int32 length = strlen32(word);
    return index >= 0 && index + length <= end
           && !memcmp64(text + index, word, length)
           && (index + length == end
               || !char_is_identifier_body(text[index + length]));
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
        keyword = csane_source_directive(file, start, end, &cursor);
        if (csane_source_keyword(text, end, keyword, "ifdef")
            || csane_source_keyword(text, end, keyword, "ifndef")) {
            csane_rule_issue(file, start, "PP-DEFINED",
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
                if (csane_source_keyword(text, end, i, "endif")) {
                    for (int32 j = i + 5; j + 1 < end; j += 1) {
                        if (text[j] == '/' && text[j + 1] == '/') {
                            csane_rule_issue(file, j, "RYU-ENDIF-COMMENT",
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

static bool
csane_in_sizeof_definition(CsaneFile *file, int32 token_index) {
    CPreprocessorAnalysis *pp = file->preprocessor;
    int32 directive_index;
    CPreprocessorDirective *directive;
    CPreprocessorDefine define;
    Token *name;

    if (pp->token_directives == NULL) {
        return false;
    }
    directive_index = pp->token_directives[token_index];
    if (directive_index < 0 || directive_index >= pp->directive_count) {
        return false;
    }
    directive = &pp->directives[directive_index];
    if (directive->kind != C_PREPROCESSOR_DIRECTIVE_DEFINE
        || !c_preprocessor_define_info(file->tokens, directive, &define)
        || define.name_token < 0) {
        return false;
    }
    name = &file->tokens->tokens[define.name_token];
    return csane_token_text(name, "SIZEOF");
}

/* Avoid treating an isolated macro alias or keyword table entry as a
 * sizeof operator. The next token must be a plausible unary operand.
 */
static bool
csane_sizeof_has_operand(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    int32 next = tokenization_next_significant(tokens, index);
    Token *token;
    if (next >= tokens->token_count) {
        return false;
    }
    if ((file->preprocessor->token_roles != NULL)
        && (file->preprocessor->token_roles[index]
            == C_PP_TOKEN_MACRO_REPLACEMENT)
        && (file->preprocessor->token_directives[index]
            != file->preprocessor->token_directives[next])) {
        return false;
    }
    token = &tokens->tokens[next];
    if ((token->kind == TOKEN_IDENT) || (token->kind == TOKEN_LITERAL)) {
        return true;
    }
    return csane_token_text(token, "(") || csane_token_text(token, "*")
           || csane_token_text(token, "&") || csane_token_text(token, "+")
           || csane_token_text(token, "-") || csane_token_text(token, "!")
           || csane_token_text(token, "~");
}

static void
csane_rule_sizeof(CsaneFile *file) {
    if (file->is_ryu) {
        return;
    }
    for (int32 i = 0; i < file->tokens->token_count; i += 1) {
        Token *token = &file->tokens->tokens[i];
        if (!csane_token_text(token, "sizeof")
            || (token->kind != TOKEN_IDENT)
            || csane_in_sizeof_definition(file, i)
            || !csane_sizeof_has_operand(file, i)) {
            continue;
        }
        /* Perl checks directive conditions and macro names as raw code too.
         * Exclude only the SIZEOF wrapper's own definition and vendored Ryu.
         */
        csane_rule_issue(file, token->offset, "PP-SIZEOF",
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
    if (index < 0 || index >= file->tokens->token_count) {
        return false;
    }
    token = &file->tokens->tokens[index];
    return csane_type_name(token)
           || csane_type_hints_typedef(file->type_hints, token);
}

static int32
csane_api_previous_code_token(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    int32 previous = tokenization_previous_significant(tokens, index);
    while (previous >= 0 && file->preprocessor->token_roles != NULL
           && file->preprocessor->token_roles[previous]
                  == C_PP_TOKEN_DIRECTIVE) {
        previous = tokenization_previous_significant(tokens, previous);
    }
    return previous;
}

static enum CsaneApiUse
csane_api_declarator(CsaneFile *file, int32 name) {
    Tokenization *tokens = file->tokens;
    int32 before = csane_api_previous_code_token(file, name);
    if (before < 0) {
        return CSANE_API_DIRECT_CALL;
    }
    if (csane_api_known_type(file, before)) {
        return CSANE_API_NOT_CALL;
    }
    if (TOKEN_IS(&tokens->tokens[before], "*")) {
        int32 earlier = tokenization_previous_significant(tokens, before);
        if (earlier >= 0
            && (csane_api_known_type(file, earlier)
                || TOKEN_IS(&tokens->tokens[earlier], "("))) {
            return CSANE_API_NOT_CALL;
        }
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->name_token == name
            && (item->kind == CSANE_TOP_FUNCTION_DEFINITION
                || item->kind == CSANE_TOP_FUNCTION_DECLARATION)) {
            return CSANE_API_NOT_CALL;
        }
    }
    if (tokens->tokens[before].kind == TOKEN_IDENT
        && !TOKEN_IS(&tokens->tokens[before], "return")
        && !TOKEN_IS(&tokens->tokens[before], "case")
        && !TOKEN_IS(&tokens->tokens[before], "sizeof")) {
        return CSANE_API_UNRESOLVED;
    }
    return CSANE_API_DIRECT_CALL;
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
    if (next >= tokens->token_count) {
        return CSANE_API_NOT_CALL;
    }
    if (TOKEN_IS(&tokens->tokens[next], ")")) {
        int32 opening = tokenization_previous_significant(tokens, index);
        if (opening < 0 || !TOKEN_IS(&tokens->tokens[opening], "(")) {
            return CSANE_API_NOT_CALL;
        }
        next = tokenization_next_significant(tokens, next);
    }
    if (next >= tokens->token_count || !TOKEN_IS(&tokens->tokens[next], "(")) {
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
    range = (TokenRange){0, tokens->token_count};
    if (token_range_matching_delimiter_forward(tokens, range, call_open) < 0) {
        return CSANE_API_UNRESOLVED;
    }
    return csane_api_declarator(file, index);
}

static void
csane_rule_calls(CsaneFile *file) {
    for (int32 i = 0; i < file->tokens->token_count; i += 1) {
        Token *token = &file->tokens->tokens[i];
        bool never = csane_token_name_in(token, &file->policy->never_names);
        CsanePaths *names = &file->policy->cbase_names;
        bool restricted = !file->in_cbase
                          && csane_token_name_in(token, names);
        if ((!never && !restricted)
            || csane_api_classify(file, i) != CSANE_API_DIRECT_CALL) {
            continue;
        }
        if (never) {
            csane_rule_issue(file, token->offset, "API-NEVER",
                             "function must never be used");
        } else {
            csane_rule_issue(file, token->offset, "API-CBASE-ONLY",
                             "function is allowed only inside cbase");
        }
    }
}

/* Analysis passes require only the source and its local token structures.
 * The ordering within each pass retains
 * the legacy dispatch order and its rule-specific exemptions. */
static CsaneRule csane_rules[] = {
    {"SRC", csane_rule_source, CSANE_PASS_SOURCE,
     CSANE_TARGET_LINES, false},
    {"PP", csane_rule_preprocessor, CSANE_PASS_TOKEN,
     CSANE_TARGET_DIRECTIVES, false},
    {"PP-SIZEOF", csane_rule_sizeof, CSANE_PASS_TOKEN,
     CSANE_TARGET_SIZEOF, false},
    {"AST-DECL-ONE", csane_declarations_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_DECL_ONE, false},
    {"API", csane_rule_calls, CSANE_PASS_TOKEN,
     CSANE_TARGET_APIS, true},
    {"CONTROL", csane_control_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CONTROL, false},
    {"LAYOUT", csane_layout_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_OPERATORS, false},
    {"ASSIGN-EQ", csane_layout_assignment_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_ASSIGNMENTS, false},
    {"FUNC-HEADERS", csane_declarations_headers, CSANE_PASS_TOKEN,
     CSANE_TARGET_FUNC_HEADERS, false},
    {"CALL-BASIC", csane_calls_token_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CALLS, false},
    {"STYLE-EXPRESSIONS", csane_expression_style_check,
     CSANE_PASS_TOKEN, CSANE_TARGET_STYLE_CALLS, false},
    {"STYLE-BOUNDS", csane_bounds_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_STATEMENTS, false},
    {"STYLE-STATUS", csane_status_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_STYLE_STATUS, false},
    {"CALL-WRAP", csane_wrap_call_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_CALL_WRAP, false},
    {"ASSIGN-WRAP", csane_wrap_check, CSANE_PASS_TOKEN,
     CSANE_TARGET_ASSIGN_WRAP, false},
};

void
csane_rules_prepare(CsaneFile *file) {
    ASSERT(file != NULL);
    ASSERT(file->tokens != NULL);
    file->violations = 0;
    file->in_cbase = csane_policy_in_cbase(file->policy, file->path);
    file->is_ryu = csane_policy_in_ryu(file->policy, file->path);
    file->column_limit = csane_policy_columns(file->policy, file->path);
}

void
csane_rules_run(CsaneFile *file, enum CsanePass pass) {
    bool fixture;

    ASSERT(file != NULL);
    fixture = csane_fixture_path(file->path, file->path_len);
    for (int32 i = 0; i < (int32)(SIZEOF(csane_rules)/SIZEOF(csane_rules[0]));
         i += 1) {
        CsaneRule *rule = &csane_rules[i];
        if (rule->pass == pass
            && (!fixture || rule->include_fixtures)) {
            rule->check(file);
        }
    }
}

/* Each group reports its own eligible and unresolved local targets.
 * File exclusions are distinct from zero eligible targets. */
static bool
csane_rule_excluded(CsaneFile *file, CsaneRule *rule) {
    int32 len = file->path_len;
    bool generated = len >= 7
                     && !memcmp64(file->path + len - 7, ".meta.h", 7);
    if (csane_fixture_path(file->path, file->path_len) && !rule->include_fixtures) {
        return true;
    }
    if (generated && (!strcmp(rule->id, "LAYOUT")
                      || !strcmp(rule->id, "AST-DECL-ONE")
                      || !strcmp(rule->id, "FUNC-HEADERS")
                      || !strcmp(rule->id, "CALL-BASIC")
                      || !strcmp(rule->id, "CALL-WRAP")
                      || !strcmp(rule->id, "ASSIGN-WRAP"))) {
        return true;
    }
    if (file->is_ryu && (!strcmp(rule->id, "LAYOUT")
                         || !strcmp(rule->id, "AST-DECL-ONE")
                         || !strcmp(rule->id, "CONTROL")
                         || !strcmp(rule->id, "PP-SIZEOF")
                         || !strcmp(rule->id, "STYLE-BOUNDS")
                         || !strcmp(rule->id, "ASSIGN-EQ")
                         || !strcmp(rule->id, "ASSIGN-WRAP"))) {
        return true;
    }
    return false;
}

int32
csane_rules_incomplete(CsaneFile *file, bool report, bool report_unresolved,
                       CsaneRuleCounts *totals) {
    int32 incomplete = 0;
    CsaneRuleCounts sum = {0};
    for (int32 i = 0; i < (int32)(SIZEOF(csane_rules)/SIZEOF(csane_rules[0]));
         i += 1) {
        CsaneRule *rule = &csane_rules[i];
        CsaneRuleCounts counts =
            csane_coverage_rule(file, rule->target, rule->id,
                                report_unresolved);
        bool excluded = csane_rule_excluded(file, rule);
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
            printf("rule_coverage file=%s group=%s status=%s pass=%d "
                   "eligible=%d checked=%d excluded=%d unresolved=%d\n",
                   file->path, rule->id, status, (int32)rule->pass,
                   counts.eligible, counts.checked, counts.excluded,
                   counts.unresolved);
        }
    }
    if (totals != NULL) {
        *totals = sum;
    }
    return incomplete;
}

#endif /* CSANECHECK_RULES_C */
