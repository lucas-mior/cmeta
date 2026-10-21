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

At this baseline: **6 implemented, 19 partial, 0 missing**. None of the
25 categories is yet marked *parity verified*. Status must be promoted only
after tests of both violations and legitimate exceptions agree with Perl.

## Rule groups and legacy test categories

| Legacy keys | Domain | Main regression-test categories |
| --- | --- | --- |
| P01–P05 | Original-source lines and Ryu-specific formatting | `test_project_source_*`, `test_ryu_*`, `test_cecup_*` |
| P06–P07 | Preprocessor and `sizeof` | `test_ifdef_*`, `test_preprocessor_*`, `test_builtin_sizeof_*` |
| P08–P11 | Braces, declarations, switch labels, extra blocks | `test_control_flow_*`, `test_multiple_*`, `test_case_*`, `test_deep_*` |
| P12–P17 | Call and function-header formatting | `test_call_alignment_*`, `test_multiline_string_literals_*`, `test_new_oracle_categories_positive_and_negative`; further tests needed |
| P18 | Short calls wrapped unnecessarily | `test_new_oracle_categories_positive_and_negative`; further tests needed |
| P19–P20 | Assignment `=` position and wrapping | `test_assignment_*`, `test_leading_equals_*`, `test_comparison_equals_*` |
| P21 | Consecutive `ASSERT_GE` / `ASSERT_LT` | `test_consecutive_bounds_*` |
| P22–P25 | Assertions, string literal and formatting idioms | `test_new_oracle_categories_positive_and_negative`; further tests needed |

**At the Step 1 baseline, nine active categories lacked dedicated Perl
regression tests:** P12, P13, P16, P17, P18, P22, P23, P24, and P25.
Step 2 added positive/negative Perl-oracle cases for all nine in
`csanecheck/tests/test_differential.py`, along with strict source-rule
examples for P01–P06. The TSV now points to these tests. This adds a
regression baseline; it does **not** imply complete behavioral parity for
those categories or prove the remaining edge cases.

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

- **Ternary operator**: `check_ternary_operators()` exists, but its only call
  is commented out. `test_non_ryu_ternary_is_rejected_but_macro_is_not()` is
  commented out too. Do not treat ternary detection as required Perl parity.
- **Multiplication spacing**: `test_multiplication_spacing_is_not_checked`
  asserts the Perl checker does not reject it. `FMT-MULTIPLY` is a separate
  `csanecheck` rule, not a parity requirement.
- **Missing trailing commas**: `test_missing_trailing_commas_are_not_enforced`
  confirms there is no Perl diagnostic to port.

Other `csanecheck`-only diagnostics are `API-NEVER`, `API-CBASE-ONLY`,
`STYLE-STATUS-TEMP`, `FMT-MULTIPLY`, `FMT-BINARY-SPACING`, and
`FMT-ASSIGN-SPACING`. These must remain tracked **separately** from Perl
compatibility: their absence from Perl output is not a false positive in
parity tests. The AST/context expression pass currently duplicates the same
`STYLE-ASSERT-REL`, `STYLE-STRLIT-LEN`, `STYLE-STREQUAL`, and `STYLE-PRINTF`
rule IDs also emitted by the token pass. The context pass is intended to
handle macro-projected aliases the token pass does not recognize; verify
that these paths neither double-report nor miss the same legacy cases.

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
