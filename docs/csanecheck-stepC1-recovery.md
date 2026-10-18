# Step C1: structural recovery without general macro expansion

The translation-unit parser now preserves a balanced function body when its
header cannot be parsed as a normal declarator. This supports source forms
such as a `CAT(...)`-generated function name followed by a conditional
parameter list. The external is represented as
`C_EXTERNAL_PARTIAL_FUNCTION`, with its original header and body token ranges.
It is **not** counted as a confirmed function declaration, and its header
remains an explicitly unsupported parse region.

The body is parsed independently using ordinary statement and expression AST
nodes. This lets the existing rules detect forbidden calls, assertion patterns,
brace violations, and other applicable issues within a partially understood
function. The header's unresolved tokens still contribute to the coverage
report; they are never silently marked as checked.

At translation-unit level, error recovery stops at a top-level semicolon or
an entire balanced braced construct. It no longer resumes at arbitrary
semicolons inside an unsupported function or initializer. Genuine unbalanced
bodies remain unsupported and are not fabricated as valid functions.

Inside functions, `if`, `while`, `switch`, and `for` statements whose headers
contain preprocessor alternatives preserve their controlled body. Their
condition or `for` header is opaque and receives a scoped unsupported issue.
For a preprocessor group that selects an unfinished `if (...)` header with a
shared braced body after `#endif`, the body is attached to the group rather
than treated as an artificial standalone block. The alternative conditions
remain separate, and none are assumed to be the active branch.

## Validation

The new `tests/csanecheck/test_recovery.py` exercises macro-generated function
headers, conditional parameter lists, recovery past malformed code, conditional
control headers, shared conditional `if` bodies, genuine artificial blocks,
pointer versus multiplication handling, and unbalanced syntax. The existing
parser and migration tests must also pass without changing rule semantics.

## Limitations

This step does not expand `CAT`, evaluate `#if`, resolve parameter bindings
from opaque headers, or parse every declaration containing preprocessor
alternatives. A partial function is not semantically equivalent to a complete
function AST, and rules requiring its unresolved declaration information may
remain uncovered. Source/token rules continue to run independently.

Step C2 will address selective macro expansion and declaration-generating
macro forms. Until then, maintainers should use `--coverage` and
`--report-parse-issues` to audit remaining gaps.
