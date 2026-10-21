# Legacy forbidden-pattern compatibility specification

This is the Step 1 baseline for replacing
`cbase/functions_forbidden_patterns.pl` with `csanecheck`.

**Authoritative inventory:** [`perl_compatibility.tsv`](perl_compatibility.tsv).
Its 25 `P01`–`P25` entries enumerate every *active diagnostic category*
emitted by the supplied version of that Perl checker. The TSV columns are:

- `legacy_key`: stable compatibility-case key (not a `csanecheck` rule ID).
- `category`: unique diagnostic behavior being migrated.
- `perl_emitter`: emitting Perl routine or named scan in the main file loop.
- `csane_rule_id`: the existing ID in `csanecheck`, as registered in
  `csck_config.c` and emitted by its rule implementation.
- `csane_implementation`: corresponding existing source file(s).
- `status`: `implemented`, `partial`, or `missing` (defined below).
- `legacy_test`: semicolon-separated unittest method names in
  `cbase/test_functions_forbidden_patterns.py` or
  `csanecheck/tests/test_differential.py`; `-` means no dedicated test.
- `parity_note`: legacy behavior and/or an outstanding compatibility risk.

The table is an inventory, **not proof of equivalence**. Status is based on
source inspection in this step:

- **implemented**: a direct raw-source or token implementation exists for
  the diagnostic, with no known dependency on the translation-unit AST.
  Behavior still needs side-by-side tests.
- **partial**: a corresponding implementation exists, but special cases,
  coverage, duplicate detection, or parser-dependent behavior need further
  verification or migration.
- **missing**: no counterpart currently emits that diagnostic.

At the Step 1 baseline there were **6 implemented, 19 partial, 0 missing**.
After Step 13 there are **25 implemented, 0 partial, 0 missing**. The
`implemented` designation identifies the local rule implementation and its
focused differential test coverage. Step 13 also makes full-tree parity a
normal regression test: the legacy and csanecheck source-selection sets must
match, and exact P01-P25 `(path, line, category)` findings must match with
multiplicity preserved.

## Rule groups and legacy test categories

| Legacy keys | Domain | Main regression-test categories |
| --- | --- | --- |
| P01–P05 | Original-source lines and Ryu-specific formatting | `test_project_source_*`, `test_ryu_*`, `test_cecup_*` |
| P06–P07 | Preprocessor and `sizeof` | `test_ifdef_*`, `test_preprocessor_*`, `test_builtin_sizeof_*` |
| P08–P11 | Braces, declarations, switch labels, extra blocks | `test_control_flow_*`, `test_multiple_*`, `test_case_*`, `test_deep_*` |
| P12–P17 | Call and function-header formatting | `test_calls.py` (18 differential cases), `test_call_alignment_*`, `test_multiline_string_literals_*` |
| P18 | Short calls wrapped unnecessarily | `test_new_oracle_categories_positive_and_negative`; `test_perl_call_wrap_parity` |
| P19–P20 | Assignment `=` position and wrapping | `test_assignment_wrap.py` (39 fixtures), `test_assignment_*`, `test_leading_equals_*` |
| P21 | Consecutive `ASSERT_GE` / `ASSERT_LT` | `test_consecutive_bounds_*`; `test_assertion_string.py` |
| P22–P25 | Assertions, string literal and formatting idioms | `test_new_oracle_categories_positive_and_negative`; `test_assertion_string.py` |

**At the Step 1 baseline, nine active categories lacked dedicated Perl
regression tests:** P12, P13, P16, P17, P18, P22, P23, P24, and P25.
Step 2 added positive/negative Perl-oracle cases for all nine in
`csanecheck/tests/test_differential.py`, along with strict source-rule
examples for P01–P06. The TSV now points to these tests. This adds a
regression baseline; it does **not** imply complete behavioral parity for
those categories or prove the remaining edge cases.

Step 9 verifies P10 against dedicated differential cases for declaration
initializers, function pointers, nested structure members, same-line scopes,
prototypes, and conditional branches. It also checks that comma expressions
and function parameters are excluded. The Perl checker intentionally skips
`typedef` declarators; this remains a compatibility exemption.

The `AST-` prefix in several `csanecheck` diagnostic IDs is historical.
For example, P08–P11 presently use the lightweight statement/declaration
scanners; the ID alone does not imply an AST dependency.

## File selection and exemptions: part of compatibility

