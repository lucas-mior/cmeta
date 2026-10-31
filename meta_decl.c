// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_DECL_C)
#define META_DECL_C

#if !defined(TESTING_meta_decl)
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_meta_decl 1
#else
#define TESTING_meta_decl 0
#endif
#endif

#include "cbase.h"
#include "meta.h"
#include "meta_ast.h"

static void *c_ast_storage_grow(void *, int32 *, int32, int32);

typedef struct DeclCursor {
    CAst *ast;
    TokenRange range;
    int32 pos;
    int32 issue;
    int32 depth;
    int32 inferred_type;
    enum CDeclParseStatus status;
} DeclCursor;

static int32
decl_sig(DeclCursor *c, int32 pos) {
    return token_range_next_significant(c->ast->tokenization, c->range,
                                        pos - 1);
}

static bool
decl_at(DeclCursor *c, int32 i, char *text) {
    return (i < c->range.end)
           && TOKEN_IS(&c->ast->tokenization->tokens[i], text,
                       strlen32(text));
}

static int32
decl_next(DeclCursor *c) {
    return decl_sig(c, c->pos);
}

static void
decl_fail(DeclCursor *c, enum CDeclParseStatus status, int32 at) {
    if (c->status == C_DECL_PARSE_OK) {
        c->status = status;
        c->issue = at;
    }
    return;
}

static int32
decl_make_node(DeclCursor *c, enum CDeclaratorKind kind,
               int32 first, int32 end, int32 inner, int32 name) {
    CAst *ast = c->ast;
    int32 index = ast->declarator_count;
    ast->declarators =
        c_ast_storage_grow(ast->declarators, &ast->declarator_capacity,
                           index, SIZEOF(CDeclaratorNode));
    ast->declarators[index] = (CDeclaratorNode){
        .tokens = {first, end}, .extra = {end, end},
        .kind = kind, .inner = inner, .next = -1,
        .name_token = name, .first_parameter = -1,
    };
    ast->declarator_count += 1;
    return index;
}

static int32
decl_make_declaration(DeclCursor *c, enum CDeclarationKind kind) {
    CAst *ast = c->ast;
    int32 index = ast->declaration_count;
    ast->declarations =
        c_ast_storage_grow(ast->declarations, &ast->declaration_capacity,
                           index, SIZEOF(CDeclaration));
    ast->declarations[index] = (CDeclaration){
        .kind = kind, .first_declarator = -1, .record = -1,
        .next = -1, .body = {c->pos, c->pos},
        .inferred_type_token = -1,
    };
    ast->declaration_count += 1;
    return index;
}

static int32
decl_make_record(DeclCursor *c, enum CRecordKind kind, int32 first, int32 tag) {
    CAst *ast = c->ast;
    int32 index = ast->record_count;
    ast->records = c_ast_storage_grow(ast->records, &ast->record_capacity,
                                      index, SIZEOF(CRecord));
    ast->records[index] = (CRecord){
        .tokens = {first, first}, .body = {first, first}, .kind = kind,
        .tag_token = tag, .first_member = -1, .first_enumerator = -1,
    };
    ast->record_count += 1;
    return index;
}

static int32
decl_match(DeclCursor *c, int32 open) {
    Tokenization *tokens = c->ast->tokenization;
    int32 close = token_range_matching_delimiter_forward(tokens, c->range,
                                                         open);
    if (close < 0) {
        decl_fail(c, C_DECL_PARSE_MALFORMED, open);
    }
    return close;
}

/* Skip an expression without pretending to understand it. The next phase
 * will parse the saved range. Only top-level separators terminate it. */
static int32
decl_skip_to(DeclCursor *c, int32 start, char *a, char *b) {
    int32 i = decl_sig(c, start);
    while (i < c->range.end) {
        if (decl_at(c, i, a) || ((b != NULL) && decl_at(c, i, b))) {
            break;
        }
        if (token_is_open_delimiter(&c->ast->tokenization->tokens[i])) {
            int32 close = decl_match(c, i);
            if (close < 0) {
                return c->range.end;
            }
            i = decl_sig(c, close + 1);
        } else {
            i = decl_sig(c, i + 1);
        }
    }
    return i;
}

static bool
decl_is_extension(DeclCursor *c, int32 i) {
    return decl_at(c, i, "__attribute__")
           || decl_at(c, i, "__declspec")
           || decl_at(c, i, "typeof")
           || decl_at(c, i, "__typeof__");
}

/* Macro annotations are common in the supported codebases. They must
 * not be confused with a typedef or with a declarator's actual name. */
static bool
decl_upper_macro(DeclCursor *c, int32 i) {
    Token *token;
    bool has_letter = false;
    if (i >= c->range.end) {
        return false;
    }
    token = &c->ast->tokenization->tokens[i];
    if (token->kind != TOKEN_IDENT || c_ast_lookup(c->ast, i)
                                      != C_NAME_UNKNOWN) {
        return false;
    }
    for (int32 j = 0; j < token->len; j += 1) {
        char ch = token->text[j];
        if (ch >= 'A' && ch <= 'Z') {
            has_letter = true;
        } else if ((ch < '0' || ch > '9') && ch != '_') {
            return false;
        }
    }
    return has_letter;
}

/* Ignore a macro attribute while retaining every original source token.
 * Ordinary declarators, expressions and macro calls are never expanded.
 * Attribute arguments are opaque balanced ranges. */
