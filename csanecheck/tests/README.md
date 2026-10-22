# Step 2 — Differential regression testing

Run from the repository root:

```sh
./build.sh test
# or, with an already built bin/csanecheck:
python3 -m unittest discover -s csanecheck/tests -p 'test_*.py' -v
```

`test_differential.py` reads the 25-rule inventory in
`csanecheck/perl_compatibility.tsv`. It includes positive and negative fixtures for the first six simple
source/preprocessor parity checks (strict assertions) and positive and
negative Perl-oracle cases for P12, P13, P16, P17, P18, and P22–P25, which
previously had no dedicated legacy tests.

The reusable `differential.py` harness invokes the Perl script and the
`csanecheck` binary on exactly the same source inputs. It normalizes paths,
line numbers and rule categories (not text or columns), preserves duplicate
counts, and separates `csanecheck`-only rules from Perl parity. Unrecognized
legacy diagnostics or unknown `csanecheck` IDs cannot silently disappear.

To report (without failing on known incomplete parity) or enforce strict
parity of specific existing source files:

```sh
python3 csanecheck/tests/differential.py --cwd . src/example.c
python3 csanecheck/tests/differential.py --cwd . --strict src/example.c
```

With `--strict`, mismatched legacy categories, lines, duplicate counts, or
unclassified diagnostics cause exit status 1. Without it, all discrepancies
are printed but do not fail the command: **a non-strict run is not a claim of
parity**. Parity of the other 19 categories must be demonstrated as the
migration progresses, including policy exceptions, macros, and branch cases.

The harness can also use `--file-list` for paths with spaces and
`--no-column-limit PATH` for exact-file exemptions, including together.
The command creates neither cache nor temporary file lists in the project.

Perl caching is isolated in temporary test directories; profiling and
external `TEST_FILTER`/`TEST_MAXDEPTH` variables are unset to keep comparisons
reproducible. `--file-list` is supported by the harness. The diagnostic
mapping is an explicit compatibility specification, not a generic fuzzy
text matcher.

## Step 3 — Token utility regression tests

`test_token_utils.py` compiles and runs `token_utils_test.c` with the C11
compiler from `CC` (default `cc`), using a temporary binary. It tests borrowed
bounded views, delimiter matching, splitting nested call arguments, source
spans, parentheses unwrapping, and top-level assignment detection. Malformed
or incomplete local constructs must fail without needing a full translation-unit
parse. The test is included in the normal `build.sh test csanecheck` target.

`csck_tokens.h` is the shared interface for subsequent rule migrations. The
underlying `meta` tokenizer still owns all token and source storage. The new
helpers only return borrowed spans; neither copy text nor expand macros.
`csane_tokens_assignment` only locates syntactic assignment operators and
intentionally makes no symbol-table or lvalue-validity claims.

## Step 4 — Preprocessor source-region recovery

`test_regions.py` compiles and runs `regions_test.c` independently of the
translation-unit parser. It checks that the source-region tree partitions the
original bytes without overlaps or gaps, including directives within function
bodies, nested `#if`/`#elif`/`#else` branches, multiline function-like macros,
and object-like macros.

Valid conditional groups remain represented when another directive is invalid.
The tests cover stray `#endif`, malformed macro parameter lists, unclosed
conditional groups, and `#elif`/duplicate `#else` after `#else`. Each region
has a `complete` flag for *local preprocessor structure*: a malformed directive
or unclosed group is marked incomplete without marking otherwise valid code
regions as incomplete. `CsaneRegions.complete` still reflects the overall
preprocessor result; it is not a gate for building valid conditional groups.

At this stage, the source-region layer still coexisted with the older `CAst`
pipeline. Step 15 removes that obsolete checker-side AST machinery; the active
code retains `CPreprocessorAnalysis` only for directive recognition and branch
membership, without evaluating conditions or expanding macros.

## Step 5 — Fault-tolerant function and statement structure

`test_structure.py` builds `structure_test.c` against the original `meta`
lexer/preprocessor analysis and the lightweight `csck_regions`,
`csck_toplevel`, and `csck_statements` modules. The tests intentionally do
not construct a `CAst` or parse an entire translation unit.

The tests exercise recovery after an unterminated top-level expression, an
unclosed function body, malformed delimiters within a function, and an
unbalanced local statement. In each case, independently recognizable later
functions and statements must remain accessible. Additional fixtures check
compiler-annotated functions, aggregate and compound-literal initializers,
ternary `case` labels, and both alternatives of conditional compilation.

