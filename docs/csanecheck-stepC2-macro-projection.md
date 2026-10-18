# Step C2: bounded macro projection

## Scope

`csanecheck` now constructs a *separate* syntax token view, leaving the
original `Tokenization` intact for source and token checks. Its AST references
the same original token indices and original source locations. The separate
view may replace a macro invocation with **one** identifier or literal token;
unused invocation tokens become trivia only in that view. Expansion is never
used for source-format diagnostics.

Supported forms (provided the definition is in the inspected file):

- Object-like macros with a single-token expansion, including aliases to
  another object-like macro and aliases to a function name.
- Function-like forwarding (`#define ID(X) X`) with one-token arguments.
- Two-argument `##` token concatenation yielding one identifier.
- `#` stringification of one identifier argument.
- The project's `CAT(A, B, ...)` convention (2–8 arguments), with each
  component resolving to one identifier, including simple aliases.

The resulting syntax view lets C-aware checks recognize the generated call
name or literal, for example `#define F STRLIT_LEN` followed by `F("bad")`.
The error still points to the original invocation in the user's file.

## Safety boundaries

- Expand only definitions preceding the invocation. Respect `#undef`.
- Do not choose values defined or redefined under unresolved conditional
  compilation branches. Only recognized file-wide include-guard branches
  qualify as unconditional.
- Do not guess a value when a macro might be redefined conditionally.
- Reject cycles, excessive depth, incorrect argument counts, invocations
  exceeding 8 CAT arguments, malformed delimiters, and outputs that are not
  a valid single identifier or literal.
- Macro-generated operators, statements, comma lists, adjacent string literal
  sequences, variadic macros, and arbitrary token streams are **not** expanded.
- This stage discovers definitions in the current file; transitive header
  macro definitions are not yet imported. `CAT` is the explicit cbase
  convention handled separately.
- Source line columns and token positions are never synthesized or shifted.
  The parser's AST indexes refer to the unchanged original token sequence.
- A successful parse does not establish that every macro body or conditional
  compilation alternative was checked.

`--coverage` adds `projected_macros` (number of projected invocations) and
`projected_tokens` (number of source tokens occupied by those invocations).
These counts are supplemental and do not replace the existing conservative
syntax/statement coverage metrics.

## Validation

`tests/csanecheck/test_macro_expansion.py` covers pasted identifiers,
object aliases, forwarding, stringification, aliases to checked calls,
conditional redefinitions, undefinitions, recursion, argument limits, and
source-location preservation. Existing rule, parser, and legacy comparison
fixtures must remain unchanged.

## Next

Import safe, locally resolvable header macros with explicit provenance,
then handle macro expansions that create *multiple* C tokens and choose
conditional compilation variants when sufficiently determined.