static int32
decl_annotation_end(DeclCursor *c, int32 i) {
    int32 next;
    int32 close;
    if (i >= c->range.end) {
        return -1;
    }
    if (decl_at(c, i, "__attribute__")
        || decl_at(c, i, "__declspec")) {
        next = decl_sig(c, i + 1);
        if (!decl_at(c, next, "(")) {
            return -1;
        }
        close = decl_match(c, next);
        return close >= 0 ? decl_sig(c, close + 1) : -1;
    }
    if (!decl_upper_macro(c, i) && !decl_at(c, i, "noreturn")) {
        return -1;
    }
    next = decl_sig(c, i + 1);
    if (decl_at(c, next, "(")) {
        close = decl_match(c, next);
        return close >= 0 ? decl_sig(c, close + 1) : -1;
    }
    return next;
}

static bool
decl_is_storage(enum CKeyword k) {
    return (k == C_KEYWORD_TYPEDEF) || (k == C_KEYWORD_STATIC)
           || (k == C_KEYWORD_EXTERN) || (k == C_KEYWORD_REGISTER)
           || (k == C_KEYWORD_AUTO) || (k == C_KEYWORD_INLINE)
           || (k == C_KEYWORD_THREAD_LOCAL) || (k == C_KEYWORD_NORETURN);
}

static bool
decl_is_builtin(enum CKeyword k) {
    return (k == C_KEYWORD_VOID) || (k == C_KEYWORD_CHAR)
           || (k == C_KEYWORD_SHORT) || (k == C_KEYWORD_INT)
           || (k == C_KEYWORD_LONG) || (k == C_KEYWORD_SIGNED)
           || (k == C_KEYWORD_UNSIGNED) || (k == C_KEYWORD_FLOAT)
           || (k == C_KEYWORD_DOUBLE) || (k == C_KEYWORD_BOOL)
           || (k == C_KEYWORD_COMPLEX) || (k == C_KEYWORD_IMAGINARY);
}

static bool
decl_is_qualifier(enum CKeyword k) {
    return (k == C_KEYWORD_CONST) || (k == C_KEYWORD_VOLATILE)
           || (k == C_KEYWORD_RESTRICT);
}

static int32 decl_parse_one(DeclCursor *, enum CDeclarationKind, bool);

static void
decl_parse_enumerators(DeclCursor *c, int32 first, int32 end, int32 record) {
    DeclCursor inner = *c;
    int32 tail = -1;
    inner.range = (TokenRange){first, end};
    inner.pos = first;
    while (decl_next(&inner) < end) {
        int32 name = decl_next(&inner);
        int32 i;
        int32 e;
        CAst *ast = c->ast;
        if (ast->tokenization->tokens[name].kind != TOKEN_IDENT) {
            decl_fail(c, C_DECL_PARSE_MALFORMED, name);
            return;
        }
        i = decl_sig(&inner, name + 1);
        e = ast->enumerator_count;
        ast->enumerators =
            c_ast_storage_grow(ast->enumerators, &ast->enumerator_capacity,
                               e, SIZEOF(CEnumerator));
        ast->enumerators[e] = (CEnumerator){
            .tokens = {name, i}, .value = {i, i},
            .name_token = name, .next = -1,
        };
        ast->enumerator_count += 1;
        if (tail < 0) {
            ast->records[record].first_enumerator = e;
        } else {
            ast->enumerators[tail].next = e;
        }
        tail = e;
        ast->records[record].enumerator_count += 1;
        if (decl_at(&inner, i, "=")) {
            int32 start = decl_sig(&inner, i + 1);
            i = decl_skip_to(&inner, start, ",", NULL);
            if (i == start) {
                decl_fail(c, C_DECL_PARSE_MALFORMED, start);
                return;
            }
            ast->enumerators[e].value = (TokenRange){start, i};
        }
        ast->enumerators[e].tokens.end = i;
        c_ast_bind(ast, name, C_NAME_ENUMERATOR);
        if (inner.status != C_DECL_PARSE_OK) {
            decl_fail(c, inner.status, inner.issue);
            return;
        }
        if (i == end) {
            break;
        }
        if (!decl_at(&inner, i, ",")) {
            decl_fail(c, C_DECL_PARSE_MALFORMED, i);
            return;
        }
        inner.pos = decl_sig(&inner, i + 1);
        if (inner.status != C_DECL_PARSE_OK) {
            decl_fail(c, inner.status, inner.issue);
            return;
        }
    }
    return;
}

static void
decl_parse_members(DeclCursor *c, int32 first, int32 end, int32 record) {
    DeclCursor inner = *c;
    int32 tail = -1;
    inner.range = (TokenRange){first, end};
    inner.pos = first;
    inner.depth += 1;
    while ((inner.status == C_DECL_PARSE_OK)
           && (decl_next(&inner) < end)) {
        int32 d = decl_parse_one(&inner, C_DECLARATION_MEMBER, true);
        if (d < 0) {
            break;
        }
        if (tail < 0) {
            c->ast->records[record].first_member = d;
        } else {
            c->ast->declarations[tail].next = d;
        }
        tail = d;
        c->ast->records[record].member_count += 1;
    }
    if (inner.status != C_DECL_PARSE_OK) {
        decl_fail(c, inner.status, inner.issue);
    }
    return;
}

