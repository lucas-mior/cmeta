# Phase 3.3: statements and translation units

Phase 3.3 adds a partial C11 statement and translation-unit parser to the
shared `cmeta` AST. It builds on the Phase 3.1 declaration/scope parser and
the Phase 3.2 expression parser. It neither replaces the existing `cy_meta`
parser nor changes the existing tokenizer, declaration, or expression APIs.

## API and ownership

`meta.h` includes `meta_ast.h` and, with `CMETA_IMPLEMENT`, includes
`meta_stmt.c` and `meta_parse.c` after the existing parsing modules.

- `c_ast_parse_statement(&ast, range, pp)` parses the first statement in a
  range and returns a `CStmtParseResult`. The result includes the stable node
  index, the next significant token, and any first reported issue.
- `c_ast_parse_translation_unit(&ast, range, pp)` appends external declarations,
  function-body trees, preprocessor regions, and recoverable issues. Its result
  contains the first top-level `CExternal` index, the total number of new
  external nodes (including nested branches), and the number of new issues.
- `pp` is an optional borrowed `CPreprocessorAnalysis` initialized from the
  *same* `Tokenization`. When it is `NULL`, the parser constructs and frees a
  temporary analysis. Supplying a shared analysis avoids doing this again.
- Nodes and issues are stored in the supplied `CAst`. They remain valid until
  `c_ast_free` and use stable array indices, never owning token text. The
  tokenization must outlive the AST. Parsing appends nodes and diagnostics;
  callers can inspect issues through `ast.issues[0..ast.issue_count)`.

`CStatement` contains `tokens`, `kind`, and optional child links and source
ranges. `first_child` / `next` link immediate children; `else_child` belongs
to an `if`. The `expression` field is the AST root for expression statements,
conditions, `case`, and `return`, where present. For `for`, separate fields
hold initializer and step expression roots, initializer declaration, and all
three original clause token ranges. A negative child or expression index is
absent. `CExternal` nodes link translation-unit declarations and standalone
preprocessor regions, with function bodies accessible through `function_body`.
All token ranges are half-open indices into the original tokenization.

## Supported constructs

- Compound blocks, declarations, expression and empty statements.
- `if` / `else` (including dangling-else association), `for`, `while`,
  `do` / `while`, and `switch`.
- `case`, `default`, named labels, `goto`, `return`, `break`, and `continue`.
- Declaration-based and expression-based `for` initializers and optional
  `for` clauses.
- Translation-unit declarations and function definitions, including parsing
  the recorded function body after introducing parameter names into its scope.
- Scopes for compound blocks, for-init declarations, function parameters,
  and preprocessor alternatives. A typedef in a nested scope cannot leak into
  subsequent unrelated code.
- Original directives and conditional alternatives as explicit AST nodes.
  `#if` / `#elif` / `#else` bodies are parsed **separately**, not combined
  into a fictitious single C statement sequence. Macro replacement tokens
  are not parsed as executable C statements.

## Recovery and limitations

The parser emits `CParseIssue` records with a kind, token index, and source
range. Errors in independent statements or file-level declarations do not
prevent later constructs from being inspected. Unsupported constructs are
retained as explicit `C_STMT_UNSUPPORTED` or `C_EXTERNAL_UNSUPPORTED` nodes.
Incomplete expression subtrees have an absent root and an issue, rather than
a guessed expression structure. A recovery step may leave unattached nodes
allocated in the AST; only nodes reachable from the returned root or linked
children should be traversed.

Preprocessor conditional expressions are **not evaluated** and macro calls
are **not expanded**. Conditional alternatives do not export typedef bindings
to the surrounding scope, because the active branch is unknown. Declarations
or statements that require combining text on both sides of a conditional
cannot be parsed reliably and are reported as incomplete or unsupported.
The parser also needs type names declared in headers registered via the Phase
3.1 `c_ast_bind` API before parsing source that relies on those types.

The implementation is intentionally not a full C compiler front end. It does
not provide semantic type checking, symbol resolution across translation
units, GNU statement expressions, `typeof`, full C23 grammar, or elaborate
preprocessor macro substitution. The Phase 3.1 declaration parser still
retains initializer and array-bound expression ranges without automatically
attaching expression roots to every declaration.

## Validation

Run from the `cmeta` directory:

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
```

`meta_stmt.c` contains control-flow, scope, preprocessor, and malformed-input
regression tests. `meta_parse.c` contains translation-unit, function-parameter
scope, conditional-branch isolation, and error-recovery tests. The existing
Phase 3.1 and Phase 3.2 test executables also continue to run unchanged.
