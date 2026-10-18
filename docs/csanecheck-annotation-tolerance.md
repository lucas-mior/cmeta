# Macro annotation tolerance and C11 static assertions

This step operates on the original token stream. It does not copy source text
into AST nodes or expand general function-like macros.

## Recognized declaration annotations

The declaration parser recognizes standalone uppercase annotation markers
before or between declaration specifiers, including `CY_AD_HELPER`, `INLINE`,
and `UNUSED`. It also handles lowercase `noreturn`, GNU `__attribute__((...))`,
and `__declspec(...)` wrappers. Function declarations can have trailing
annotation calls such as `ATTR_PRINTF(1, 2)`.

Recognition uses surrounding syntax: an uppercase identifier is not discarded
merely because it is uppercase. In particular, `HASH_KEY_TYPE *key` and
`HASH_VALUE_TYPE value` can be type-macro declarations, and `int32 LIMIT;`
remains a declaration of `LIMIT`. No rule rewrites the original tokens.

C11 `_Static_assert(expr, message);` is recognized in translation units and
compound blocks. Its arguments are retained as an opaque balanced token
range; they are not evaluated. The checker still reports independent lexical
violations inside these constructs.

## Coverage and limitations

The changes allow AST-dependent rules to inspect the bodies of functions with
common annotations. For example, `STRLIT_LEN("abc")` inside a function
preceded by `CY_AD_HELPER` is checked normally.

This is not a general macro-expansion system. Token pasting (`CAT(...)`),
macros that generate statements, conditional function signatures, string
literal assembly, and expression-generating macro substitutions can remain
unsupported. The parser does not assume every uppercase identifier is an
empty annotation. Existing `--coverage`, `--rule-coverage`, and
`--report-parse-issues` modes continue reporting unresolved regions.

## Validation

Run `python3 tests/csanecheck/test_annotations.py bin/csanecheck` after
building. The suite includes both positive acceptance cases and negative
cases that must remain malformed or retain a valid C interpretation.