static int32
decl_parse_record(DeclCursor *c) {
    int32 start = decl_next(c);
    enum CKeyword k = c_token_keyword(&c->ast->tokenization->tokens[start]);
    enum CRecordKind kind = C_RECORD_STRUCT;
    enum CNameClass tag_kind = C_NAME_TAG_STRUCT;
    int32 i = decl_sig(c, start + 1);
    int32 tag = -1;
    int32 record;
    if (k == C_KEYWORD_UNION) {
        kind = C_RECORD_UNION;
        tag_kind = C_NAME_TAG_UNION;
    } else if (k == C_KEYWORD_ENUM) {
        kind = C_RECORD_ENUM;
        tag_kind = C_NAME_TAG_ENUM;
    }
    if ((i < c->range.end)
        && (c->ast->tokenization->tokens[i].kind == TOKEN_IDENT)) {
        tag = i;
        i = decl_sig(c, i + 1);
    }
    if ((tag < 0) && !decl_at(c, i, "{")) {
        decl_fail(c, C_DECL_PARSE_MALFORMED, i);
        return -1;
    }
    record = decl_make_record(c, kind, start, tag);
    if (tag >= 0) {
        c_ast_bind(c->ast, tag, tag_kind);
    }
    if (decl_at(c, i, "{")) {
        int32 close = decl_match(c, i);
        if (close < 0) {
            return record;
        }
        c->ast->records[record].body = (TokenRange){i, close + 1};
        if (kind == C_RECORD_ENUM) {
            decl_parse_enumerators(c, i + 1, close, record);
        } else {
            decl_parse_members(c, i + 1, close, record);
        }
        i = decl_sig(c, close + 1);
    }
    c->ast->records[record].tokens = (TokenRange){start, i};
    c->pos = i;
    return record;
}

static bool
decl_parse_specifiers(DeclCursor *c, int32 d) {
    bool type_seen = false;
    bool prefix_seen = false;
    int32 first = decl_next(c);
    int32 i = first;
    CAst *ast = c->ast;
    while (i < c->range.end) {
        Token *token = &ast->tokenization->tokens[i];
        enum CKeyword k = c_token_keyword(token);
        if (k == C_KEYWORD_TYPEDEF) {
            ast->declarations[d].is_typedef = true;
        } else if (decl_is_storage(k)) {
            prefix_seen = true;
        } else if (decl_is_builtin(k)) {
            type_seen = true;
        } else if (decl_is_qualifier(k)) {
            prefix_seen = true;
        } else if (k == C_KEYWORD_ATOMIC) {
            int32 next = decl_sig(c, i + 1);
            if (decl_at(c, next, "(")) {
                int32 close = decl_match(c, next);
                if (close < 0) {
                    return false;
                }
                type_seen = true;
                i = close;
            }
        } else if (k == C_KEYWORD_ALIGNAS) {
            int32 next = decl_sig(c, i + 1);
            if (!decl_at(c, next, "(")) {
                decl_fail(c, C_DECL_PARSE_MALFORMED, next);
                return false;
            }
            i = decl_match(c, next);
            if (i < 0) {
                return false;
            }
        } else if ((k == C_KEYWORD_STRUCT) || (k == C_KEYWORD_UNION)
                   || (k == C_KEYWORD_ENUM)) {
            if (type_seen) {
                break;
            }
            c->pos = i;
            ast->declarations[d].record = decl_parse_record(c);
            if (c->status != C_DECL_PARSE_OK) {
                return false;
            }
            type_seen = true;
            i = c->pos;
            continue;
        } else if ((token->kind == TOKEN_IDENT) && !type_seen
                   && (c_ast_lookup(ast, i) == C_NAME_TYPEDEF
                       || c_ast_lookup(ast, i) == C_NAME_INFERRED_TYPEDEF)) {
            type_seen = true;
        } else if (decl_at(c, i, "noreturn")
                   || decl_at(c, i, "__attribute__")
                   || decl_at(c, i, "__declspec")) {
            int32 end = decl_annotation_end(c, i);
            if (end < 0) {
                decl_fail(c, C_DECL_PARSE_MALFORMED, i);
                return false;
            }
            i = end;
            continue;
        } else if (decl_upper_macro(c, i)
                   && decl_sig(c, i + 1) < c->range.end) {
            int32 lookahead = decl_sig(c, i + 1);
            Token *next = &ast->tokenization->tokens[lookahead];
            enum CKeyword next_keyword = c_token_keyword(next);
            bool known_type = c_ast_lookup(ast, lookahead) == C_NAME_TYPEDEF
                              || c_ast_lookup(ast, lookahead)
                                 == C_NAME_INFERRED_TYPEDEF;
            bool type_prefix = decl_is_builtin(next_keyword)
                               || decl_is_storage(next_keyword)
                               || decl_is_qualifier(next_keyword)
                               || next_keyword == C_KEYWORD_STRUCT
                               || next_keyword == C_KEYWORD_UNION
                               || next_keyword == C_KEYWORD_ENUM
                               || known_type || TOKEN_IS(next, "bool")
                               || TOKEN_IS(next, "noreturn");
            bool another_name = next->kind == TOKEN_IDENT
                                && !decl_at(c, lookahead, "(");
            /* An uppercase macro may itself expand to a type. In
             * particular, HASH_VALUE_TYPE value is a declaration, not
             * an annotation followed by an unknown type `value`. */
            if (!decl_at(c, lookahead, "(")
                && (type_seen ? another_name : type_prefix)) {
                i = lookahead;
                continue;
            }
            if (ast->infer_type_names && !type_seen
                && (prefix_seen || c->depth > 0
                    || ast->infer_external)) {
                type_seen = true;
                c->inferred_type = i;
            } else {
                break;
            }
        } else if (decl_is_extension(c, i)) {
            decl_fail(c, C_DECL_PARSE_UNSUPPORTED, i);
            return false;
        } else if (ast->infer_type_names && !type_seen
                   && token->kind == TOKEN_IDENT
                   && (c_ast_lookup(ast, i) == C_NAME_UNKNOWN)
                   && (prefix_seen || c->depth > 0
                       || ast->infer_external)) {
            type_seen = true;
            c->inferred_type = i;
        } else {
            break;
        }
        i = decl_sig(c, i + 1);
    }
    if (!type_seen) {
        decl_fail(c, C_DECL_PARSE_NOT_DECLARATION, first);
        return false;
    }
    ast->declarations[d].specifiers = (TokenRange){first, i};
    c->pos = i;
    return true;
}

