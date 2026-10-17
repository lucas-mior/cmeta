# `csanecheck` phase 1: legacy behavior and migration contract

This document is an **inventory and regression baseline**, not the new checker.
The production build still uses `cbase/functions_forbidden.sh`. Phase 1 adds
no parsing features, no new mandatory style restrictions, and no changes to the
`cmeta` or `cy_meta` public API. The `tests/csanecheck` Python runner is an
**offline development/test tool only**; the intended `csanecheck` remains a
native C program.

## Sources of truth and scope

The current enforcement pipeline comprises:

1. `cbase/functions_never_use.txt` (32 names): direct `grep -E` search for
   identifier-like names followed by `(`, across discovered C and header files.
   It reports matches even in comments, strings, definitions, or declarations.
2. `cbase/functions_allowed_cbase_only.txt` (9 names): the same grep scan,
   excluding files resolved inside the selected `cbase` directory.
3. `cbase/functions_forbidden_patterns.pl`: the source-layout, preprocessor,
   declaration, statement, and expression idiom checks.
4. **Also** `cbase/status_temporary_patterns.py`: an independent contextual
   checker invoked by the shell entry point. Replacing the shell program while
   leaving this out would remove an active check.

The written policy documents (`cbase/c-format.md`, `cbase/c-guidelines.md`,
`cbase/README.md`) are the reference for *intent*. The executable checks above
are the reference for *currently enforced behavior*. They do not completely
implement those documents. A proposed policy change should be identified
separately, not silently included in parity work.

`tests/csanecheck/rules.json` defines stable **provisional** rule identifiers,
activation status, minimum analysis layer, a legacy diagnostic classifier, and
notes. The identifiers can later become `csanecheck` diagnostic IDs. The
classifiers match the messages emitted today; they are not rules for parsing C.

## Rule inventory

All active rules below are covered by at least one triggering regression case.
Cases are paired with counterexamples where useful. An asterisk denotes a
scope exception described after the table.

| ID | Active behavior (short statement) | Minimum analysis |
|---|---|---|
| `API-NEVER` | Reject use of the 32 never-use function names | Tokens / calls |
| `API-CBASE-ONLY` | Reject nine low-level libc names outside `cbase` | Calls / path identity |
| `PP-DEFINED` | Prefer `#if defined` / `#if !defined` over `#ifdef` / `#ifndef` | Directive lexer |
| `PP-SIZEOF` | Use signed `SIZEOF`, not builtin `sizeof`* | Tokens / macro definition |
| `SRC-TABS` | No tab characters | Original source |
| `SRC-TRAILING` | No trailing whitespace | Original source |
| `SRC-COLUMNS` | Enforce per-file Unicode character column limit* | Original source |
| `RYU-ENDIF-COMMENT` | Use block comment, not `//`, after `#endif` in Ryu | Source / directives |
| `RYU-FINAL-NEWLINE` | Require final newline in Ryu | Original source |
| `AST-BRACES` | Braces for `if`, `else`, `for`, `while`, `switch`, `do` bodies* | Statement AST |
| `AST-DECL-ONE` | One variable declaration per line; no comma declarators* | Declaration AST + layout |
| `AST-SWITCH-ALIGN` | Align `case`/`default` with nearest `switch`* | Statement AST + layout |
| `AST-ARTIFICIAL-BLOCK` | Disallow extra scope-only blocks at deep indentation* | Statement AST + layout |
| `FMT-CALL-FIRST` | Do not wrap immediately after a call's opening `(` | Call AST + layout |
| `FMT-FUNC-ALIGN` | Align subsequent function-header parameters with the first | Declarator AST + layout |
| `FMT-LITERAL-ALIGN` | Align adjacent multiline string fragments | Call arguments + source |
| `FMT-ARG-ALIGN` | Align continued call arguments with the chosen anchor | Call AST + source |
| `FMT-FORMAT-LINE` | Put all or none of format arguments on the format line | Call AST + source |
| `FMT-RELATED-ARGS` | Keep pairs (`x/y`, `width/height`, `value/value_len`) together | Call arguments + layout |
| `FMT-CALL-WRAP` | Do not needlessly split a call if its adjacent group fits | Call AST + nearby source |
| `FMT-ASSIGN-EQ` | Assignment `=` at end of preceding line, not start of next | Expression + source |
| `FMT-ASSIGN-WRAP` | Avoid needless break immediately after `=` for short groups | Statements + nearby source |
| `STYLE-ASSERT-REL` | Prefer `ASSERT_GE/GT/LE/LT` for simple integer bounds | Call + expression AST |
| `STYLE-ASSERT-BOUNDS` | Collapse consecutive zero/upper bound assertions | Adjacent statement AST |
| `STYLE-STRLIT-LEN` | Do not call `STRLIT_LEN` on a literal string | Call AST |
| `STYLE-STREQUAL` | Do not pass a `STRLIT("...")` literal to `STREQUAL` | Nested call AST |
| `STYLE-PRINTF` | Avoid `str_printf` whose format consists only of a conversion | Call AST + literal |
| `STYLE-STATUS-TEMP` | Inline narrowly matching `int32 status` assignment/use pairs | Function + statements + scopes |

