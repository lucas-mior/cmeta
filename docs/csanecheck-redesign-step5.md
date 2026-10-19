# Step 5: independent function-local statements

`csanecheck_statements.h` exposes `CsaneStatements`, an optional structural
view of recognized function definitions (`CsaneTopLevels`). It does not depend
on the C AST, type inference, or macro expansion. The original tokenizer and
existing style rules are unchanged.

Every statement retains its original source and token span, source-stream
owner, containing function, and parent/child/sibling relationship. Control
statements additionally retain their header and direct body; `if` statements
can identify an `else` alternative. Switch labels are represented separately,
with their containing block and switch available by walking parent links.

Statement recognition handles compound blocks, `if`/`else`, `for`, `while`,
`do`/`while`, `switch`, `case`, `default`, labels, and semicolon-terminated
statements. Nested delimiter groups do not terminate a statement prematurely.

Preprocessor branches are independent token streams. The file stream omits
conditional alternatives, and each branch is examined separately, so mutually
exclusive tokens never form a synthetic statement. Partial branch fragments,
missing header context, and unrecognized syntax are retained as `OPAQUE`
statements rather than abandoned or treated as invalid C. As a consequence,
statement nesting may be incomplete across preprocessor boundaries; downstream
rules must check for opaque regions before claiming complete coverage.

`CsaneFile.statements` points to the new view during analysis. Ownership is
local to a file: `CsaneStatements` borrows `CsaneTopLevels`, which borrows
`CsaneRegions`, and all are freed in reverse order before tokenization.

The test `tests/csanecheck/test_statements.py` verifies nested statement
relationships, conditional branch ownership, and invariants over the project's
root and cbase C sources and headers, including Ryu. Rule migration and
per-rule coverage reporting are later steps; the existing AST parser and its
coverage statistics remain authoritative for the current rule passes.