static int32 decl_parse_declarator(DeclCursor *, bool);

static void
decl_parse_parameters(DeclCursor *c, int32 node, int32 start, int32 end) {
    DeclCursor inner = *c;
    int32 tail = -1;
    inner.range = (TokenRange){start, end};
    inner.pos = start;
    inner.depth += 1;
    if (decl_sig(&inner, start) >= end) {
        return;
    }
    if (decl_at(&inner, decl_sig(&inner, start), "void")
        && (decl_sig(&inner, decl_sig(&inner, start) + 1) == end)) {
        return;
    }
    c_ast_scope_push(c->ast);
    while ((inner.status == C_DECL_PARSE_OK) && (inner.pos < end)) {
        int32 i = decl_skip_to(&inner, inner.pos, ",", NULL);
        DeclCursor argument = inner;
        int32 parameter;
        argument.range.end = i;
        argument.depth += 1;
        if (decl_at(&inner, decl_sig(&inner, inner.pos), "...")) {
            int32 dots = decl_sig(&inner, inner.pos);
            if (decl_sig(&inner, dots + 1) != i) {
                decl_fail(&inner, C_DECL_PARSE_MALFORMED, i);
            }
            c->ast->declarators[node].variadic = true;
            if (i < end) {
                decl_fail(&inner, C_DECL_PARSE_MALFORMED, i);
            }
            inner.pos = i;
            break;
        }
        parameter = decl_parse_one(&argument, C_DECLARATION_PARAMETER, false);
        if (argument.status != C_DECL_PARSE_OK) {
            decl_fail(&inner, argument.status, argument.issue);
            break;
        }
        if (tail < 0) {
            c->ast->declarators[node].first_parameter = parameter;
        } else {
            c->ast->declarations[tail].next = parameter;
        }
        tail = parameter;
        c->ast->declarators[node].parameter_count += 1;
        inner.pos = i;
        if (i >= end) {
            break;
        }
        inner.pos = decl_sig(&inner, i + 1);
        if (inner.pos == end) {
            decl_fail(&inner, C_DECL_PARSE_MALFORMED, i);
        }
    }
    c_ast_scope_pop(c->ast);
    if (inner.status != C_DECL_PARSE_OK) {
        decl_fail(c, inner.status, inner.issue);
    }
    return;
}

static int32
decl_parse_declarator(DeclCursor *c, bool abstract_allowed) {
    int32 pointer[64];
    int32 pointer_count = 0;
    int32 first = decl_next(c);
    int32 i = first;
    int32 root = -1;
    CAst *ast = c->ast;
    if (c->depth > 64) {
        decl_fail(c, C_DECL_PARSE_UNSUPPORTED, i);
        return -1;
    }
    while (decl_at(c, i, "*")) {
        int32 start = i;
        i = decl_sig(c, i + 1);
        while (i < c->range.end) {
            enum CKeyword k = c_token_keyword(&ast->tokenization->tokens[i]);
            if (!decl_is_qualifier(k) && (k != C_KEYWORD_ATOMIC)) {
                break;
            }
            i = decl_sig(c, i + 1);
        }
        if (pointer_count >= 64) {
            decl_fail(c, C_DECL_PARSE_UNSUPPORTED, i);
            return -1;
        }
        pointer[pointer_count] = decl_make_node(c, C_DECL_POINTER, start, i,
                                                -1, -1);
        pointer_count += 1;
    }
    if ((i < c->range.end)
        && (ast->tokenization->tokens[i].kind == TOKEN_IDENT)
        && (c_token_keyword(&ast->tokenization->tokens[i]) == 0)) {
        root = decl_make_node(c, C_DECL_NAME, i, i + 1, -1, i);
        i = decl_sig(c, i + 1);
    } else if (decl_at(c, i, "(")) {
        int32 next = decl_sig(c, i + 1);
        bool grouped = (decl_at(c, next, "*") || decl_at(c, next, "("));
        if ((next < c->range.end)
            && (ast->tokenization->tokens[next].kind == TOKEN_IDENT)
            && (c_ast_lookup(ast, next) != C_NAME_TYPEDEF)
            && (c_ast_lookup(ast, next) != C_NAME_INFERRED_TYPEDEF)) {
            grouped = true;
        }
        if (grouped) {
            int32 open = i;
            int32 nested;
            c->pos = next;
            c->depth += 1;
            nested = decl_parse_declarator(c, abstract_allowed);
            c->depth -= 1;
            i = decl_next(c);
            if ((nested < 0) || !decl_at(c, i, ")")) {
                decl_fail(c, C_DECL_PARSE_MALFORMED, i);
                return -1;
            }
            root = decl_make_node(c, C_DECL_PAREN, open, i + 1, nested, -1);
            i = decl_sig(c, i + 1);
        }
    }
    if (root < 0) {
        if (!abstract_allowed) {
            decl_fail(c, C_DECL_PARSE_MALFORMED, i);
            return -1;
        }
        root = decl_make_node(c, C_DECL_ABSTRACT, i, i, -1, -1);
    }
    while (decl_at(c, i, "[") || decl_at(c, i, "(")) {
        int32 close = decl_match(c, i);
        int32 outer;
        if (close < 0) {
            return -1;
        }
        if (decl_at(c, i, "[")) {
            outer = decl_make_node(c, C_DECL_ARRAY,
                                   ast->declarators[root].tokens.first,
                                   close + 1, root, -1);
            ast->declarators[outer].extra = (TokenRange){i + 1, close};
        } else {
            outer = decl_make_node(c, C_DECL_FUNCTION,
                                   ast->declarators[root].tokens.first,
                                   close + 1, root, -1);
            decl_parse_parameters(c, outer, i + 1, close);
            if (c->status != C_DECL_PARSE_OK) {
                return -1;
            }
        }
        root = outer;
        i = decl_sig(c, close + 1);
    }
    for (int32 j = pointer_count - 1; j >= 0; j -= 1) {
        int32 node = pointer[j];
        ast->declarators[node].inner = root;
        ast->declarators[node].tokens.end = i;
        root = node;
    }
    c->pos = i;
    return root;
}