### Scope and exemptions to preserve

- All source rules see headers and `.c` files. The Perl scanner **skips**
  `*.meta.h` and any path containing `tests/fixtures/`; the shell discovery
  additionally excludes `*.meta.h` before invoking any scanner. The two grep
  scans do **not** independently implement the Perl `tests/fixtures` skip.
- For files physically inside `cbase/ryu/` directly under that directory,
  the Perl checker still enforces the generic whitespace/column and
  preprocessor conditional rules, and adds the two Ryu-only style rules.
  It does *not* enforce its brace, declaration, case alignment, artificial
  block, `sizeof`, or consecutive-bounds rules on Ryu. Some generic call and
  expression checks are not explicitly exempted; do not infer a blanket
  Ryu exemption. The shell's two grep lists also apply to Ryu.
- The `PP-SIZEOF` exception covers the `#define SIZEOF` definition and its
  continued lines, as well as Ryu. It is **not** a general macro exemption.
- The brace check permits `else if` and an intentionally empty `for (...);`
  body. This convention is confirmed by the included legacy tests.
- The artificial-block check uses the heuristic of **at least 12 spaces** of
  indentation and attempts to exempt control bodies, types, cases, and
  initializer braces. Do not confuse that heuristic with its intended
  syntactic definition.
- By default the limit is **80 Unicode code points**, rather than terminal
  display cells; `cecup/src/` is allowed **100**. For the `piscou` root with
  `piscou.1` and `config.h`, the shell passes `config.h` as exempt from the
  column limit. Tabs and trailing whitespace still count as violations.
- The `FMT-*` rules involving calls and functions operate on **source
  layout**, with special treatment for format-string anchors, comments,
  escaped newlines, consecutive same-indentation groups and multi-line
  literals. The Perl checker normally skips `.meta.h` entirely. Short but
  consistently grouped wraps are sometimes allowed.
- `STYLE-STATUS-TEMP` only recognizes a function-local `int32 status;` with
  one or more simple assignment/use pairs on *adjacent physical lines*,
  starting with the declaration. It skips comments, literals, directives,
  member names and additional references. It does not make a general claim
  that every temporary named `status` can be eliminated.

### Not active (do not enable incidentally)

- `STYLE-TERNARY`: the Perl function exists, but its invocation is commented
  out. The legacy regression case explicitly checks that ternaries are allowed.
- `FMT-MULTIPLY`: `c-format.md` forbids spaces surrounding multiplication `*`,
  but the checker does not currently enforce that policy. Pointer
  declarations make token-only enforcement hazardous.
- `FMT-TRAILING-COMMA`: `c-format.md` requires trailing commas in arrays
  (and certain structs), but this is not checked by the present pipeline.
- Many other prose recommendations in the two style documents are advice or
  unenforced policy; **the inventory is of code-enforced rules**, not every
  sentence of the documentation.

### Known discrepancies / decisions for the later migration

The fixture `never_comment_false_positive` demonstrates a **known grep false
positive**: `// strcpy(dst, src);` is currently reported by `API-NEVER`.
The `legacy` expectation records this result, while `target` records no
violation. Future `csanecheck` should distinguish comments, strings, function
calls, and declarations using tokens and a partial AST; do not force that
false positive into the replacement.

Similarly, macro-expanded calls require an explicit coverage decision: a
checker operating without complete macro expansion may inspect macro
replacement bodies and their invocation sites, but it must not pretend to
resolve every possible invocation through arbitrarily complex macros. The
initial invariant should be precise diagnostics and explicit uncertainty, not
invented syntax.

