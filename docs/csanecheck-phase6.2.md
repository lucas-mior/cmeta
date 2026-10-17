# Phase 6.2 — function and call layout

`csanecheck_calls.c` inspects expression-AST calls and declaration-AST
function declarators. Argument spans, names, string literals, columns, and
physical line boundaries all refer to the original `Tokenization`: the checker
does not reconstruct or copy source text into AST nodes.

## Migrated rules

| ID | Behavior |
| --- | --- |
| `FMT-CALL-FIRST` | A nonempty call cannot break directly after `(` before its first argument. Comments interposed before an argument are not treated as plain whitespace. |
| `FMT-FUNC-ALIGN` | Function-declaration/definition parameters that start on a continuation line must align with the first parameter; later parameters on that same physical line are not independently required to align. Function-pointer variables are excluded. |
| `FMT-LITERAL-ALIGN` | Adjacent multiline string-literal fragments of a call argument align with their first fragment. Intervening comments or other syntax interrupt the fragment sequence. |
| `FMT-ARG-ALIGN` | Call arguments beginning on a new physical line after a comma align with the first argument, or with the format-string argument if one exists. Newlines internal to an argument do not trigger alignment of the next argument. |
| `FMT-FORMAT-LINE` | When a percent-containing string-literal argument is followed by value arguments, the line containing the end of that format argument contains either all of those values or none. |
| `FMT-RELATED-ARGS` | Related identifier arguments (`x/y`, `width/height`, `name/name_len`) should stay on one line or each occupy a line of their own. |

The checks do not infer C function calls by searching for `NAME(` patterns;
only recognized call-expression nodes qualify. They distinguish real calls
from declarations, macro definitions, comments, string contents, and
function-pointer declarators. Preprocessor directives and macro replacement
text do not enter the call-layout pass. The legacy generated `*.meta.h`
exception is preserved. Existing `allow-rule ID PATH_REGEX` policy directives
apply to these six IDs.

## Scope and remaining work

This phase intentionally does **not** implement the group-sensitive
`FMT-CALL-WRAP` and `FMT-ASSIGN-WRAP` rules or broader contextual line
breaking; those belong in Phase 6.3. No existing `cmeta` or `cy_meta` public
parser APIs were changed. Unsupported syntax and unexpanded macros can still
prevent an AST-derived rule from running. Use `--coverage` and
`--report-parse-issues` to see that limitation; zero violations does not
mean complete C syntax coverage.

The frozen Phase 1 fixture corpus and legacy policies are unchanged. The
comparison harness now checks these six rule IDs, and the new
`tests/csanecheck/test_calls.py` covers positive/negative cases, call versus
header ambiguity, multiline expressions, format anchors, related pairs,
comments, macro bodies, generated files, and policy exemptions. Two existing
context tests now expect the additional `FMT-CALL-FIRST` diagnostic.

Run `CC=gcc ./build.sh test` and `CC=clang ./build.sh test`; the new tests
also run independently with
`python3 tests/csanecheck/test_calls.py bin/csanecheck`.

The old shell, Perl, and Python checker pipeline remains in place.