static int32
decl_name(CAst *ast, int32 root) {
    while (root >= 0) {
        CDeclaratorNode *node = &ast->declarators[root];
        if (node->kind == C_DECL_NAME) {
            return node->name_token;
        }
        root = node->inner;
    }
    return -1;
}

/* The operation nearest the identifier determines whether the declarator
 * denotes a function or a pointer to a function. Ignore parentheses. */
static bool
decl_is_function(CAst *ast, int32 root) {
    enum CDeclaratorKind nearest = 0;
    while (root >= 0) {
        CDeclaratorNode *node = &ast->declarators[root];
        if ((node->kind == C_DECL_FUNCTION)
            || (node->kind == C_DECL_POINTER)
            || (node->kind == C_DECL_ARRAY)) {
            nearest = node->kind;
        }
        root = node->inner;
    }
    return nearest == C_DECL_FUNCTION;
}

static int32
decl_parse_one(DeclCursor *c, enum CDeclarationKind kind, bool require_semi) {
    int32 first = decl_next(c);
    int32 d;
    int32 tail = -1;
    bool allow_abstract = (kind != C_DECLARATION_NORMAL);
    if (c->depth > 64) {
        decl_fail(c, C_DECL_PARSE_UNSUPPORTED, first);
        return -1;
    }
    if (decl_at(c, first, "#")) {
        decl_fail(c, C_DECL_PARSE_UNSUPPORTED, first);
        return -1;
    }
    c->inferred_type = -1;
    d = decl_make_declaration(c, kind);
    if (!decl_parse_specifiers(c, d)) {
        return -1;
    }
    while (c->status == C_DECL_PARSE_OK) {
        CAst *ast = c->ast;
        int32 i = decl_next(c);
        int32 root;
        if ((i >= c->range.end) || decl_at(c, i, ";")
            || ((kind == C_DECLARATION_MEMBER) && decl_at(c, i, ":"))) {
            if (decl_at(c, i, ":")) {
                /* Anonymous bit field. */
                allow_abstract = true;
            } else {
                break;
            }
        }
        root = decl_parse_declarator(c, allow_abstract);
        if (root < 0) {
            return -1;
        }
        if (tail < 0) {
            ast->declarations[d].first_declarator = root;
        } else {
            ast->declarators[tail].next = root;
        }
        ast->declarations[d].declarator_count += 1;
        tail = root;
        i = decl_next(c);
        if (decl_at(c, i, "=") || ((kind == C_DECLARATION_MEMBER)
                                     && decl_at(c, i, ":"))) {
            int32 start = decl_sig(c, i + 1);
            char *separator = ",";
            i = decl_skip_to(c, start, separator, ";");
            if ((start == i) || (c->status != C_DECL_PARSE_OK)) {
                decl_fail(c, C_DECL_PARSE_MALFORMED, start);
                return -1;
            }
            ast->declarators[root].extra = (TokenRange){start, i};
            c->pos = i;
        }
        i = decl_next(c);
        /* Function annotations may follow a declarator, e.g.
         * `void log(...) ATTR_PRINTF(1, 2);`. Require the annotation to
         * lead to a declaration separator or body, never consume calls
         * within an initializer or ordinary expression. */
        while ((decl_upper_macro(c, i) || decl_at(c, i, "noreturn")
                || decl_at(c, i, "__attribute__")
                || decl_at(c, i, "__declspec"))) {
            int32 end = decl_annotation_end(c, i);
            if (end < 0) {
                return -1;
            }
            if (!decl_at(c, end, ";") && !decl_at(c, end, "{")
                && !decl_at(c, end, ",")
                && !decl_upper_macro(c, end)
                && !decl_at(c, end, "__attribute__")
                && !decl_at(c, end, "__declspec")) {
                break;
            }
            c->pos = end;
            i = end;
        }
        if (decl_is_extension(c, i)) {
            decl_fail(c, C_DECL_PARSE_UNSUPPORTED, i);
            return -1;
        }
        if (!decl_at(c, i, ",")) {
            break;
        }
        c->pos = decl_sig(c, i + 1);
        if ((c->pos >= c->range.end) || decl_at(c, c->pos, ";")) {
            decl_fail(c, C_DECL_PARSE_MALFORMED, c->pos);
        }
    }
    if (c->status != C_DECL_PARSE_OK) {
        return -1;
    }
    {
        int32 i = decl_next(c);
        CAst *ast = c->ast;
        if (require_semi && decl_at(c, i, "{")
            && (kind == C_DECLARATION_NORMAL)
            && (ast->declarations[d].declarator_count == 1)
            && decl_is_function(ast, ast->declarations[d].first_declarator)) {
            int32 close = decl_match(c, i);
            if (close < 0) {
                return -1;
            }
            ast->declarations[d].is_function_definition = true;
            ast->declarations[d].body = (TokenRange){i, close + 1};
            i = close + 1;
        } else if (require_semi && decl_at(c, i, ";")) {
            i += 1;
        } else if (require_semi || (i < c->range.end)) {
            decl_fail(c, C_DECL_PARSE_MALFORMED, i);
            return -1;
        }
        ast->declarations[d].tokens = (TokenRange){first, i};
        ast->declarations[d].inferred_type_token = c->inferred_type;
        c->pos = decl_sig(c, i);
        if (c->inferred_type >= 0
            && c_ast_lookup(ast, c->inferred_type) == C_NAME_UNKNOWN) {
            c_ast_bind(ast, c->inferred_type, C_NAME_INFERRED_TYPEDEF);
        }
        for (int32 root = ast->declarations[d].first_declarator;
             root >= 0; root = ast->declarators[root].next) {
            int32 name = decl_name(ast, root);
            if ((name >= 0) && (kind != C_DECLARATION_MEMBER)) {
                enum CNameClass cls = C_NAME_ORDINARY;
                if (ast->declarations[d].is_typedef) {
                    cls = C_NAME_TYPEDEF;
                }
                c_ast_bind(ast, name, cls);
            }
        }
    }
    return d;
}

