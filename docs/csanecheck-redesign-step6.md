# Step 6: local convention-aware classification

`csanecheck_classify.h` exposes a borrowed, token-local API for recognizing
primitive/project types, CamelCase names, uppercase macros, and call-like
invocations. The module never modifies the tokenization or requires an AST.

- `csane_name_kind` recognizes standard C scalar types and cbase aliases,
  CamelCase type-name conventions, uppercase macro names, and the documented
  lowercase `error` macro exception.
- `csane_type_name` returns true for primitive and conventionally named types.
- `csane_classify_statement` recognizes declaration beginnings with optional
  storage/qualifier prefixes. It also accepts uppercase names preceding a
  declarator to accommodate external all-uppercase type names; this is a
  *contextual heuristic*, not a global assertion that macros are types.
- `csane_classify_invocation` recognizes local calls and macro invocations and
  rejects obvious function prototypes. A call within an opaque source region
  can still be recognized independently.

Limitations: declarations involving complex attributes, function pointers,
macro-generated fragments, or split conditional headers may remain unknown.
Classification does not prove syntax validity, resolve typedefs, infer a
complete type system, or provide rule coverage. Unknown is intentionally
preserved instead of being interpreted as malformed C. Existing style rules
and AST behavior are not changed by this step.

Run `python3 tests/csanecheck/test_classify.py` for the focused test.
