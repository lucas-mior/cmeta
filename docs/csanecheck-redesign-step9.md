# Step 9: AST-independent declaration and function-header checks

`AST-DECL-ONE` and `FMT-FUNC-ALIGN` now operate on original tokens and
structural top-level ranges. Neither requires the translation-unit AST.
The existing tokenizer interface and source-region representation are unchanged.

* The declaration rule scans top-level declarations and local lexical scopes,
  including record members and `for` initializers, with separate same-line
  state for each scope. Balanced nested initializers and call expressions do
  not create extra declarators. Function declarations are excluded, while
  function-pointer variable declarations remain eligible.
* The function-header rule scans the parameter ranges of independently
  recognized function definitions and prototypes, preserving the original
  column-zero name condition and generated-header exemption.
* Both rule groups run during the token pass. Other expression, formatting,
  and context rules continue using the existing AST until later migrations.
* Existing rule IDs, diagnostics, policy exemptions, and tokenizer functions
  have not changed. AST coverage counters still describe the old parser,
  not rule-complete coverage (scheduled for Step 18).

There are deliberate limitations: constructs classified as opaque by the
structural scanner and declarations split across independent conditional
source streams are not guaranteed to be interpreted as a single declaration.
Step 17 addresses those cases; Step 18 will report unresolved applicability.

Validation: build with `./build.sh build`, run checker regression modules,
then `python3 tests/csanecheck/compare.py bin/csanecheck --project
cmeta=. --maxdepth 8 --fail-on-difference` to compare the selected project.