CDeclParseResult
c_ast_parse_declaration(CAst *ast, TokenRange range) {
    CDeclParseResult result = {C_DECL_PARSE_MALFORMED, -1, -1, -1};
    DeclCursor c;
    int32 declaration_count;
    int32 declarator_count;
    int32 record_count;
    int32 enumerator_count;
    int32 binding_count;
    int32 scope_count;
    int32 scope;
    if ((ast == NULL) || (ast->tokenization == NULL)
        || !token_range_is_valid(ast->tokenization, range)) {
        return result;
    }
    if (token_range_first_significant(ast->tokenization, range)
        >= range.end) {
        result.status = C_DECL_PARSE_NOT_DECLARATION;
        return result;
    }
    c = (DeclCursor){.ast = ast, .range = range, .pos = range.first,
                     .issue = -1, .inferred_type = -1,
                     .status = C_DECL_PARSE_OK};
    declaration_count = ast->declaration_count;
    declarator_count = ast->declarator_count;
    record_count = ast->record_count;
    enumerator_count = ast->enumerator_count;
    binding_count = ast->binding_count;
    scope_count = ast->scope_count;
    scope = ast->current_scope;
    result.declaration = decl_parse_one(&c, C_DECLARATION_NORMAL, true);
    result.status = c.status;
    result.issue_token = c.issue;
    if ((result.declaration < 0) || (c.status != C_DECL_PARSE_OK)) {
        ast->declaration_count = declaration_count;
        ast->declarator_count = declarator_count;
        ast->record_count = record_count;
        ast->enumerator_count = enumerator_count;
        ast->binding_count = binding_count;
        ast->scope_count = scope_count;
        ast->current_scope = scope;
        result.declaration = -1;
    } else {
        result.next_token = c.pos;
    }
    return result;
}

/* Type-name probing must not introduce declarations or typedef bindings.
 * All potentially created nodes and names are discarded, even on success.
 */
bool
c_ast_is_type_name(CAst *ast, TokenRange range) {
    DeclCursor c;
    int32 declaration_count;
    int32 declarator_count;
    int32 record_count;
    int32 enumerator_count;
    int32 binding_count;
    int32 scope_count;
    int32 current_scope;
    int32 d;
    int32 i;
    bool infer_types;
    bool infer_external;
    bool valid = false;

    if ((ast == NULL) || (ast->tokenization == NULL)
        || !token_range_is_valid(ast->tokenization, range)) {
        return false;
    }
    c = (DeclCursor){.ast = ast, .range = range, .pos = range.first,
                     .issue = -1, .inferred_type = -1,
                     .status = C_DECL_PARSE_OK};
    declaration_count = ast->declaration_count;
    declarator_count = ast->declarator_count;
    record_count = ast->record_count;
    enumerator_count = ast->enumerator_count;
    binding_count = ast->binding_count;
    scope_count = ast->scope_count;
    current_scope = ast->current_scope;
    infer_types = ast->infer_type_names;
    infer_external = ast->infer_external;
    ast->infer_type_names = false;
    ast->infer_external = false;
    d = decl_make_declaration(&c, C_DECLARATION_PARAMETER);
    if (!decl_parse_specifiers(&c, d)) {
        goto cleanup;
    }
    for (i = decl_sig(&c, range.first);
         i < ast->declarations[d].specifiers.end;
         i = decl_sig(&c, i + 1)) {
        enum CKeyword k = c_token_keyword(&ast->tokenization->tokens[i]);
        if (decl_is_storage(k) || (k == C_KEYWORD_ALIGNAS)) {
            goto cleanup;
        }
    }
    i = decl_next(&c);
    if (i < range.end) {
        int32 root = decl_parse_declarator(&c, true);
        if ((root < 0) || (decl_name(ast, root) >= 0)) {
            goto cleanup;
        }
    }
    valid = (c.status == C_DECL_PARSE_OK) && (decl_next(&c) == range.end);
cleanup:
    ast->declaration_count = declaration_count;
    ast->declarator_count = declarator_count;
    ast->record_count = record_count;
    ast->enumerator_count = enumerator_count;
    ast->binding_count = binding_count;
    ast->scope_count = scope_count;
    ast->current_scope = current_scope;
    ast->infer_type_names = infer_types;
    ast->infer_external = infer_external;
    return valid;
}

