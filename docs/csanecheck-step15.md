# Step 15: local adjacent-bounds analysis

`STYLE-ASSERT-BOUNDS` runs during the token pass using `CsaneStatements`
instead of AST sibling statements. The check recognizes neighboring
`ASSERT_GE(value, 0)` and `ASSERT_LT(value, upper)` expressions on immediately
adjacent physical lines. The `value` must be the same side-effect-free
identifier, member access, or simply indexed reference in both calls.

Comparison is limited to statements from the same function, source-region
stream, and containing parent. Original source text between statements must
contain only whitespace. This also supports two standalone root statements
when the structural scanner does not emit an enclosing block. The Ryu
exemption and rule-policy filtering are unchanged.

The remaining AST-based `STYLE-STATUS-TEMP` analysis and projected macro
expression-style checks are intentionally left for later steps. No tokenizer
API, token kinds, or tokenizer behavior changed.

Verification: `test_context.py`, `test_passes.py`, all other checker regression
modules, `audit_rules.py`, and `compare.py`.
