// SPDX-License-Identifier: AGPL
// Copyright (c) 2026 Lucas Mior

#if !defined(META_AST_H) && !defined(CMETA_DECLARATIONS_COMPLETE)
#include "meta.h"
#endif

#if !defined(META_AST_H)
#define META_AST_H

/* Node and symbol indices are stable until c_ast_free. -1 means absent.
 * Source text, tokens and token strings are ALWAYS borrowed from tokenization.
 * No syntax node owns a copy of source text or of an identifier.
 */
enum CNameClass {
    C_NAME_UNKNOWN = 0,
    C_NAME_TYPEDEF,
    C_NAME_ORDINARY,
    C_NAME_ENUMERATOR,
    C_NAME_TAG_STRUCT,
    C_NAME_TAG_UNION,
    C_NAME_TAG_ENUM,
    C_NAME_INFERRED_TYPEDEF, /* Inferred from syntax, not a known typedef. */
};

enum CDeclaratorKind {
    C_DECL_NAME = 1,
    C_DECL_ABSTRACT,
    C_DECL_POINTER,
    C_DECL_PAREN,
    C_DECL_ARRAY,
    C_DECL_FUNCTION,
};

enum CRecordKind {
    C_RECORD_STRUCT = 1,
    C_RECORD_UNION,
    C_RECORD_ENUM,
};

enum CDeclarationKind {
    C_DECLARATION_NORMAL = 1,
    C_DECLARATION_MEMBER,
    C_DECLARATION_PARAMETER,
};

enum CDeclParseStatus {
    C_DECL_PARSE_OK = 1,
    C_DECL_PARSE_NOT_DECLARATION,
    C_DECL_PARSE_UNSUPPORTED,
    C_DECL_PARSE_MALFORMED,
};

typedef struct CDeclaratorNode {
    TokenRange tokens;
    TokenRange extra; /* Initializer, array bound or bit-field width. */
    enum CDeclaratorKind kind;
    int32 inner;
    int32 next;
    int32 name_token;
    int32 first_parameter;
    int32 parameter_count;
    bool variadic;
} CDeclaratorNode;

typedef struct CDeclaration {
    TokenRange tokens;
    TokenRange specifiers;
    TokenRange body; /* Function body, including its braces, or empty. */
    enum CDeclarationKind kind;
    int32 first_declarator;
    int32 declarator_count;
    int32 record;
    int32 next;
    bool is_typedef;
    bool is_function_definition;
    int32 inferred_type_token; /* -1 unless a type was inferred. */
} CDeclaration;

typedef struct CEnumerator {
    TokenRange tokens;
    TokenRange value; /* Expression range, unparsed until phase 3.2. */
    int32 name_token;
    int32 next;
} CEnumerator;

typedef struct CRecord {
    TokenRange tokens;
    TokenRange body;
    enum CRecordKind kind;
    int32 tag_token;
    int32 first_member;
    int32 member_count;
    int32 first_enumerator;
    int32 enumerator_count;
} CRecord;

typedef struct CScope {
    int32 parent;
} CScope;

typedef struct CBinding {
    int32 scope;
    int32 name_token;
    enum CNameClass name_class;
} CBinding;

/* Expression syntax refers to token indices in the same Tokenization as
 * declarations. Children and argument lists use stable array indices.
 * An absent child, operator or argument has index -1.
 */
enum CExpressionKind {
    C_EXPR_IDENTIFIER = 1,
    C_EXPR_LITERAL,
    C_EXPR_PAREN,
    C_EXPR_UNARY,
    C_EXPR_POSTFIX,
    C_EXPR_BINARY,
    C_EXPR_ASSIGN,
    C_EXPR_CONDITIONAL,
    C_EXPR_COMMA,
    C_EXPR_CALL,
    C_EXPR_SUBSCRIPT,
    C_EXPR_MEMBER,
    C_EXPR_CAST,
    C_EXPR_SIZEOF_EXPR,
    C_EXPR_SIZEOF_TYPE,
    C_EXPR_ALIGNOF_TYPE,
    C_EXPR_COMPOUND_LITERAL,
    C_EXPR_INITIALIZER_LIST,
};