The following policy must accompany the diagnostic rules. It is not represented
as separate diagnostic IDs in the TSV:

1. Skip `*.meta.h` entirely. Skip `tests/fixtures/` even if explicitly
   requested. `functions_forbidden.sh` also excludes `stc` subtrees from
   discovery. Compare `csck_files.c` selection with the wrapper, including
   explicit paths, filters, symlinks, and directories.
2. Ordinary project files use 80 columns; `cecup/src/` uses 100. The Perl
   `--no-column-limit PATH` switch applies to the exact canonical file path;
   the shell wrapper uses it for `config.h` in a `piscou.1` project.
   Compare this with `csck_config.c`'s regex-based overrides.
3. For direct `cbase/ryu/*.[ch]` files, retain source-line and
   preprocessor-conditional checks, as well as special `#endif //` and final
   newline checks. Exempt Ryu from braces, declarations, switch labels,
   artificial blocks (P08–P11), `sizeof` (P07), consecutive bounds (P21),
   and assignment-specific checks (P19–P20). **Do not exempt Ryu** from
   call formatting (P12–P18) or the last assertion/string idiom scans
   (P22–P25): those Perl checks run unconditionally on eligible files.
4. Ignore comments and literals where Perl masks them. Respect preprocessor
   continuation lines, inactive-configuration branches, nested delimiters,
   source columns, and order/grouping of adjacent calls and assignments.
5. Perl supports `--file-list` (including paths with spaces), caches unchanged
   file diagnostics by path and file signature, and has opt-in profiling.
   The comparison harness must not mistake cache/profiling output or
   differences in diagnostic message wording for rule-coverage differences.
6. Preserve legacy exceptions, including empty `for` bodies, `else if`,
   `do-while`, initializers, grouped wraps, string fragments ending in `\\n`,
   and `error_impl` / `assert_error` call wraps. Refer to the implementation
   and named regression tests for the exact conditions.

The Perl script's output is a plain `path:line:description` stream; the
current `csanecheck` format is `path:line:column: error[ID]: description`.
Step 2 should compare canonical `(path, line, category)` records, matching
legacy text to the stable TSV category, and retain full messages/positions
for debugging. It must compare counts and exemptions as well as positive
findings; do not deduplicate multiple legitimate findings on the same line.

## Explicitly NOT active Perl diagnostics

- **Ternary operator**: the guide says to avoid ternary expressions, but this
  is a preference rather than an enforced diagnostic. Neither the Perl checker
  nor `csanecheck` checks for ternary operators.
- **Multiplication spacing**: `test_multiplication_spacing_is_not_checked`
  asserts the Perl checker does not reject it. `FMT-MULTIPLY` is a separate
  `csanecheck` rule, not a parity requirement.
- **Missing trailing commas**: `test_missing_trailing_commas_are_not_enforced`
  confirms there is no Perl diagnostic to port.

Other `csanecheck`-only diagnostics are `API-NEVER`, `API-CBASE-ONLY`,
`STYLE-STATUS-TEMP`, `FMT-MULTIPLY`, `FMT-BINARY-SPACING`, and
`FMT-ASSIGN-SPACING`. These must remain tracked **separately** from Perl
compatibility: their absence from Perl output is not a false positive in
parity tests. Step 13 disables the redundant AST/context style pass: the Perl checker only
checks directly spelled calls, and the existing token pass now handles all
P21–P25 cases without projected macro aliases. This prevents duplicate
legacy diagnostics. The unused AST context implementation can be removed
with the rest of the AST pipeline in Step 15.

Also separate from this particular Perl script are the wrapper's forbidden
function-name checks and `cbase/status_temporary_patterns.py`. The wrapper
runs other tooling, and `single_use_patterns.py` is a different analysis.
Replacing those invocations requires its own parity work before the wrapper
can safely switch completely to `csanecheck`.

## Acceptance contract for later steps

For each TSV category:

1. Build an isolated violating example and a legitimate counterpart, both
   including important macro and multi-branch cases where relevant.
2. Run the *current* Perl checker as the oracle, preserving file-specific
   exemptions. Compare normalized findings with `csanecheck` while disabling
   unrelated `csanecheck`-only diagnostics in the comparison.
3. Record and resolve differences in counts, locations, file selection,
   false positives, and false negatives. Maintain a rule-specific coverage
   result when local syntax cannot be classified.
