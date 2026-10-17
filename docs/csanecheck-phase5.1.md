# csanecheck Phase 5.1: declarations and parser coverage

Phase 5.1 adds the first AST-based structural rule and explicitly reports
how much syntax the partial C parser recognized. It does not change `meta.h`
or `cy_meta`, and the legacy checker is still invoked by the existing build.

## AST-DECL-ONE

The rule uses C declaration nodes and token source locations rather than
recognizing possible declaration prefixes by regular expression.

- Reject comma-separated declarators in a declaration, including nested
  function pointers and `for`-initializer declarations.
- Reject separate variable declarations beginning on the same physical line
  within one translation-unit, compound-block, or record-member sequence.
- Include typedef aliases and struct/union members, matching the former
  checker's declaration policy. Enum constants and function parameters are
  not independent variable declarations.
- Ignore commas in initializers, nested expressions, function prototypes,
  and parameter lists. Separate function prototypes are not variable
  declarations, while function-pointer variables are.
- Analyze preprocessor alternatives independently. Declarations belonging
  to different branches or lexical blocks are not joined into one sequence.
- Continue to skip `tests/fixtures/` and vendored `ryu/` sources, preserving
  the legacy Perl exemptions. `allow-rule AST-DECL-ONE REGEX` may also exempt
  selected paths.

Only successfully parsed declarations are checked. No fallback regex is
executed on unsupported syntax, and the checker must not interpret a clean
result as proof that every declaration in the file was checked.

## Coverage counters

Use either or both flags:

```sh
bin/csanecheck --file src/example.c --coverage
bin/csanecheck --root . --max-depth 1 --stats
bin/csanecheck --file src/example.c --coverage --report-parse-issues
```

`--coverage` prints one `coverage file=...` record for each selected source;
`--stats` appends aggregate counts to the existing summary. The fields are:

- `declarations`: declaration nodes, **including** parameters and record
  members. These are not necessarily all variable declarations.
- `function_declarations` and `function_definitions`: recognized direct
  function declarators. Function-pointer variables do not count.
- `statements`: understood statement nodes, excluding preprocessor
  wrappers and unsupported statements.
- `expressions`: expression nodes created by the initial translation-unit
  parse. Declaration initializers and macro replacements analyzed later by
  individual rules are deliberately not included.
- `unsupported_statements` and `unsupported_externals`: explicit AST nodes
  for skipped syntax. These are **node counts**, not distinct lines or
  percentages of source covered.
- `malformed_issues`, `unsupported_issues`: parser issue counts by kind;
  separate `parse_issues` also includes preprocessor-boundary problems.
- `preprocessor_issues`: conditional/directive analysis errors.

These counters are a visibility aid, not a claim of complete analysis.
`--report-parse-issues` displays the source locations of parser issues.
Neither a low issue count nor a zero violation count certifies rule coverage;
typedefs from headers and unexpanded macros remain significant limitations.

## Deferred checks

Function header **alignment** and other presentation rules remain in Phase 6.
Context-dependent return/status temporary simplifications remain in Phase 5.3:
inlining an arbitrary temporary can change behavior and is not implied by a
simple declaration/return AST pattern. Phase 5.1 introduces no such automatic
new policy beyond the already-active declaration rule.

## Testing

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/test_structure.py bin/csanecheck
python3 tests/csanecheck/compare.py bin/csanecheck
```

The new test driver covers comma declarators, compound initializers, function
pointers, prototypes, nested scopes, typedef aliases, record members, `for`
initializers, conditional branches, Ryu and fixture exceptions, configuration
exemptions, and partial-parser coverage.