enum CExprParseStatus {
    C_EXPR_PARSE_OK = 1,
    C_EXPR_PARSE_NOT_EXPRESSION,
    C_EXPR_PARSE_UNSUPPORTED,
    C_EXPR_PARSE_MALFORMED,
};

typedef struct CExpression {
    TokenRange tokens;
    TokenRange type_tokens; /* For cast, type-sizeof and compound literal. */
    enum CExpressionKind kind;
    int32 operator_token;
    int32 left;
    int32 right;
    int32 third;
    int32 first_argument;
    int32 argument_count;
} CExpression;

typedef struct CExprArgument {
    TokenRange designator; /* Empty unless this is a designated initializer. */
    int32 expression;
    int32 next;
} CExprArgument;

typedef struct CExprParseResult {
    enum CExprParseStatus status;
    int32 expression;
    int32 next_token;
    int32 issue_token;
} CExprParseResult;

/* Statement and translation-unit syntax borrows the original token stream.
 * Expression and declaration indices refer to the existing AST arrays.
 * The first_child/next lists contain only immediate children. A preprocessor
 * alternative is represented separately and is never joined to its siblings.
 */
enum CStatementKind {
    C_STMT_COMPOUND = 1,
    C_STMT_DECLARATION,
    C_STMT_EXPRESSION,
    C_STMT_EMPTY,
    C_STMT_IF,
    C_STMT_WHILE,
    C_STMT_DO,
    C_STMT_FOR,
    C_STMT_SWITCH,
    C_STMT_CASE,
    C_STMT_DEFAULT,
    C_STMT_RETURN,
    C_STMT_BREAK,
    C_STMT_CONTINUE,
    C_STMT_GOTO,
    C_STMT_LABEL,
    C_STMT_PP_DIRECTIVE,
    C_STMT_PP_GROUP,
    C_STMT_PP_BRANCH,
    C_STMT_UNSUPPORTED,
    C_STMT_STATIC_ASSERT,
};

enum CExternalKind {
    C_EXTERNAL_DECLARATION = 1,
    C_EXTERNAL_PP_DIRECTIVE,
    C_EXTERNAL_PP_GROUP,
    C_EXTERNAL_PP_BRANCH,
    C_EXTERNAL_UNSUPPORTED,
    C_EXTERNAL_STATIC_ASSERT,
    C_EXTERNAL_PARTIAL_FUNCTION, /* Unresolved header, parsed body. */
};

enum CParseIssueKind {
    C_PARSE_ISSUE_MALFORMED = 1,
    C_PARSE_ISSUE_UNSUPPORTED,
    C_PARSE_ISSUE_PREPROCESSOR,
};

typedef struct CStatement {
    TokenRange tokens;
    TokenRange condition; /* Header expression, or case label expression. */
    TokenRange for_init;
    TokenRange for_step;
    enum CStatementKind kind;
    int32 expression; /* Condition, return value or expression statement. */
    int32 for_init_expression;
    int32 for_step_expression;
    int32 declaration; /* Declaration AST index, or -1. */
    int32 first_child;
    int32 next;
    int32 else_child;
    int32 token; /* Label name or goto target, or -1. */
    int32 directive; /* Preprocessor directive or group index, or -1. */
} CStatement;

typedef struct CExternal {
    TokenRange tokens;
    enum CExternalKind kind;
    int32 declaration;
    int32 function_body; /* CStatement index, or -1. */
    int32 first_child;
    int32 next;
    int32 directive; /* Directive or group index, depending on kind. */
} CExternal;

typedef struct CParseIssue {
    enum CParseIssueKind kind;
    int32 token;
    TokenRange tokens;
} CParseIssue;