4. Finally scan the entire eligible `cmeta` tree; require all 25 active
   categories to be parity verified and every relevant source target to be
   checked or explicitly exempted. A passing Perl scan alone does not prove
   this—synthetic violations are required as well.

## Baseline commands and observations

On this archive, before any checker implementation change:

```sh
python3 -m unittest cbase/test_functions_forbidden_patterns.py -q
# Ran 75 tests: OK

./build.sh build
# Succeeded; emitted pre-existing compiler warnings.
```

No code paths, rules, or build targets are changed by Step 1. Step 2 can use
the TSV as the source of truth for the parity-test harness and as a checklist
for the nine uncovered test categories.

## Step 2 update — differential test harness

`csanecheck/tests/differential.py` loads the TSV, runs both checkers against
identical files, maps diagnostic descriptions to the 25 stable legacy keys,
and compares `(canonical path, line, category)` including multiplicity.
Unknown diagnostics are surfaced, and csanecheck-only rules are reported
separately. Run it with `--strict` to fail on differences; without `--strict`
it is a reporting tool, not an assertion of equivalence.

The new regression suite contains a positive and negative fixture for each
previously untested Perl category and six simple source-rule categories.
The infrastructure is integrated through the **already existing** `test`
branch of `build.sh`; no checker semantics or build-script changes were
necessary. The inventory statuses remain as at Step 1 pending full parity
verification.

## Step 7 update — source, preprocessor, and file-policy validation

Step 7 added `csanecheck/tests/test_source_policy.py`, covering legacy
categories **P01–P07** with strict differential comparisons for matching
(source line, category, multiplicity). These tests exercise Unicode columns,
tabs, trailing whitespace, Ryu `#endif //` and final newlines, nested/direct
Ryu exclusions, conditional directives, comments/strings, continuation macros,
`sizeof` in ordinary source and preprocessor directives, SIZEOF wrapper
exemptions, `.meta.h`, `tests/fixtures`, `cecup/src` and configuration-based
column limits. Coverage does **not** depend on a complete translation-unit AST.

`csck_rules.c` now implements preprocessor-conditional detection with a
monotonic token-masked physical-line scan. This reproduces the legacy
checker's handling of `#ifdef`/`#ifndef` even in macro continuation lines;
parsed preprocessor directive lists alone cannot represent that case.
Ryu's `#endif //` check is deliberately raw-line based, matching its
Perl expression instead of recognizing extra syntax. `sizeof` is also
checked in directive conditions and macro declarations, while retaining the
SIZEOF wrapper exemption and vendored Ryu exception.

**Resolved legacy oracle inconsistency:** the Perl checker previously
used `Encode::decode(..., FB_CROAK)` on its live source line, which
consumed the input string when a line exceeded 80 bytes. Subsequent tab and
trailing-whitespace checks then silently missed those violations, even on
UTF-8 lines whose displayed column count was within the limit. The Perl
checker now uses `LEAVE_SRC` so decoding does not mutate the original line.
Regression tests compare Perl and `csanecheck` on overlong ASCII and UTF-8
lines that also contain forbidden tabs or trailing whitespace. Both report
all applicable diagnostics independently.

The P07 inventory status is now `implemented`; it does not assert final
whole-tree parity. The Step 7 regression cases are recorded in the inventory.

## Step 8 update — control-flow and block parity

`csck_control.c` now treats an opaque control body as unbraced if its first
original token is not `{`, avoiding suppressed findings after a malformed
expression. It recovers the nearest enclosing switch for `case` and `default`
labels in conditional branches using the original source spans. Deep
standalone blocks in separate conditional source streams are checked without
joining mutually exclusive branches, and preceding directives/macro
replacement tokens do not accidentally affect control-header exceptions.

`csanecheck/tests/test_control_flow.py` contains 18 strict differential cases
covering P08 (braces and empty-for/else-if/do-while exceptions), P09 (nested
switches and branch-scoped labels), and P11 (inline/deep blocks, control and
initializer exceptions, multiline macros and conditional branches). Positive
findings, legitimate exemptions, counts and source lines are compared with
Perl. The 76 legacy Perl regression tests remain the oracle.

P08, P09, and P11 are now marked `implemented`. They remain subject to the
complete eligible-tree, coverage and fixture requirements in Step 16; no
whole-translation-unit AST interpretation is added by Step 8.

## Step 10 update — call and function-header diagnostics

