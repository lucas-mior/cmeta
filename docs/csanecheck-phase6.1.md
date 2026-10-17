# Phase 6.1 — operator spacing and localized layout

`csanecheck` now has a dedicated layout pass, `csanecheck_layout.c`, using
expression-AST nodes linked to original token indices and byte ranges. It does
not infer operator meaning from surrounding identifier spellings or tokenize C
with regular expressions.

## Rules

| Rule | Status | Detection |
| --- | --- | --- |
| `FMT-ASSIGN-EQ` | Migrated from legacy Perl | A standalone `=` at the beginning of a non-preprocessor source line (even in an unsupported AST region). Does not mistake `==` for assignment. |
| `FMT-MULTIPLY` | Newly enforced, formerly documented-only | Horizontal space adjacent to a *binary multiplication* `*`. Pointer declarators, dereferences, and function pointers are not binary multiplication. |
| `FMT-BINARY-SPACING` | Newly enforced | Missing whitespace around AST binary `+`, `-`, `&`, `<<`, `>>`, `&&`, `||`, `==`, `!=`, `<`, `<=`, `>`, `>=`. Division, modulo, XOR and bitwise OR keep optional spacing. |
| `FMT-ASSIGN-SPACING` | Newly enforced | Missing whitespace around assignment operators in parsed expressions and declaration initializers (including `for` initializers). |

When a gap contains a comment, the spacing check does not make assumptions
about desired formatting. A newline counts as separation for operators
requiring whitespace. For multiplication, the checker rejects horizontal
spacing on the same line without attempting to reformat line breaks.

## Coverage and exemptions

- Original source text, token indices and source locations remain owned by the
  tokenizer. The AST stores no duplicate source text.
- Expression spacing checks require recognized AST nodes. Declaration
  initializer `=` positions are derived from declarator initializer ranges.
  Unsupported syntax can still hide violations; `--coverage` and
  `--report-parse-issues` expose those limitations.
- `FMT-ASSIGN-EQ` uses the original token stream and preprocessor token roles,
  so it still works when expression parsing fails.
- Preprocessor directives and macro replacements are excluded from these new
  checks. Source formatting of macro replacements needs a separate policy.
- Vendored Ryu files and generated `*.meta.h` files are excluded. The
  structural style checks also skip `tests/fixtures` paths, as before.
- Each ID can be exempted independently with `allow-rule ID PATH_REGEX` in
  the existing `csanecheck` configuration file.

## Legacy compatibility

`FMT-ASSIGN-EQ` uses the Phase 1 inventory's existing ID and is covered by
`tests/csanecheck/compare.py`. `FMT-MULTIPLY` was *not* enforced by the Perl
checker; the new spacing rules are intentional policy additions, so the
legacy parity comparison does not treat their new reports as regressions.
The baseline manifests remain frozen to the original checker behavior.

## Tests

Run `./build.sh test` with both GCC and Clang. The new
`tests/csanecheck/test_layout.py` tests include multiplication versus pointer
syntax, unary/binary ambiguity, mandatory versus optional spacing, initializers,
`for` declarations, preprocessor alternatives, source-line continuation,
comments, generated/vendor/fixture exemptions, unsupported code, and policy
configuration.

Phase 6.2 will handle call and declaration alignment, and Phase 6.3 will
handle contextual wrapping and remaining presentation rules. The existing
legacy checker has not been removed or replaced in the build scripts.
