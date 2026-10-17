# csanecheck — Phase 5.3: expression and contextual checks

Phase 5.3 migrates six active legacy rules to `csanecheck`:

| Rule | Detection |
| --- | --- |
| `STYLE-ASSERT-REL` | `ASSERT(identifier >=/>/<=/< decimal-integer)`; suggest the corresponding dedicated assertion macro |
| `STYLE-ASSERT-BOUNDS` | Consecutive `ASSERT_GE(value, 0)` and `ASSERT_LT(value, upper)` on adjacent physical lines |
| `STYLE-STRLIT-LEN` | `STRLIT_LEN` invoked on a string literal |
| `STYLE-STREQUAL` | `STREQUAL` given an argument of the form `STRLIT("...")` |
| `STYLE-PRINTF` | `str_printf` with a sole printf conversion in its string-format argument, and a following conversion argument |
| `STYLE-STATUS-TEMP` | Function-local `int32 status;` and exclusively immediate assignment/use pairs |

The first five rules inspect expression AST nodes and their original token
ranges, not identifier text matched across comments and strings. Bounds
assertions use structural equality for simple side-effect-free references;
function calls and other potentially side-effecting values are excluded.

The status-temporary rule examines function ASTs, their statement lists, and
the original identifier tokens. It requires exactly one uninitialized
`int32 status;` declaration in the function body, one or more direct simple
`status = ...;` assignments, and one use on the immediately following
physical line per assignment. It counts all other references throughout the
function (except member names, directives, comments, and literals); any
unpaired reference suppresses the recommendation. This is intentionally
conservative; it is not general dataflow analysis and will not prove that
arbitrary reassignment sequences can be transformed safely.

## Typedef context

At the *executable layer*, `csanecheck` registers the concrete typedef names
exported by `cbase/primitives.h` that appear in the current token stream.
This enables the partial parser to recognize common project code such as
`int32 status;` and casts to `(uint32)` without guessing type names from
capitalization or a naming pattern. This does not change `cmeta`'s public
parser API, nor does it resolve arbitrary header-defined typedefs.

## Exemptions and behavior

These rules use the existing `allow-rule RULE_ID REGEX` configuration
mechanism. Guideline rules are not applied to the existing `tests/fixtures/`
exclusion, matching the current rule-dispatch policy. The paired bounds rule
is omitted for vendored Ryu code; source-independent checks remain enabled.

As in previous phases, an unsupported AST region can conceal violations.
The source/preprocessor rules still run on those files. `--coverage` reports
unsupported externals, statements, and parser issues. This phase introduces
no macro expansion, type inference, or general control/dataflow analysis.

## Verification

Run:

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/test_context.py bin/csanecheck
python3 tests/csanecheck/compare.py bin/csanecheck
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
python3 cbase/test_status_temporary_patterns.py
```

The legacy Perl and Python scripts remain unchanged for offline comparison
and until all remaining Phase 6 formatting rules have been migrated.