Unrecognized fragments remain `CSANE_TOP_OPAQUE` or `CSANE_STMT_OPAQUE`;
they are not silently considered checked. The resynchronization boundary is
heuristic: a malformed function body can resume at a later function beginning
at column zero, but unusual formatting or arbitrary syntactic corruption may
still leave a larger opaque fragment. This step does not change the existing
AST-backed diagnostic or coverage pipeline (Steps 6 and 15).

## Step 6 — Per-rule coverage accounting

`test_coverage.py` runs the checker with `--rule-coverage` and
`--require-coverage`, validating rule-specific target counts. Each group
reports `eligible`, `checked`, `excluded`, and `unresolved`; the last three
must sum to `eligible`. `checked` with `eligible=0` means there were no targets
for that rule, not that unrelated text was analyzed.

Targets depend on the rule: source lines, preprocessor directives, selected
identifiers, assignments, calls, function headers, statements, declarations,
AST-classified operators, or projected calls. A locally opaque statement is
unresolved for structural rules, while unrelated source/token rules can remain
fully checked. Fixture exemptions are reported as `excluded`. Generated
`.meta.h` files are filtered by source selection before coverage reporting.

`--coverage` retains the legacy parser statistics for comparison but adds a
`coverage_layers` summary of the new rule-target counters. `--require-coverage`
now fails only when a participating rule has unresolved targets. Preprocessor
parse failures no longer automatically downgrade every unrelated token rule.

This is a transitional, conservative accounting model, **not** a proof of full
rule parity: each rule's target recognizer must evolve with the diagnostic as
Steps 7–15 migrate it. In particular, `LAYOUT` still uses the AST, so operator
candidates not known to its expression/declarator recognizer remain unresolved
until Step 14. The original full-file AST is still constructed and its
statistics are still available; its removal is Step 15.

## Step 10 — Call and function-header differential tests

`test_calls.py` tests Perl compatibility of P12–P17 on 18 cases, including
positive and negative examples for nested calls, line alignment, formatting
strings, related argument pairs, macro replacements and conditional branches.
It additionally checks the independent `API-NEVER` and `API-CBASE-ONLY` rules
against token-local direct calls without misclassifying member accesses or
function prototypes.

## Step 11 — complete-call wrapping parity

`test_call_wrap.py` compares the legacy P18 rule with `FMT-CALL-WRAP` on 46
independent positive and negative source fixtures. It checks neighboring-call
groups, short and overlong calls, preprocessor branches, nested calls, calls
in assignments/returns, macro invocations, comment and string-literal
exceptions, and generated-header exemptions. Both source locations and
duplicate counts must agree. No translation-unit AST information is used to
classify these call-wrap candidates.

## Step 12 — Assignment placement and wrapping parity

`test_assignment_wrap.py` uses the Perl oracle to verify P19/P20 on 37 named
source fixtures, plus two column-limit cases. The checks cover leading
`=` versus comparisons, masking of comments and macro replacements, nested
calls and declarations, grouping across comment-only lines, blank-line
separation, long related assignments, aggregate initializers, conditional
branches, duplicate suppression, and special file exemptions. Both source
line and diagnostic multiplicity are compared. The two corrected Perl
multi-statement-line behaviors have separate legacy regression tests.

## Step 14 — Token-local operator layout

`test_layout.py` exercises `FMT-MULTIPLY`, `FMT-BINARY-SPACING`, and
`FMT-ASSIGN-SPACING` without relying on AST expression nodes or translation-
unit typedef resolution. Cases distinguish multiplication from pointer
and function-pointer declarations, unary dereferences, cast expressions,
compound assignments, and operators in different `#if` branches.

The shared `csane_layout_classify` function drives both diagnostics and
`LAYOUT` rule-coverage accounting. A well-classified unary/declarator token is
checked without a spacing diagnostic; a locally incomplete binary expression
is unresolved. The `LAYOUT` pass runs before AST initialization. The remaining
AST construction and context passes are deferred to Step 15.

Unusual lowercase typedef names without a locally recognizable declaration
prefix can remain ambiguous (C gives the same spelling to a typedef and an
ordinary variable). The checker does not infer their meaning from the whole
translation unit; such inputs will need additional bounded recognition if they
occur in the final source-tree coverage evaluation.

## Step 15 — AST-free execution

The active `csanecheck` driver now requires only original-source tokens,
preprocessor regions, top-level constructs and local statements. No full C
translation-unit parse or syntax-only macro projection is performed. The
`test_ast_independence.py` suite verifies this property, including incompatible
conditional branches and malformed statements.