#if TESTING_meta_decl
#define CBASE_IMPLEMENT
#include "cbase.h"
#define CMETA_IMPLEMENT
#include "meta.h"

static int32
ast_test_find(Tokenization *tokens, char *name, int32 occurrence) {
    TokenRange all = {0, tokens->token_count};
    int32 i = -1;
    for (int32 j = 0; j <= occurrence; j += 1) {
        i = token_range_next_text(tokens, all, i, name, strlen32(name));
    }
    ASSERT_LT_VAR(i, tokens->token_count);
    return i;
}

static CDeclParseResult
ast_test_parse(CAst *ast, int32 start) {
    TokenRange range = {start, ast->tokenization->token_count};
    return c_ast_parse_declaration(ast, range);
}

static void
ast_test_declarators(void) {
    char *source = "typedef unsigned long Count; "
                   "Count *ptr, table[2 + 3]; "
                   "int32 (*callback)(Count n, ...); "
                   "int32 *factory(void) { return 0; }";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult result;
    int32 d;
    int32 root;
    int32 next;
    int32 name;
    ASSERT(c_ast_init(&ast, &tokens));
    name = ast_test_find(&tokens, "int32", 0);
    ASSERT(c_ast_bind(&ast, name, C_NAME_TYPEDEF));
    result = ast_test_parse(&ast, 0);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Count", 1)),
              C_NAME_TYPEDEF);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    d = result.declaration;
    ASSERT_EQ(ast.declarations[d].declarator_count, 2);
    root = ast.declarations[d].first_declarator;
    ASSERT_EQ(ast.declarators[root].kind, C_DECL_POINTER);
    ASSERT_EQ(ast.declarators[root].tokens.first,
              ast_test_find(&tokens, "*", 0));
    next = ast.declarators[root].next;
    ASSERT_EQ(ast.declarators[next].kind, C_DECL_ARRAY);
    ASSERT_EQ(ast.declarators[next].extra.first,
              ast_test_find(&tokens, "2", 0));
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    d = result.declaration;
    root = ast.declarations[d].first_declarator;
    ASSERT_EQ(ast.declarators[root].kind, C_DECL_FUNCTION);
    ASSERT_EQ(ast.declarators[root].parameter_count, 1);
    ASSERT(ast.declarators[root].variadic);
    ASSERT_EQ(ast.declarators[ast.declarators[root].inner].kind, C_DECL_PAREN);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    ASSERT(ast.declarations[result.declaration].is_function_definition);
    root = ast.declarations[result.declaration].first_declarator;
    ASSERT_EQ(ast.declarators[root].kind, C_DECL_POINTER);
    ASSERT_EQ(ast.declarators[ast.declarators[root].inner].kind,
              C_DECL_FUNCTION);
    ASSERT_EQ(ast.declarations[result.declaration].body.first,
              ast_test_find(&tokens, "{", 0));
    ASSERT_EQ(ast.declarators[ast.declarators[root].inner].parameter_count, 0);
    ASSERT_EQ(ast.tokenization->tokens[name].text, tokens.tokens[name].text);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
ast_test_records(void) {
    char *source = "typedef struct Node { "
                   "int value; unsigned bits : 3; "
                   "union { char ch; int count; }; "
                   "} Node; "
                   "enum Color { RED, GREEN = f(1, 2), BLUE, }; "
                   "struct Node *next;";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult result;
    CRecord *record;
    int32 enum_record;
    ASSERT(c_ast_init(&ast, &tokens));
    result = ast_test_parse(&ast, 0);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    record = &ast.records[ast.declarations[result.declaration].record];
    ASSERT_EQ(record->kind, C_RECORD_STRUCT);
    ASSERT_EQ(record->member_count, 3);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Node", 2)),
              C_NAME_TYPEDEF);
    ASSERT_EQ(c_ast_lookup_tag(&ast, ast_test_find(&tokens, "Node", 0)),
              C_NAME_TAG_STRUCT);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "value", 0)),
              C_NAME_UNKNOWN);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    enum_record = ast.declarations[result.declaration].record;
    ASSERT_EQ(ast.records[enum_record].enumerator_count, 3);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "GREEN", 0)),
              C_NAME_ENUMERATOR);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