P12–P17 have dedicated strict differential tests in `test_calls.py`. They
exercise call-first-argument wrapping, parameter and call alignment, adjacent
string literals, format-string argument distribution, related argument names,
macros, conditional branches, and member access. These rules rely on original
source tokens and bounded delimiter matching rather than whole-file semantics.

`csck_calls.c` no longer checks adjacent string literals contained in a nested
call or embedded in a larger argument expression while checking the outer
call. This prevents a nested call from generating duplicate findings.

The Perl checker also previously mistook indented block-local function
prototypes for calls split before their first argument. It now excludes such
prototypes using a shallow declaration-prefix recognizer, while preserving
checks on calls in assignments and return expressions. The corresponding
legacy regression test and side-by-side test cover this correction.

The `API-NEVER` and `API-CBASE-ONLY` rules are separate from Perl P12–P17.
Step 10 also tests their existing token-only classification for direct calls,
member accesses, prototypes, and macro replacements. This does not establish
parity with the shell wrapper's complete external function-name workflow.

Strict comparisons across 42 existing `meta*` and `csanecheck` source and
header files showed no P12–P17 discrepancies in the tested checkout. Full
repository discovery and all-rule completeness are deferred to Step 16.

## Step 11 update — unnecessary call wrapping (P18)

`csck_wrap.c` now identifies complete invocation statements from local token
ranges, including uppercase macros, ordinary calls, and call initializers or
returns, without depending on the translation-unit AST. The candidate must
start at the beginning of the physical statement (after indentation), end
with a semicolon on the closing-parenthesis line, and have no trailing comment
or extra statement. Line joining respects backslash continuations, comments,
brace and semicolon boundaries, and runs of adjacent string literals where
*every* fragment ends with `\n`. `error_impl` and `assert_error` retain the
legacy exemption.

Candidates are gathered from existing function statements and top-level
ranges, sorted by their original source positions, and deduplicated so the
same source inside independent preprocessor-branch streams cannot be reported
twice. Maximal immediately adjacent, same-indentation call groups are emitted
only when *every* flattened statement fits the configured column limit; a
long neighbor therefore preserves a deliberately wrapped short call.

`test_call_wrap.py` supplies 46 named differential cases comparing the actual
Perl and `csanecheck` diagnostics (including counts and source lines). P18 is
classified as implemented based on these targeted cases, **not** as having
complete full-tree coverage: repository-wide coverage is Step 16.

## Step 12 update — assignment positioning and wrapping (P19–P20)

`csck_layout.c` checks standalone continuation-line assignment `=` tokens
against original source, now also treating completed block comments preceding
`=` as masked source. Comparison operators and macro replacement lists remain
excluded. No AST inference is used.

`csck_wrap.c` reuses the local token recognizer to find complete assignments
in lightweight statements and top-level items. It additionally scans
original tokens for isolated statements not retained by an interrupted
preprocessor source stream. All candidates are source-sorted and deduplicated
before grouping by indentation and intervening source lines. Long or unsafe
assignments protect short neighbors; blank lines, unrelated statements and
preprocessor directives split groups. Trailing comments and inline aggregate
initializers suppress inappropriate flattening, while multiline initializers
cannot suppress independent following statements.

`test_assignment_wrap.py` compares 37 named P19/P20 fixtures plus
two column-limit cases with the Perl oracle and validates diagnostic source
lines and multiplicity. The Perl oracle also received two fixes for
multi-statement physical lines: a candidate containing two semicolons must
not swallow a later standalone assignment, and the final `=` on a line with
a preceding statement is not a standalone assignment-wrap target.

The two rules are classified implemented based on focused parity tests. As
with the other migrated rules, full-tree proof and unresolved-target
accounting remain part of Step 16.

## Step 13 update — assertions, bounds, and string idioms

P21–P25 now have dedicated strict differential tests in
`csanecheck/tests/test_assertion_string.py`. The tests include 61 isolated
positive/negative cases plus macro continuations, both conditional
alternatives, and multiline diagnostic locations.

Token rules match Perl's deliberately narrow criteria for relational
assertions (`[a-z_]+` identifier, decimal integer and semicolon), the
initial unprefixed string-literal spelling in `STRLIT_LEN` and `STRLIT`,
and its single-conversion format grammar. Unlike an AST-based check, they
do not require a complete expression tree or projected macro expansion.

