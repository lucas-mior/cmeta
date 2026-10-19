# csanecheck redesign: Step 2 token navigation

Step 2 adds `csanecheck_tokens.h` and `csanecheck_tokens.c`, a borrowed
checker-owned view over existing `Tokenization`/`TokenRange` objects. The
public `meta.h` tokenization API, token definitions and token semantics are
unchanged. The checker builds the new module into its existing single
translation unit. No style rule has been migrated in this step.

The view exposes significant-token navigation, bidirectional matching of
`()`, `[]` and `{}`, full-range balance testing, depth-zero separator search,
range splitting (including nested comma protection), and original source
spans. Where possible these delegate directly to existing `meta` token-range
helpers. Views and resulting ranges do not own source memory and remain valid
only as long as the original tokenization.

A failed match returns -1; an unsuccessful depth-zero search returns
`range.end`, but returns -1 when the range is invalid or unbalanced. Splitting
returns -1 for invalid/unbalanced input and may count items without allocating
storage. The current layer does not attempt recovery across malformed
preprocessor fragments; that belongs to subsequent decomposition steps.

Run independent tests with:

```sh
python3 tests/csanecheck/test_tokens.py
```

The test harness compiles against the unchanged `meta.h` implementation and
checks nested delimiters, trivia, comments, string literals, source spans,
invalid ranges and unbalanced nesting. It is also included in `build.sh test`.
