# csanecheck Phase 6.3: contextual wrapping and legacy-rule audit

This phase migrates the last two **active** rules recorded in the frozen
`tests/csanecheck/rules.json` inventory.

## Rules

- `FMT-CALL-WRAP`: a complete call statement, return of a direct call, or
  assignment/initializer of a direct call should not be broken across lines if
  the flattened statement fits within the configured column limit.
- `FMT-ASSIGN-WRAP`: a standalone assignment or single-declarator initializer
  should not break immediately after `=` when it fits on one line.

Both rules are group-sensitive. A short wrapped statement is accepted if it
belongs to a consecutive, same-indentation group containing a statement that
cannot fit on one line. Call groups require directly adjacent statements;
assignment groups may also contain comment-only lines, but not blank lines.
The checks walk sibling statement nodes and use original token offsets, never
source-text patterns to guess which constructs are calls or assignments.

The checker does not rewrite source. Nested calls are not independently
classified as complete statements. It conservatively skips structures that
cannot be flattened safely (comments inside a candidate, preprocessor regions,
brace-containing or multi-statement ranges, escaped physical newlines, and
adjacent string literals split across physical lines). `error_impl` and
`assert_error` are exempt from the call-wrap rule as in the legacy checker.
Generated `.meta.h` files and vendored Ryu sources are exempt. Existing rule
exemptions are available through `allow-rule` configuration entries.

The parser still does not cover all valid C extensions, expanded macros or
header-provided typedefs. Unsupported statement regions do **not** receive
AST-based wrapping checks. A candidate is also exempted when a nearby
parser-recovery issue in the same uninterrupted source block could hide a
long neighboring statement; this avoids unsupported syntax generating false
"unnecessary wrap" conclusions. A clean diagnostic result is therefore not a proof
of complete syntax coverage; use `--coverage` to see incomplete regions.

## Migration audit

Run `python3 tests/csanecheck/audit_rules.py` to verify that all **28 active**
legacy rule IDs in the frozen inventory are represented in the migration parity
set. The previously disabled `STYLE-TERNARY` rule is not activated. Additional
formatting rules introduced by Phase 6.1 are *not* counted as legacy parity.

Run `python3 tests/csanecheck/compare.py bin/csanecheck` to compare the frozen
legacy fixtures. For project comparisons, use `compare.py --project` as
explained in the Phase 4.3 documentation. These comparisons cover the
supported legacy rules only; project differences or unsupported AST regions
must be investigated rather than silently waived.

New tests: `python3 tests/csanecheck/test_wrap.py bin/csanecheck`.
