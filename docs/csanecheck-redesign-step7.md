# Step 7: AST-independent API call checks

`API-NEVER` and `API-CBASE-ONLY` now use the original token stream in the
TOKEN pass. Direct calls are detected in function bodies, initializers, array
bounds, and macro replacements, without requiring translation-unit AST parsing.
The scanner excludes comments/literals, preprocessor directive text, macro
formal arguments, member calls, and recognized declarators. The existing
name lists, cbase-only path restrictions, and per-rule exemptions are preserved.

The former API pass also parsed macro bodies and declaration initializers into
the AST as an incidental service for formatting and contextual checks. That
work continues in a separate internal syntax-pass preparation operation until
those checks migrate. It does not emit API diagnostics and is not counted as an
active rule or a separate coverage group.

The name/declarator recognizer is intentionally conservative rather than a
complete C declarator parser. Step 18's rule-level coverage will need to
measure residual uncertain contexts explicitly.

Regression checks:

- `python3 tests/csanecheck/test_rules.py bin/csanecheck`
- `python3 tests/csanecheck/test_policy.py bin/csanecheck`
- `python3 tests/csanecheck/test_passes.py bin/csanecheck`
- `python3 tests/csanecheck/compare.py bin/csanecheck`