The `cystemas` copy of `functions_forbidden_patterns.pl` and the one in this
snapshot are not necessarily byte-for-byte identical. The source of truth for
**the recorded regression corpus** is the `cmeta(1).zip` version. A future
migration should reconcile any project-specific exceptions rather than silently
assume they are universal.

## Discovery, filtering, and exit behavior

The shell entry point is sourced by `cbase/common.sh` during normal builds.
Its source discovery searches the project root (depth 1) and existing
`cbase`, `src`, `test`, `tests`, `examples`, `cyblocks`, and `tools` directories.
Except for the root, depth is limited by `TEST_MAXDEPTH` when set. All
vendored `cbase/ryu/**/*.[ch]` files are discovered regardless of that limit.
The sorted, deduplicated list excludes paths within `stc/`, generated
`*.meta.h`, and paths matching the regex `TEST_EXCLUDE_PATTERN`.

`TEST_FILTER` matches a relative source path, basename, or basename stem.
If the name does not match, `.c` files are normally skipped, while `.h`
files remain selected; selecting `ryu` also keeps vendored Ryu `.c` files.
The shell detects whether `cbase` is embedded or the project itself, and
resolves symlinks when enforcing the cbase-only restriction. It prefers policy
files in `cbase/`, falling back to files in the project root. These behaviors
are observable in the existing `cbase/test_functions_forbidden_patterns.py`
regressions and must be retained or explicitly changed.

The shell groups diagnostics into four categories and exits with **1** if any
violation is found; `0` otherwise. Perl and Python scanners normally return
**0** even when they print diagnostics. Their normal output consists of
`path:line:message`, except that the two grep scanners instead print the
matched source text as the "message". Diagnostics can overlap (for example,
a single short wrapped call may trigger both `FMT-CALL-FIRST` and
`FMT-CALL-WRAP`). Phase 1 intentionally records duplicates/overlaps.

The legacy scripts use separate caches. The Perl cache and Python cache use
file signatures and checker fingerprints, while the two grep stages rescan
selected sources. The project baseline runner sets caches in an isolated
temporary directory and records results without depending on warm-cache state.

## Reproduce the baseline

Run from the `cmeta` directory:

```sh
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
```

The first command checks literal `.txt` fixtures copied to temporary `.c` or
`.h` paths. Text fixtures are intentionally **not** installed as real `.c`
files in the repository: unrestricted source discovery must not treat negative
test examples as production violations. `cases.json` includes both
`legacy` (what exists today) and `target` (what `csanecheck` should eventually
recognize) identifiers. At present the runner compares only `legacy`.

With working trees for the three relevant projects (or an embedded `cbase`
directory), produce a reproducible, machine-readable snapshot:

```sh
python3 tests/csanecheck/baseline.py projects \
    --project cmeta=/path/to/cmeta \
    --project cbase=/path/to/cmeta/cbase \
    --project cystemas=/path/to/cystemas \
    --maxdepth 1 --output /tmp/csanecheck-baseline.json
```

Compare another run against the same snapshot with `--verify
/tmp/csanecheck-baseline.json`. For the **uploaded archive versions** used in
Phase 1, an initial snapshot is committed as
`tests/csanecheck/project-baseline.json`. It contains the legacy checker
file hashes, selected file counts, exit statuses and ordered diagnostics; it
does **not** contain absolute filesystem paths, runtime timings or caches.
Other trees and different discovery depth settings will have different
snapshots; that difference is not automatically a regression.

The initial snapshot with `TEST_MAXDEPTH=1` is clean: **71 selected files for
cmeta, 65 for standalone cbase, and 155 for cystemas**, with no diagnostics.
This is *not* proof that every combination of build filters or other source
trees is clean. The per-rule fixture suite supplies triggering examples.

## Phase 1 exit criteria

- Every active legacy diagnostic is assigned an ID and a minimum analysis
  layer, with source/path exceptions documented.
- Every active diagnostic has at least one triggering fixture; representative
  non-triggering cases are included.
- Documented-only and disabled rules remain distinguishable from active rules.
- Positive/negative cases record known legacy mistakes separately from desired
  future `csanecheck` behavior.
- A cold-cache, isolated, repeatable project baseline is available before any
  lexer, AST or policy changes.
- The existing `cmeta` interfaces and checker behavior remain untouched.

Phase 2 can now change the infrastructure under these compatibility gates.
