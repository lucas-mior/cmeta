// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(CSANECHECK_RULES_C)
#define CSANECHECK_RULES_C

#include "cbase.h"
#include "csanecheck.h"

static bool
csane_name_in(Token *token, CsanePaths *names) {
    if (token->kind != TOKEN_IDENT) {
        return false;
    }
    for (int32 i = 0; i < names->count; i += 1) {
        int32 len = strlen32(names->items[i]);
        if ((len == token->len)
            && !memcmp64(token->text, names->items[i], len)) {
            return true;
        }
    }
    return false;
}

static bool
csane_token_text(Token *token, char *text) {
    int32 len = strlen32(text);
    return token->len == len && !memcmp64(token->text, text, len);
}

static bool
csane_fixture_path(char *path) {
    int32 len = strlen32(path);
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
    if (csane_policy_rule_allowed(file->policy, file->path, code)) {
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

static void
csane_rule_preprocessor(CsaneFile *file) {
    CPreprocessorAnalysis *pp = file->preprocessor;
    for (int32 i = 0; i < pp->directive_count; i += 1) {
        CPreprocessorDirective *directive = &pp->directives[i];
        if ((directive->kind == C_PREPROCESSOR_DIRECTIVE_IFDEF)
            || (directive->kind == C_PREPROCESSOR_DIRECTIVE_IFNDEF)) {
            csane_rule_issue(file, directive->source.start, "PP-DEFINED",
                             "use #if defined or #if !defined");
        }
        if (file->is_ryu
            && directive->kind == C_PREPROCESSOR_DIRECTIVE_ENDIF) {
            int32 start = directive->source.start;
            int32 end = directive->source.end;
            for (int32 j = start; j + 1 < end; j += 1) {
                if (file->tokens->text[j] == '\n') {
                    break;
                }
                if (file->tokens->text[j] == '/'
                    && file->tokens->text[j + 1] == '/') {
                    csane_rule_issue(file, j, "RYU-ENDIF-COMMENT",
                                     "use a block comment after #endif");
                    break;
                }
            }
        }
    }
    return;
}

static bool
csane_in_sizeof_definition(CsaneFile *file, int32 token_index) {
    CPreprocessorAnalysis *pp = file->preprocessor;
    int32 directive_index;
    CPreprocessorDirective *directive;
    CPreprocessorDefine define;
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
    Token *name = &file->tokens->tokens[define.name_token];
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
        /* A keyword in a directive's condition is not C expression syntax.
         * Macro replacement lists are intentionally checked.
         */
        if (file->preprocessor->token_roles != NULL
            && file->preprocessor->token_roles[i] == C_PP_TOKEN_DIRECTIVE) {
            continue;
        }
        csane_rule_issue(file, token->offset, "PP-SIZEOF",
                         "use SIZEOF instead of sizeof");
    }
    return;
}

/* Inspect an ordinary compound body embedded in a statement-shaped
 * macro. The replacement is never expanded or evaluated.
 */
static void
csane_parse_macro_block(CsaneFile *file, TokenRange replacement) {
    for (int32 i = replacement.first; i < replacement.end; i += 1) {
        if (csane_token_text(&file->tokens->tokens[i], "{")) {
            int32 close;
            close = token_range_matching_delimiter_forward(file->tokens,
                                                           replacement, i);
            if (close >= 0) {
                TokenRange block = {i, close + 1};
                c_ast_parse_statement(file->ast, block, NULL);
            }
            break;
        }
    }
    return;
}

static void
csane_rule_legacy_ast_preparation(CsaneFile *file) {
    CAst *ast = file->ast;
    CPreprocessorAnalysis *pp = file->preprocessor;
    int32 saved_issues = ast->issue_count;

    /* The declaration parser retains initializers and array bounds as token
     * ranges. Parse them here so direct calls in those constructs are checked
     * just like direct calls in statements, without copying source text.
     */
    for (int32 i = 0; i < ast->declarator_count; i += 1) {
        CDeclaratorNode *declarator = &ast->declarators[i];
        TokenRange range = declarator->extra;
        if (token_range_is_empty(file->tokens, range)) {
            continue;
        }
        c_ast_parse_initializer(ast, range);
    }

    /* Expression-shaped macros can contain direct calls despite being outside
     * ordinary translation-unit syntax. Parse them with the same AST parser,
     * without expanding them or guessing their replacement semantics.
     */
    for (int32 i = 0; i < pp->directive_count; i += 1) {
        CPreprocessorDirective *directive = &pp->directives[i];
        CPreprocessorDefine define;
        if (directive->kind == C_PREPROCESSOR_DIRECTIVE_DEFINE
            && c_preprocessor_define_info(file->tokens, directive, &define)
            && !token_range_is_empty(file->tokens, define.replacement)) {
            CExprParseResult result =
                c_ast_parse_expression(ast, define.replacement);
            if (result.status != C_EXPR_PARSE_OK) {
                csane_parse_macro_block(file, define.replacement);
            }
        }
    }
    /* Preserve the old macro/initializer AST side effects for rules that
     * have not migrated yet. API diagnostics never use this representation.
     */
    ast->issue_count = saved_issues;
}

/* API checks use original tokens, even when unrelated syntax is opaque.
 * A function-like name is only a call when it is not a member, declarator,
 * macro parameter, or named top-level function declaration/definition.
 */
static bool
csane_api_declarator(CsaneFile *file, int32 name) {
    Tokenization *tokens = file->tokens;
    int32 before = tokenization_previous_significant(tokens, name);
    if (before < 0) {
        return false;
    }
    Token *previous = &tokens->tokens[before];
    if (csane_type_name(previous)) {
        return true;
    }
    if (TOKEN_IS(previous, "*")) {
        int32 earlier = tokenization_previous_significant(tokens, before);
        if (earlier >= 0
            && (csane_type_name(&tokens->tokens[earlier])
                || TOKEN_IS(&tokens->tokens[earlier], "("))) {
            return true;
        }
    }
    for (int32 i = 0; i < file->toplevel->count; i += 1) {
        CsaneTopItem *item = &file->toplevel->items[i];
        if (item->name_token == name
            && (item->kind == CSANE_TOP_FUNCTION_DEFINITION
                || item->kind == CSANE_TOP_FUNCTION_DECLARATION)) {
            return true;
        }
    }
    return false;
}

static bool
csane_api_direct_call(CsaneFile *file, int32 index) {
    Tokenization *tokens = file->tokens;
    Token *name = &tokens->tokens[index];
    int32 next = tokenization_next_significant(tokens, index);
    if (name->kind != TOKEN_IDENT || next >= tokens->token_count) {
        return false;
    }
    if (TOKEN_IS(&tokens->tokens[next], ")")) {
        int32 opening = tokenization_previous_significant(tokens, index);
        if (opening < 0 || !TOKEN_IS(&tokens->tokens[opening], "(")) {
            return false;
        }
        next = tokenization_next_significant(tokens, next);
    }
    if (next >= tokens->token_count
        || !TOKEN_IS(&tokens->tokens[next], "(")) {
        return false;
    }
    if (file->preprocessor->token_roles != NULL) {
        int32 role = file->preprocessor->token_roles[index];
        if (role == C_PP_TOKEN_DIRECTIVE
            || role == C_PP_TOKEN_MACRO_PARAMETERS) {
            return false;
        }
        if (role == C_PP_TOKEN_MACRO_REPLACEMENT
            && file->preprocessor->token_directives[index]
               != file->preprocessor->token_directives[next]) {
            return false;
        }
    }
    int32 before = tokenization_previous_significant(tokens, index);
    if (before >= 0
        && (TOKEN_IS(&tokens->tokens[before], ".")
            || TOKEN_IS(&tokens->tokens[before], "->"))) {
        return false;
    }
    return !csane_api_declarator(file, index);
}

static void
csane_rule_calls(CsaneFile *file) {
    for (int32 i = 0; i < file->tokens->token_count; i += 1) {
        Token *token = &file->tokens->tokens[i];
        bool never = csane_name_in(token, &file->policy->never_names);
        bool restricted = !file->in_cbase
                          && csane_name_in(token, &file->policy->cbase_names);
        if ((!never && !restricted) || !csane_api_direct_call(file, i)) {
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

/* Analysis passes are independent. A source rule never requires a parser;
 * a token rule never requires an AST. The ordering within each pass retains
 * the legacy dispatch order and its rule-specific exemptions. */
static CsaneRule csane_rules[] = {
    {"SRC", csane_rule_source, CSANE_PASS_SOURCE, false},
    {"PP", csane_rule_preprocessor, CSANE_PASS_TOKEN, false},
    {"PP-SIZEOF", csane_rule_sizeof, CSANE_PASS_TOKEN, false},
    {"AST-DECL-ONE", csane_declarations_check, CSANE_PASS_TOKEN, false},
    {"API", csane_rule_calls, CSANE_PASS_TOKEN, true},
    {"CONTROL", csane_control_check, CSANE_PASS_TOKEN, false},
    {"LAYOUT", csane_layout_check, CSANE_PASS_SYNTAX, false},
    {"ASSIGN-EQ", csane_layout_assignment_check, CSANE_PASS_TOKEN, false},
    {"FUNC-HEADERS", csane_declarations_headers, CSANE_PASS_TOKEN, false},
    {"CALL-BASIC", csane_calls_token_check, CSANE_PASS_TOKEN, false},
    {"STYLE", csane_context_check, CSANE_PASS_CONTEXT, false},
    {"CALL-WRAP", csane_wrap_call_check, CSANE_PASS_TOKEN, false},
    {"ASSIGN-WRAP", csane_wrap_check, CSANE_PASS_TOKEN, false},
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
    ASSERT(file != NULL);
    bool fixture = csane_fixture_path(file->path);
    if (pass == CSANE_PASS_SYNTAX) {
        csane_rule_legacy_ast_preparation(file);
    }
    for (int32 i = 0; i < (int32)(SIZEOF(csane_rules)/SIZEOF(csane_rules[0]));
         i += 1) {
        CsaneRule *rule = &csane_rules[i];
        if (rule->pass == pass
            && (!fixture || rule->include_fixtures)) {
            rule->check(file);
        }
    }
}

/* "checked" describes whether the pass had its required representation,
 * not a proof that every syntactic pattern was recognized. For rule-local
 * coverage, this coarse result is a lower-confidence estimate. */
void
csane_rules_report(CsaneFile *file, CsaneAnalysisCoverage *coverage) {
    bool fixture = csane_fixture_path(file->path);
    for (int32 i = 0; i < (int32)(SIZEOF(csane_rules)/SIZEOF(csane_rules[0]));
         i += 1) {
        CsaneRule *rule = &csane_rules[i];
        char *status = "checked";
        if (fixture && !rule->include_fixtures) {
            status = "excluded";
        } else if (rule->pass == CSANE_PASS_TOKEN) {
            if (!coverage->preprocessor_ready
                || coverage->preprocessor_issues > 0) {
                status = "partial";
            }
        } else if (rule->pass >= CSANE_PASS_SYNTAX) {
            if (!coverage->ast_ready
                || (coverage->eligible_tokens > 0
                    && coverage->parsed_tokens == 0)) {
                status = "not_checked";
            } else if (coverage->unresolved_tokens > 0
                       || coverage->macro_tokens > 0
                       || coverage->parse_issues > 0
                       || coverage->preprocessor_issues > 0) {
                status = "partial";
            }
        }
        printf("rule_coverage file=%s group=%s status=%s pass=%d "
               "eligible_tokens=%d parsed_tokens=%d unresolved_tokens=%d "
               "macro_tokens=%d\n",
               file->path, rule->id, status, (int32)rule->pass,
               coverage->eligible_tokens,
               coverage->parsed_tokens, coverage->unresolved_tokens,
               coverage->macro_tokens);
    }
}

#endif /* CSANECHECK_RULES_C */