typedef struct CStmtParseResult {
    int32 statement;
    int32 next_token;
    int32 issue_token;
    enum CParseIssueKind issue; /* Zero on success. */
} CStmtParseResult;

typedef struct CUnitParseResult {
    int32 first_external;
    int32 external_count;
    int32 issue_count; /* Newly emitted issues. */
} CUnitParseResult;

typedef struct CAst {
    Tokenization *tokenization;
    CDeclaratorNode *declarators;
    CDeclaration *declarations;
    CEnumerator *enumerators;
    CRecord *records;
    CScope *scopes;
    CBinding *bindings;
    CExpression *expressions;
    CExprArgument *arguments;
    CStatement *statements;
    CExternal *externals;
    CParseIssue *issues;
    int32 declarator_count;
    int32 declarator_capacity;
    int32 declaration_count;
    int32 declaration_capacity;
    int32 enumerator_count;
    int32 enumerator_capacity;
    int32 record_count;
    int32 record_capacity;
    int32 scope_count;
    int32 scope_capacity;
    int32 binding_count;
    int32 binding_capacity;
    int32 current_scope;
    int32 expression_count;
    int32 expression_capacity;
    int32 argument_count;
    int32 argument_capacity;
    int32 statement_count;
    int32 statement_capacity;
    int32 external_count;
    int32 external_capacity;
    int32 issue_count;
    int32 issue_capacity;
    bool infer_type_names; /* Opt-in to unknown type-name inference. */
    bool infer_external; /* Only while parsing an external declaration. */
} CAst;

typedef struct CDeclParseResult {
    enum CDeclParseStatus status;
    int32 declaration;
    int32 next_token;
    int32 issue_token;
} CDeclParseResult;

/* The tokenization must outlive the AST; caller owns both objects. */
bool c_ast_init(CAst *, Tokenization *);
void c_ast_free(CAst *);
/* A new scope inherits names from its parent. Scope 0 is global. */
int32 c_ast_scope_push(CAst *);
bool c_ast_scope_pop(CAst *);
/* Name token must be an identifier in this AST's tokenization. */
bool c_ast_bind(CAst *, int32, enum CNameClass);
/* Ordinary identifiers and tags are distinct namespaces. */
enum CNameClass c_ast_lookup(CAst *, int32);
enum CNameClass c_ast_lookup_tag(CAst *, int32);
/* Parse a single C declaration beginning at range.first.
 * On success next_token is the next significant token past the terminator.
 * On failure no nodes, scopes, or bindings are committed.
 * Unsupported extensions return UNSUPPORTED, not invented syntax nodes.
 * Typedefs from headers must be registered with c_ast_bind beforehand.
 */
CDeclParseResult c_ast_parse_declaration(CAst *, TokenRange);
/* Recognizes a complete C type-name using declaration rules and the active
 * typedef namespace. Speculative parsing does not commit any AST changes.
 */
bool c_ast_is_type_name(CAst *, TokenRange);
/* Parses the entire range (ignoring trivia); failed parses roll back all
 * expression and argument nodes. next_token is the next significant token.
 * Use parse_initializer for brace-enclosed initializer lists.
 */
CExprParseResult c_ast_parse_expression(CAst *, TokenRange);
CExprParseResult c_ast_parse_initializer(CAst *, TokenRange);

/* Statement parser consumes one complete statement and commits its AST.
 * Pass a preprocessor analysis to prevent parsing through directives. The
 * analysis is borrowed and must be for the same tokenization.
 */
CStmtParseResult c_ast_parse_statement(CAst *, TokenRange,
                                       CPreprocessorAnalysis *);
/* Parse a whole source range and record each external declaration or
 * preprocessor region. Independent failures create issues and do not prevent
 * parsing later declarations. Does not evaluate conditional directives.
 */
CUnitParseResult c_ast_parse_translation_unit(CAst *, TokenRange,
                                              CPreprocessorAnalysis *);

#endif /* META_AST_H */
