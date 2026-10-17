# Phase 4.2: rule engine and initial guideline checks

The `cmeta` build now produces a native `bin/csanecheck` that enforces a
**subset** of the legacy guidelines. The Phase 4.1 source discovery, diagnostics
and parser-reporting interface remains intact. Unlike Phase 4.1, rule
violations are printed without an opt-in flag and cause exit status 1.

## Design

`csanecheck_rules.c` is an internal dispatcher. Each registered rule gets a
borrowed `CsaneFile` containing the path, original `Tokenization`, preprocessor
analysis and partially parsed AST. A parse failure in one region does not
prevent the source and preprocessor rules from running in that file. Diagnostics
are `path:line:column: error[RULE-ID]: message`, with 1-based locations.
`--report-parse-issues` and `--strict-parse` still control separate
`note[PARSE-*]` reports. `--stats` now also reports `violations=N`.

No `cmeta` or `cy_meta` public API has changed. The rule engine is separate
from source discovery so future phases can add rules without modifying file
traversal. The executable continues to run without Perl or Python; Python is
used only for offline regression tests.

## Implemented rules

| Rule ID | Behavior |
|---|---|
| `SRC-TABS` | Reject tab characters |
| `SRC-TRAILING` | Reject trailing spaces/tabs/CR |
| `SRC-COLUMNS` | Enforce 80 Unicode columns, or 100 for `cecup/src` |
| `PP-DEFINED` | Reject `#ifdef` and `#ifndef` directives |
| `PP-SIZEOF` | Reject builtin `sizeof` with a plausible operand |
| `API-NEVER` | Reject recognized calls to all 32 never-use names |
| `API-CBASE-ONLY` | Reject recognized calls to all nine cbase-only names outside `cbase` |
| `RYU-ENDIF-COMMENT` | Reject `//` comments after Ryu `#endif` |
| `RYU-FINAL-NEWLINE` | Require a final newline in Ryu sources |

For `PP-SIZEOF`, the replacement of `#define SIZEOF(...)` is intentionally
exempt (including backslash continuations). Ryu sources are exempt from the
`sizeof` rule. Comments and string literals are not interpreted as code.
The Perl-derived formatting and preprocessor rules skip `tests/fixtures`,
matching the legacy Perl checker. The API restrictions still inspect fixtures,
as the legacy shell grep does. `.meta.h` files remain excluded by project
discovery.

API rules inspect **recognized direct-call expression nodes**, including
nested calls and parenthesized callee names. They do not mistake function
prototypes, function-pointer declarations, member calls or commented-out code
for direct calls. The checker also parses initializer and array-bound ranges
retained by the declaration AST. Expression-shaped macro replacement lists
and compound blocks in common statement-shaped macros are inspected without
expanding the macros. Names are embedded in the executable from the two
checked-in policy files, and tests ensure the tables cover those files.

## Deferred coverage and known limitations

- Macro expansion, indirect calls, and complex replacement lists are not fully
  resolved. A direct call in an unparsed/unsupported AST region can be missed;
  parsing diagnostics can be inspected separately with
  `--report-parse-issues`. Source and directive checks still run there.
- The nine-name `cbase` exemption currently uses a literal `cbase` directory
  segment in the source path. Canonical identity / symlink resolution,
  project-specific exemptions, and policy configuration are Phase 4.3 work.
- The special `piscou.1`/`config.h` no-column-limit case is not yet
  implemented. Other legacy formatting and semantic rules remain in Phases
  5 and 6. The existing shell/Perl/Python checking pipeline is unchanged.
- Invalid UTF-8 is treated conservatively as individual bytes for column
  counting; the current rule does not validate source encoding.
- `--strict-parse` is still opt-in because included typedefs and macro
  context are not fully available to the partial C parser.

## Validation

From the `cmeta` root:

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
bin/csanecheck --root . --max-depth 1 --stats
```

`tests/csanecheck/test_cli.py` checks the inherited command-line behavior.
`tests/csanecheck/test_rules.py` checks positive and negative rule cases,
including the full forbidden-name lists, casts/parenthesized names, macro
replacement regions, lexical exemptions, AST initializers, Unicode column
counting, source locations, and independent error reporting.
