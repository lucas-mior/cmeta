# csanecheck Phase 5.2: control flow and statement structure

The AST-based structure traversal now enforces three additional rules, while
preserving `AST-DECL-ONE` from Phase 5.1. The original source and token
stream remain owned by the tokenizer; AST nodes refer to token indices.

## New diagnostics

- `AST-BRACES`: require compound statement bodies for `if`, `else`, `for`,
  `while`, `do`, and `switch`. `else if` chains and intentional empty `for`
  bodies (`for (...);`) are allowed. The terminating `while` of `do ... while`
  is not treated as an independent loop statement. Statements hidden behind
  unsupported syntax or ambiguous preprocessor boundaries are not guessed.
- `AST-SWITCH-ALIGN`: require each `case` or `default` keyword to begin in
  the same source column as its **innermost enclosing switch**. Labels nested
  inside case scopes inherit the correct switch. This is source formatting
  checked using the statement tree and original token positions.
- `AST-ARTIFICIAL-BLOCK`: reject a standalone, scope-only compound block
  whose opening brace is on a line with at least 12 leading spaces. The AST
  distinguishes those blocks from function bodies, control-flow bodies,
  labeled bodies, record declarations, and initializer lists. This check
  does not apply to generated metadata headers.

As in the legacy checker, all three rules skip vendored Ryu sources,
`*.meta.h` generated headers, and `tests/fixtures/` files. Generated
headers are also exempt from `AST-DECL-ONE`. Existing path-policy configuration supports
`allow-rule RULE_ID REGEX` for each new diagnostic. The general-purpose
translation-unit parser and public `cmeta` interfaces are unchanged except
for one internal function-body parsing refinement described below.

## Conditional compilation in function bodies

If a preprocessor directive occurs inside an otherwise recognizable function,
Phase 3.3 previously cut the declaration range at that directive, often
causing the **whole function** to be marked unsupported. `meta_parse.c` now
retries declaration parsing across the body when a function-like header opens
its brace before that directive. It requires ordinary delimiter matching and
preprocessor-aware matching to agree on the function's closing brace; if the
alternative branches make this ambiguous, the parser retains its conservative
unsupported result.

After recognizing the function, the statement parser visits the preprocessor
branches separately. Rules do not merge statements from mutually exclusive
branches or assume any particular `#if` condition evaluates to true. Full
macro expansion and header typedef discovery are still outside the supported
scope. Parsing coverage reported with `--coverage` remains relevant.

## Tests

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/test_control.py bin/csanecheck
python3 tests/csanecheck/compare.py bin/csanecheck
```

The new CLI tests cover every controlled-body kind, `else if` and empty-loop
exceptions, nested switches and scoped labels, artificial blocks versus
initializers, conditional alternatives, generated headers, path exemptions,
configurable rule exemptions, and partial-parser fallback behavior. The legacy
fixture comparison includes the three new rule IDs; remaining semantic and
formatting rules are intentionally deferred to Phases 5.3 and 6.