ast_test_scopes_and_rollback(void) {
    char *source = "typedef int Type; Type *ptr; invalid *guess; "
                   "int Type; Type *bad; "
                   "typedef struct { int good; unknown broken; } Fail; "
                   "int later;";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult result;
    int32 start;
    int32 saved;
    ASSERT(c_ast_init(&ast, &tokens));
    result = ast_test_parse(&ast, 0);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Type", 1)),
              C_NAME_TYPEDEF);
    start = result.next_token;
    saved = ast.declaration_count;
    result = ast_test_parse(&ast, start);
    ASSERT_EQ(result.status, C_DECL_PARSE_NOT_DECLARATION);
    ASSERT_EQ(ast.declaration_count, saved);
    result = ast_test_parse(&ast, ast_test_find(&tokens, "int", 1));
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Type", 3)),
              C_NAME_ORDINARY);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_NOT_DECLARATION);
    saved = ast.binding_count;
    result = ast_test_parse(&ast, ast_test_find(&tokens, "typedef", 1));
    ASSERT_EQ(result.status, C_DECL_PARSE_NOT_DECLARATION);
    ASSERT_EQ(ast.binding_count, saved);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "good", 0)),
              C_NAME_UNKNOWN);
    ASSERT_EQ(c_ast_lookup_tag(&ast, ast_test_find(&tokens, "Fail", 0)),
              C_NAME_UNKNOWN);
    ASSERT(c_ast_scope_push(&ast) > 0);
    ASSERT(c_ast_bind(&ast, ast_test_find(&tokens, "Type", 2),
                      C_NAME_TYPEDEF));
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Type", 3)),
              C_NAME_TYPEDEF);
    ASSERT(c_ast_scope_pop(&ast));
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "Type", 3)),
              C_NAME_ORDINARY);
    ASSERT(!c_ast_scope_pop(&ast));
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
ast_test_other_constructs(void) {
    char *source = "typedef int Kind; "
                   "struct S { Kind field; enum Nested { VALUE } item; }; "
                   "int data[2][3] = {{1, 2, 3}, {4, 5, 6}}; "
                   "int (*handlers[3])(Kind); "
                   "int bad __attribute__((unused)); "
                   "z *w;";
    Tokenization tokens = tokenize_with_flags(source, strlen32(source),
                                              TOKENIZE_SKIP_WHITESPACE);
    CAst ast;
    CDeclParseResult result;
    int32 root;
    int32 members;
    ASSERT(c_ast_init(&ast, &tokens));
    result = ast_test_parse(&ast, 0);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    members = ast.records[ast.declarations[result.declaration].record]
              .member_count;
    ASSERT_EQ(members, 2);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "field", 0)),
              C_NAME_UNKNOWN);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "VALUE", 0)),
              C_NAME_ENUMERATOR);
    ASSERT_EQ(c_ast_lookup_tag(&ast, ast_test_find(&tokens, "Nested", 0)),
              C_NAME_TAG_ENUM);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    root = ast.declarations[result.declaration].first_declarator;
    ASSERT_EQ(ast.declarators[root].kind, C_DECL_ARRAY);
    ASSERT_EQ(ast.declarators[ast.declarators[root].inner].kind, C_DECL_ARRAY);
    ASSERT_LT_VAR(ast.declarators[root].extra.first,
                  ast.declarators[root].extra.end);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    root = ast.declarations[result.declaration].first_declarator;
    ASSERT_EQ(ast.declarators[root].kind, C_DECL_FUNCTION);
    ASSERT_EQ(ast.declarators[root].parameter_count, 1);
    result = ast_test_parse(&ast, result.next_token);
    ASSERT_EQ(result.status, C_DECL_PARSE_OK);
    ASSERT_EQ(c_ast_lookup(&ast, ast_test_find(&tokens, "bad", 0)),
              C_NAME_ORDINARY);
    result = ast_test_parse(&ast, ast_test_find(&tokens, "z", 0));
    ASSERT_EQ(result.status, C_DECL_PARSE_NOT_DECLARATION);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
ast_test_malformed(void) {
    char *source = "int missing( ; int broken[3; "
                   "int x = ; unsigned : 4;";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult result;
    ASSERT(c_ast_init(&ast, &tokens));
    result = ast_test_parse(&ast, 0);
    ASSERT_EQ(result.status, C_DECL_PARSE_MALFORMED);
    ASSERT_EQ(ast.declaration_count, 0);
    result = ast_test_parse(&ast, ast_test_find(&tokens, "int", 1));
    ASSERT_EQ(result.status, C_DECL_PARSE_MALFORMED);
    result = ast_test_parse(&ast, ast_test_find(&tokens, "int", 2));
    ASSERT_EQ(result.status, C_DECL_PARSE_MALFORMED);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

static void
ast_test_inferred_types(void) {
    char *source = "static MissingType *value; "
                   "MissingResult f(MissingArg *argument) { return 0; } "
                   "int ordinary; a*b;";
    Tokenization tokens = tokenize(source, strlen32(source));
    CAst ast;
    CDeclParseResult parsed;
    int32 missing = ast_test_find(&tokens, "MissingType", 0);
    int32 unknown = ast_test_find(&tokens, "a", 0);
    ASSERT(c_ast_init(&ast, &tokens));
    ast.infer_type_names = true;
    ast.infer_external = true;
    parsed = ast_test_parse(&ast, 0);
    ASSERT_EQ(parsed.status, C_DECL_PARSE_OK);
    ASSERT_EQ(ast.declarations[parsed.declaration].inferred_type_token,
              missing);
    ASSERT_EQ(c_ast_lookup(&ast, missing), C_NAME_INFERRED_TYPEDEF);
    parsed = ast_test_parse(&ast, parsed.next_token);
    ASSERT_EQ(parsed.status, C_DECL_PARSE_OK);
    ASSERT(ast.declarations[parsed.declaration].is_function_definition);
    ast.infer_external = false;
    parsed = ast_test_parse(&ast, ast_test_find(&tokens, "a", 0));
    ASSERT_EQ(parsed.status, C_DECL_PARSE_NOT_DECLARATION);
    ASSERT_EQ(c_ast_lookup(&ast, unknown), C_NAME_UNKNOWN);
    c_ast_free(&ast);
    free_tokenization(&tokens);
    return;
}

int
main(void) {
    ast_test_inferred_types();
    ast_test_declarators();
    ast_test_records();
    ast_test_scopes_and_rollback();
    ast_test_other_constructs();
    ast_test_malformed();
    return 0;
}
#endif

#endif /* META_DECL_C */
