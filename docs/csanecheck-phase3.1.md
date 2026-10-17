# Phase 3.1: source-referencing C declaration AST

This phase adds a reusable declaration, declarator, record and lexical-scope
representation to `cmeta`. It does not introduce an executable or migrate the
existing `cy_meta` AST. Existing tokenizer, splitter and preprocessor entry
points remain unchanged.

## Integration and ownership

`meta.h` exposes the new `meta_ast.h` declarations. Including `meta_ast.h`
independently also works. In the usual single-implementation arrangement,
`#define CMETA_IMPLEMENT` followed by `#include "meta.h"` includes both
`meta_scope.c` and `meta_decl.c`. The added functions are ordinary `cmeta`
functions; they are not tied to `csanecheck`.

Call `c_ast_init(&ast, &tokens)` on a live `Tokenization`, then
`c_ast_parse_declaration(&ast, range)` for each declaration beginning at the
start of a token range. A successful result identifies the new declaration
and the **next significant token** following its semicolon or function body.
Later declarations can be parsed using this position. Each result contains a
status and, when applicable, a failing token index.

All AST nodes refer to the original `Tokenization` using half-open token index
ranges. No source strings, identifier strings or tokens are copied into AST
nodes. The AST owns its dynamically allocated node arrays and scope bindings;
`c_ast_free` releases those arrays, but **does not** free the tokenization.
The tokenization must remain live, with its token buffer and source text
unchanged, until after `c_ast_free`. Node indices remain stable if the arrays
grow; pointers into node arrays do not.

`c_ast_scope_push` and `c_ast_scope_pop` are explicit. The caller manages block
entry and exit until Phase 3.3 adds statement parsing. The root scope is
created by `c_ast_init`; it cannot be popped. `c_ast_bind` registers a name
from a token index and its class. Ordinary names (including typedefs) and
`struct`/`union`/`enum` tags have separate lookup namespaces. A new ordinary
binding in an inner scope can shadow an outer typedef. Struct/union members
are represented as declarations but are not registered as ordinary bindings.

**External typedefs are not guessed from their spelling.** Before parsing a
file that uses typedefs from an included header, the caller must make those
types known to its AST, for example by registering an occurrence of each known
type name with `c_ast_bind(..., C_NAME_TYPEDEF)` or parsing their declarations
first. This is essential to distinguish `T *p;` from multiplication `T*p;`.
A future translation-unit driver may populate the types from an include model
or from project-specific known typedefs.

## Recognized constructs

- Declaration specifiers: C11 built-in type words, storage-class specifiers,
  qualifiers, `_Atomic` and `_Alignas` (with their parenthesized content kept
  opaque), `typedef` names, and `struct`, `union`, and `enum` specifiers.
- Declarators: identifiers, pointers with qualifiers, parentheses, arrays,
  functions, and arbitrary supported combinations, including function
  pointers. Each operation has its own node referring back to tokens.
- Function prototypes, parameter declarations, `void` parameter lists,
  variadic parameter lists and function definitions with an opaque body span.
- Struct and union definitions, their member declarations and bit-field
  widths, enum enumerators and their optional value expressions.
- Initializers and array bounds are saved as token ranges without expression
  parsing (Phase 3.2 will fill that gap).

The distinction between a function returning a pointer (`int *f(void)`) and a
pointer-to-function variable (`int (*f)(void)`) is represented by the different
nesting of `C_DECL_FUNCTION`, `C_DECL_PAREN` and `C_DECL_POINTER` nodes.

## Boundaries and failure policy

`c_ast_parse_declaration` parses **one** declaration from a caller-provided
range. It is not yet a full translation-unit parser, expression parser,
statement parser, macro expander or compiler. The caller should use the Phase 2
preprocessor analysis to avoid merging conditional branches into one invalid
sequence of C tokens. Preprocessor directives at the head of a declaration
are unsupported. Complicated compiler-specific declaration syntax is reported
as unsupported where recognized; malformed input reports a failure, and
unknown type names are not taken to be types.

On parse failure, all nodes, bindings and scopes created by that attempt are
rolled back. In particular, a typedef or enum constant from a failed
struct/enum declaration cannot remain registered. Existing prior AST entries
remain valid. No best-guess declaration nodes are returned on failure.

The parsing API deliberately makes no attempt at complete C semantic checking
(e.g., verifying valid combinations of storage classes, evaluating constant
expressions, or checking type compatibility); those concerns are outside
Phase 3.1.

## Validation

From the `cmeta` root (with Phase 1 and 2 applied):

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
```

The new declaration AST tests live in `meta_decl.c` and are picked up by the
existing `build.sh test` mechanism. They cover declarations, nested
function/pointer declarators, parameter lists, enum and struct members,
shadowing, unsupported declarations and transactional recovery. The larger
`cy_meta` regression suite should be run against the updated `cmeta`
dependency before further refactoring.
