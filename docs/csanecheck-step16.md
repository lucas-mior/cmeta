# Step 16: function-local status analysis

`STYLE-STATUS-TEMP` now runs in the token pass using `CsaneTopLevels` and
`CsaneStatements`, without requiring a translation-unit AST. It retains the
legacy conservative criterion: one direct `int32 status;` declaration and at
least one immediate assignment/use pair, with no other eligible `status`
references anywhere in that function body. A pair must occupy consecutive
source lines, and the first statement must begin with `status =`.

The statement scanner does not link root-level siblings; the status checker
finds successive root statements within the same function and preprocessing
stream using their original source positions. It never joins statements across
different conditional alternatives, and nested references disqualify the
recommendation through whole-body token counting. Member `obj.status`, macro
directives, strings, and comments do not count as direct variable references.

The AST context stage remains in place solely for existing projected-macro
expression-style checks. No tokenizer or existing meta parser API changed.
Coverage remains provisional until the dedicated coverage redesign in Step 18.

Verification includes context fixtures, the checker regression modules,
28-rule inventory audit, and 33/33 migration fixture comparison.
