# csanecheck: staged guideline analysis

The checker enforces the same legacy rule categories as before. This change
reorganizes rule execution, adds conservative coverage reporting, and does not
introduce new style policies or change the shared `cmeta` public parser API.

## Passes

1. **Source**: `SRC` rules inspect the original bytes and use tokenization
   only to obtain source locations. They run before preprocessor analysis and
   never require a translation-unit AST.
2. **Token**: `PP` and `PP-SIZEOF` rules use preprocessing and lexical tokens.
   They run before constructing the AST. They still run when preprocessing
   reports a structural error, using whatever directives are available.
3. **Syntax**: declaration, direct-call and localized layout checks use the
   token-referencing C AST. These run only after attempting translation-unit
   parsing. Rule-local parsing of initializers and macro expressions is kept.
4. **Context**: assertion/status checks and contextual wrapping inspect AST
   expressions and neighboring statements. Failure in this pass cannot stop
   earlier passes.

A file containing unsupported syntax still receives all source and available
preprocessor checks. Previously supported AST regions continue to receive
syntax and context checks. The engine does not pretend to understand skipped
regions or synthesize grammar out of regex matches.

## Coverage

`--coverage` preserves the existing parser counters and adds a
`coverage_layers` record containing the status of each pass plus:

- `eligible_tokens`: significant ordinary C tokens, excluding comments and
  preprocessing directives;
- `parsed_tokens`: the union of tokens inside recognized AST translation-unit
  regions, excluding unsupported statement/external and parser-issue ranges;
- `unresolved_tokens`: ordinary C tokens not demonstrably represented by a
  recognized region;
- `macro_tokens`: macro replacement tokens, tracked separately because a
  translation-unit AST does not guarantee their expansion or interpretation.

`--rule-coverage` optionally prints a record for each rule **group**. `checked`,
`partial`, `not_checked`, and `excluded` indicate whether the rule group had
its required analysis representation. Source rules can be checked even when
syntax is completely unavailable. AST groups become `partial` if *any* eligible
region or macro is unverified. Thus an entirely clean source file with macro
replacement lists can still have `partial` AST coverage.

**Important:** These estimates are deliberately conservative and file-wide.
They are **not** a proof that every applicable occurrence for a particular rule
was visited. For example, an accepted outer function node does not prove the
parser understood every macro-generated expression inside it. Macro bodies
are counted separately even though some rules already inspect simple macro
replacement expressions. Future parser-tolerance work should add precise
per-rule region provenance and additional recovery paths; it should not lower
these reporting thresholds merely to produce a green coverage result.

Coverage is recorded before rule-local expression parsing mutates the AST.
`--rule-coverage` does not change the exit status, enable extra checks, or
activate previously disabled rules. `--strict-parse` retains its behavior.

## Regression focus

Run `python3 tests/csanecheck/test_passes.py bin/csanecheck` after building.
The cases cover raw and token checks despite malformed C, checked/partial/
not-checked states, incomplete macro coverage, and the original `CY_AD_HELPER`
with `STRLIT_LEN("aaa")`. The annotated-function test guards currently working
behavior; it is not evidence that general annotation macro tolerance exists.

The existing migration comparison and all previous rule fixtures should also
remain green before parser recovery and macro-handling changes are introduced.