Bounds rules require adjacent physical lines and both semicolons on the
same lines as their calls. String/format findings use the beginning of
the relevant call argument for legacy line parity, which matters when
the first token is on a continuation line. The AST style fallback is no
longer dispatched. The general-purpose parser and AST-dependent operator
spacing remain for later steps. The 25 implemented statuses do not imply
whole-source coverage: Steps 14–17 are still needed.

## Step 15: AST-free diagnostic pipeline

The checker dispatches only source and token-local rules. Function and statement
counts come from the lightweight structural scanners; diagnostic coverage comes
from per-rule targets. Macro replacement text is analyzed as original tokens,
not expanded into projected AST expressions. The former projected-call style
pass was already redundant with the token-local Step 13 checks and is not run.

The `--strict-parse` and `--report-parse-issues` option names are retained, but
refer to local opaque regions and preprocessor issues, not semantic or complete
C parsing. Legacy AST-only statistics have been replaced by local structural
counters. The Step 16 whole-tree check must still validate that all eligible
rule targets are covered.

## Forbidden function-list compatibility

`test_api_compat.py` is the compatibility oracle for the two legacy textual
function scans driven by `functions_never_use.txt` and
`functions_allowed_cbase_only.txt`.  It treats the checked-in name lists as
the policy source and checks every listed name, direct calls, whitespace,
conditional branches, macro replacement text, cbase path membership, and a
symlink whose target is inside cbase.

The legacy shell implementation is deliberately line-oriented grep.  The API
rules are deliberately token-aware instead.  The following grep matches are
therefore **not** compatibility requirements and are regression-tested as
intentional improvements:

- a function declaration or prototype whose name is in a forbidden list;
- a member call such as `object.memcpy(...)` or `object->memcpy(...)`;
- a forbidden-looking call inside a comment or string literal;
- a macro parameter merely named like a forbidden function.

Conversely, token-aware direct calls remain violations even when grep misses
their spelling, including `(strcpy)(...)` and a call whose function name and
opening parenthesis are on different source lines.  These are actual calls and
are intentionally stricter than the legacy textual scan.

Both sides of conditional source are checked independently.  cbase-only
membership follows the legacy wrapper's canonical-path behavior: a symlink
outside cbase whose target is inside cbase is treated as cbase code.  These
intentional differences mean replacement parity for the two name-list scans is
specified in terms of real direct-call behavior, not byte-for-byte grep output.

## Temporary `status` checker compatibility

`test_status_compat.py` covers replacement of
`cbase/status_temporary_patterns.py`. It differentially checks the legacy
script and `STYLE-STATUS-TEMP` for ordinary and multiple assignment/use pairs,
blank lines, comments and literals, nested blocks, macro uses, conditional
branches, return uses, repeated or unpaired references, initialized and
shadowed declarations, and multiple statements on one source line.

There is one intentional behavior difference. The legacy Python checker only
requires a semicolon somewhere between the assignment and the following-line
use. Consequently it reports this pattern:

```c
status = operation(); side_effect();
ASSERT_EQ(status, 0);
```

`csanecheck` deliberately does not report it. Inlining would move
`operation()` after `side_effect()` and can change program behavior. A
statement before the assignment on the same line remains eligible because
inlining preserves its ordering relative to `operation()`.


## Step 13 — Permanent full-tree parity acceptance

`test_full_tree_compat.py` promotes the repository-wide Perl comparison into
the normal unittest suite. It independently models the default source
discovery from `functions_forbidden.sh`, asks csanecheck to discover using the
same legacy roots, and requires the two selected file sets to be identical.

The same selected set is then passed to both checkers through a file list. The
comparison requires exact P01-P25 `(path, line, category)` parity and preserves
duplicate findings on the same line. Csanecheck-only diagnostics remain outside
the Perl-parity result, as in the focused differential harness.

The full-tree check complements rather than replaces synthetic tests. A third
acceptance assertion requires all P01-P25 inventory entries to remain
`implemented` and to name at least one focused regression test. This prevents a
clean repository from making a legacy category appear verified merely because
neither checker happens to emit it.

## Unified guide inventory

`rule_inventory.tsv` is the broader source of truth for migration beyond Perl
parity.  It includes the P01-P25 mappings from `perl_compatibility.tsv`, the
legacy function-list/discovery/status policies, and the enforceable and manual
rules from `c-format.md` and `c-guidelines.md`.  `perl_compatibility.tsv`
remains the exact P01-P25 differential specification; the unified inventory
classifies where each broader guide rule belongs (`implemented`,
`csanecheck_candidate`, `compiler`, or `manual`).
