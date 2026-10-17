# Phase 3.2: source-referencing C expression AST

This phase extends the Phase 3.1 `CAst` with general-purpose expression
parsing. The `cmeta` tokenizer, preprocessor, declaration API and `cy_meta`
interfaces remain unchanged. There is no rule checker executable yet.

## Ownership and API

`meta.h` exposes the declarations from `meta_ast.h` and, under
`CMETA_IMPLEMENT`, includes `meta_expr.c` after `meta_scope.c` and
`meta_decl.c`. An initialized `CAst` borrows a live `Tokenization`.

Call `c_ast_parse_expression(&ast, range)` to parse **one complete expression**
from a half-open token-index range. The range must exclude any surrounding
semicolon or top-level call-argument separator. Pass a brace-enclosed
initializer to `c_ast_parse_initializer(&ast, range)` instead; that entry point
also accepts an ordinary assignment-expression initializer. Expressions in
array bounds, enum values, and initializers saved by Phase 3.1 can be parsed
using these entry points, but they are not parsed automatically yet.

Both functions return `CExprParseResult` with a status, a stable root-node
index, the next significant token on success, and an issue token on parsing
failure. The caller owns the `CAst` and calls `c_ast_free` before freeing the
tokenization. A failed parse rolls back all newly allocated expression and
argument nodes; previously committed nodes are unaffected. `NOT_EXPRESSION`
currently denotes an empty or trivia-only range.

Nodes contain **no copied source strings or tokens**. Each `CExpression`
stores a half-open `tokens` range; operators are referred to by token index.
The `type_tokens` range is populated for casts, type-based `sizeof`,
`_Alignof`, and compound literals. The `left`, `right`, and `third` fields are
child expression indices, except that `C_EXPR_MEMBER.right` is the **token
index** of the member name. For conditional nodes, they represent condition,
true expression and false expression. For call nodes, `left` is the callee.
`first_argument` and `argument_count` refer to a linked array of
`CExprArgument` entries. A designated initializer records its designator as
an original token range on the corresponding argument entry. An absent child,
operator or argument index is `-1`. Node indices remain stable when storage
grows; pointers into the arrays do not.

`c_ast_is_type_name(&ast, range)` reuses Phase 3.1 declarator and typedef
recognition to distinguish `(name)` from `(Type)` without guessing from
identifier spelling. The type-name probe **always** rolls back speculative
records, declarators, scopes and bindings, including when it succeeds. This is
sufficient for expression classification, but not a substitute for recording
all side effects of a complete compiler's type-name parsing.

## Recognized syntax

- Identifiers, numeric/character/string literals and adjacent string literals.
- Parenthesized expressions, prefix and postfix unary operations.
- Binary and assignment operators with C precedence and associativity.
- Comma and conditional (`?:`) expressions.
- Function calls, argument lists, array indexing, and member access (`.`/`->`).
- Cast expressions and typedef-aware `sizeof(type)` versus `sizeof expr`.
- Type-form `_Alignof(type)`.
- Compound literals such as `(int[]){1, 2}`.
- Brace initializer lists, nested lists, and field/index designators. Index
  expressions in designators are retained as token ranges for later analysis.

The AST reports syntactic structure, not type correctness. In particular,
`f(x)` is recognized as call syntax regardless of whether `f` is a function,
function pointer, macro, or something else; semantic resolution is outside the
current parser. Macro invocations are not expanded.

## Unsupported syntax and limitations

The parser explicitly reports `UNSUPPORTED` for `_Generic`, `typeof`,
`__typeof__`, `__extension__`, GNU statement expressions, and excessive
nesting. Several compiler intrinsics that take types as arguments are not yet
recognized specially. It reports `MALFORMED` when ordinary expression syntax
is incomplete or there are unconsumed tokens. No complete semantic type
checking, initializer compatibility checking, statement parsing, or general
preprocessor evaluation is attempted.

The caller must supply suitable typedef bindings through the Phase 3.1 scope
API, and use the Phase 2 preprocessing analysis to avoid combining mutually
exclusive source branches. Phase 3.3 will integrate expression parsing with
statement and translation-unit traversal.

## Validation

From the `cmeta` directory:

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
```

`meta_expr.c` contains a dedicated test executable, covering precedence,
associativity, nested calls, casts and type-name ambiguity, compound literals,
initializer designators, typedef shadowing, unsupported forms, malformed
input, source spans, and transactional rollback.
