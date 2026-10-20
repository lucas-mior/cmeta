# `csanecheck` compatibility tests

These tests are **not** a substitute for the existing Perl, shell, or Python
checkers. None of those checkers are removed, disabled, or modified.

## Run

From the repository root, first build the C program and then run the suite:

```sh
./build.sh build
python3 -m unittest discover -s csanecheck/tests -p 'test_*.py' -v
```

`./build.sh test` also runs the compatibility suite after the preexisting C
test step. To test a different binary, set `CSANECHECK_BINARY` to its absolute
path. Python 3's standard library is the only test dependency.

## Fixtures and baseline

`legacy_cases.json` contains **95** test inputs and exact diagnostic-message
snapshots captured from `cbase/test_functions_forbidden_patterns.py` (81
inputs) and `cbase/test_status_temporary_patterns.py` (14 inputs). The
original test function name, source filename, file-list mode, and diagnostic
line/message are retained. The reference messages are mapped to the matching
`csanecheck` rule identifiers before comparison; paths and column numbers are
not compared because their formats differ. Rule counts **are** compared, so an
unrelated diagnostic cannot satisfy an expected rule.

`known_differences.json` lists the **1** remaining fixture-level divergence,
each with the *exact* missing and extra rule counts and a reason.
The compatibility test fails if a new divergence appears, if an existing one
changes, or if a waiver becomes obsolete following a fix. Update or delete the
corresponding entry as the implementation improves. A green baseline therefore
**does not mean full parity**. Tests for status temporaries in `if` conditions
and nested scopes are now passing. `test_step2.py` adds focused positive and
negative regressions for bounds assertions, status uses, and file-scope wraps.

For an independently reproduced snapshot, run:

```sh
python3 csanecheck/tests/capture_legacy.py --check
```

To refresh it after intentionally changing the original tests:

```sh
python3 csanecheck/tests/capture_legacy.py --write
```

The snapshot generator executes the original Python test suites and invokes
the original Perl and status checkers. It intentionally runs only on demand.
If new legacy diagnostic messages appear, add their mapping to
`LEGACY_MESSAGES` in `test_compatibility.py`. Any newly revealed differences
must be investigated, not automatically waived.

## What the suite does and does not cover

In addition to legacy snippets, the Python test covers API policy lists,
`cbase`-only APIs, `--root`/`--file`/`--file-list`, paths containing spaces,
duplicate file-list entries, source-selection options, and CLI error codes.
The reference corpus consists of snippets (including intentionally incomplete
C fragments), not solely valid translation units. The remaining discrepancy is the deliberate extra multiplication-spacing
rule. File-scope call wrapping now follows the legacy behavior. Step 3 now skips generated `.meta.h` files consistently for
all input modes. `test_step3.py` checks project-relative exclusions, `stc`
subtrees, regular C extensions, depth limits including zero, unconditional Ryu
discovery, module filters, environment overrides, and source symlink behavior.
Structural coverage and parser reporting still need further work.

The original shell tests (discovery, environment variables, symlinks, caching
and profiling) and direct status-checker cache tests are not represented by
all their operations in the 95 captured snippets. They remain active in their
original test suites and must be covered with further dedicated compatibility
tests before claiming that the shell script can be replaced.

**This suite is a compatibility baseline with the Step 2 fixes, not
certification that `csanecheck` is a drop-in replacement.**

## Step 4: type imports and preprocessor boundaries

`test_step4.py` exercises syntax coverage through root-level project headers,
transitive quoted includes, include-directory precedence, and imported macro
aliases. It also verifies C11 `bool` declarations, conditional-include
conservatism, independent conditional alternatives, and parse diagnostics
for malformed input.

The import resolver now searches the including file's directory first, then
project roots, then `cbase` roots. This matches the repository's include
search order and allows syntax analysis to recognize typedefs defined in
project-level headers. A conditional include does not make its typedefs
unconditionally available: only ordinary unguarded imports and imports inside
a recognized conventional include guard are collected as definite types.
The macro resolver uses the same quoted-header path resolution, which makes
project-header aliases visible to contextual checks.

This work improves the syntax frontend but does not make it a complete C
parser. Platform-specific types, configurations that depend on system headers,
macro replacements that cannot be projected, and genuinely malformed source
can still produce unresolved syntax. The `--coverage`, `--rule-coverage`, and
`--strict-parse` options remain useful during development. Making incomplete
required coverage cause a reliable overall failure is the separate Step 5.

## Step 5: conservative coverage enforcement

`--require-coverage` makes the checker return status 1 if any non-excluded
syntax-dependent rule group lacks the structural information required to
claim complete analysis, even when no style violations are reported.
`--rule-coverage` reports the same status computation without changing exit
status. A source-level rule is independent of parsing; a token rule requires
a usable preprocessor analysis; syntax/context rules require fully mapped
code tokens and no parser issues or unparsed macro replacement tokens.
This is intentionally conservative and may reject otherwise valid C.

Enforcement is currently **opt-in**, to keep the legacy comparison fixtures
and normal development workflows unchanged while parser coverage improves.
The status is pass-level, not yet exact per-rule source-region proof. It must
not be interpreted as certification that every individual rule is exhaustive.
Do not retire the legacy checkers based on this option alone.

## Step 6: policy audit

The captured 95-fixture corpus now differs only on `FMT-MULTIPLY`, an
intentional extension beyond the Perl checker. This extra policy remains
enabled and is recorded in `known_differences.json`, rather than weakening
the rule for literal legacy compatibility. `test_step6.py` exercises
file-scope call wrapping with independent positive and negative cases.

A fixture-level match is not a proof of full policy equivalence: the corpus
does not exhaust all C constructs, preprocessor configurations, or discovery
behavior. In particular, conservative `--require-coverage` is not yet exact
per-rule/per-region proof. The legacy checkers must remain enabled.

## Step 7: translation-unit macro recovery (partial)

The translation-unit parser isolates known declaration-generating macro
invocations that cannot be projected into C syntax. Each is reported as an
unsupported region rather than merging subsequent invocations into a single
malformed declaration. `--require-coverage` still rejects these unresolved
regions. The Step 7 regressions cover isolated macros, coverage enforcement,
malformed declarations, and macros within functions.

This is **not** complete parser coverage. In particular, token-pasting macros
and many external-header dependent constructs still require more capable
preprocessor projection; unsupported source must not be treated as fully
checked. The legacy checkers remain authoritative.

### Step 7 continuation: translation-unit macro boundaries

`test_step7_continued.py` covers semicolon-terminated declaration macros,
subsequent declarations on the same line, `#undef`/redefinition tracking, and
required coverage failure for unresolved expansions. This is conservative
recovery, not general macro expansion; unresolved token-pasting macros and
conditional definition histories still need full parser/preprocessor support.

### Step 7: conditional macro definitions

`test_step7_conditionals.py` checks that a `#define` or `#undef` inside a
conditional preprocessor branch does not establish a certain global macro
state at later file-scope invocations. A subsequent unconditional directive
resolves that uncertainty. Invocations that might be macros remain marked
unsupported; this change does **not** implement token-pasting expansion or
make their coverage complete.

### Step 7: numeric token concatenation

`test_step7_token_paste.py` checks syntax projection for `identifier ## 12`
(and the equivalent `CAT(identifier, 12)` form). The result must be a valid
identifier; string literals and invalid pasted tokens are not projected.
This only improves expansions fitting the invocation's available token slots.
Larger declaration-generating expansions remain unresolved and must not be
mistaken for complete analysis.