`--report-parse-issues` and `--strict-parse` retain their option names for
compatibility but now report only `PP-STRUCTURE` and `STRUCTURE-OPAQUE` notes.
They **do not** validate complete C syntax. `--stats` and `--coverage` report
local `structural_issues`, `opaque_statements`, and `opaque_externals`, rather
than AST-derived expression and parser metrics. `--require-coverage` continues
to enforce unresolved rule targets separately.

The legacy `csck_context.c`, `csck_types.c`, and `csck_macros.c` modules have
been removed. The active checker uses original tokens, local structural
recognition, conditional token paths, and shallow type-name hints instead;
the shared `meta` library remains unchanged.

## Forbidden API compatibility

`test_api_compat.py` covers replacement of the legacy grep scans using
`functions_never_use.txt` and `functions_allowed_cbase_only.txt`.  It verifies
all checked-in names, direct calls in conditional branches and macro
replacements, cbase-only path handling (including canonical symlink targets),
and the documented differences between textual grep and token-aware API call
classification.  Comments, strings, prototypes and member calls are
intentional legacy false positives and are not diagnostics; parenthesized and
multiline direct calls are intentional token-aware improvements.

## Temporary status compatibility

`test_status_compat.py` differentially checks `STYLE-STATUS-TEMP` against
`cbase/status_temporary_patterns.py`. It covers positive and negative pairs,
blank and comment-only lines, nested blocks, macro and conditional source,
return uses, repeated references, shadowing, and multiple statements on one
line. The one intentional difference is the unsafe case where another
completed statement follows the assignment before the next-line use; csanecheck
does not suggest an inline transformation that would reorder side effects.


## Step 13 — Full-tree legacy parity

`test_full_tree_compat.py` is the permanent repository-level acceptance test
for P01-P25. It compares the legacy wrapper's default source-selection policy
with csanecheck configured to the same roots, then runs both pattern checkers on
that exact file set and requires exact `(path, line, category)` parity including
multiplicity.

Focused synthetic differential fixtures remain required for every P01-P25
category; the full-tree test explicitly verifies that each inventory row still
references at least one such regression test.

## Step 14 — Full-tree coverage acceptance

`test_full_tree_coverage.py` runs the default first-party source discovery with
`--require-coverage` and rejects any unresolved structural or rule candidate
that is not part of the reviewed baseline in
`full_tree_coverage_allowlist.tsv`. The baseline is keyed by file, target,
reason and normalized source text rather than line number, so inserting lines
above a known ambiguity does not require rebaselining. Counts are upper bounds:
removing a known unresolved candidate is accepted automatically, while adding
or duplicating one fails the gate.

The baseline is deliberately narrow. Its current reason classes are
`ambiguous-operator-context`, `conditional construct could not be merged`, and
`control-structure-unrecognized`; each has a focused regression fixture in
`test_layout.py`, `test_structure.py`, or `test_coverage.py`. New unresolved
reason classes require both a focused regression and an explicit review before
they can enter the baseline.

## Step 16 — Unified rule inventory

`csanecheck/rule_inventory.tsv` is the machine-readable enforcement inventory
for `cbase/c-format.md`, `cbase/c-guidelines.md`, and the legacy checker
surface.  Each entry has a stable inventory ID, exact source line range,
section, concise requirement, enforcement classification, current csanecheck
rule IDs where applicable, and notes for exceptions or intentionally manual
judgment.

The four classifications are:

- `implemented`: csanecheck currently enforces the listed requirement;
- `csanecheck_candidate`: the rule is local/token-oriented enough to implement
  without turning csanecheck into a semantic C parser;
- `compiler`: compiler/toolchain diagnostics are the preferred enforcement;
- `manual`: the rule depends on design intent, cross-file semantics, API
  contracts, performance knowledge, or readability judgment.

`test_rule_inventory.py` validates the TSV schema and source ranges, verifies
that referenced csanecheck diagnostic IDs exist, requires every major guide
section and every top-level guide bullet to be covered by an inventory entry,
and verifies that all P01-P25 mappings plus the legacy function-list,
discovery, and temporary-status policies appear in the unified inventory.

## Step 18 — Declaration and type style

`test_declaration_style.py` covers the declaration/type rules that can be
decided from local tokens and permissive declaration structure: function
signature `const`, standard fixed-width `_t` types,
definitely dynamic VLAs, aggregate/pointer typedef policy, and
declaration-after-statement. Ambiguous VLA bounds remain undecided rather than
being guessed. The tests also verify documented typedef exceptions and that
all step-18 diagnostics are valid `allow-rule` policy IDs for exact externally
imposed API/ABI signatures.
