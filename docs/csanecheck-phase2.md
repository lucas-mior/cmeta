# `cmeta` phase 2: source and preprocessor infrastructure

Phase 2 adds reusable, source-preserving preprocessing *structure*, without
introducing an AST, compiler-style conditional evaluation, include resolution,
or macro expansion. The Phase 1 rule inventory and checker are unchanged.

## Source lifetime and indexing

- `tokenize()` and `tokenize_with_flags()` still borrow their source buffer;
  callers remain responsible for keeping it alive.
- `tokenize_copy(text, len, flags)` owns a private NUL-terminated copy.
  `free_tokenization()` now frees that buffer when appropriate, as well as
  tokens and the lazy physical-line index. Copies of a `Tokenization` struct
  must **not** be separately freed. Its token text pointers refer into the
  owned buffer until cleanup.
- Source byte offsets, including physical line ranges and CRLF positions, are
  preserved. The existing location API remains unchanged.
- `tokenization_token_at_or_after_offset()` now uses binary search instead of
  scanning every token; it also accepts offsets inside a token. This helps
  repeated source-to-token conversions without introducing a separate index.
- `tokenization_is_in_preprocessor_define()` now uses the shared directive
  recognizer, including `%:` and continued macro definitions.

## Conditional branches and token origins

The new `meta_preproc_analysis.c` provides:

- `c_preprocessor_analysis_init()` and `c_preprocessor_analysis_free()`.
- Directive records in original lexical order.
- Nested conditional groups with linked alternative branches. Each branch has
  a half-open body source/token range and an index for its enclosing branch.
  Bodies are **not concatenated** and may contain nested groups.
- Constant-time token context lookup through
  `c_preprocessor_token_context()` (token role, enclosing branch index,
  directive index). The roles are code, directive, macro parameter list, and
  macro replacement text. The macro-name token stays classified as a
  directive token.
- Structured issues for unmatched/unterminated `#if` blocks, stray/duplicate
  `#elif`/`#else`/`#endif`, and malformed `#define` headers. On a structural
  error, init returns false but leaves the analysis and issues available; the
  caller must still free it.

Analyses **borrow** an unmodified `Tokenization`: it must outlive the analysis.
Use ordinary `tokenize()` or `tokenize_with_flags()` without
`TOKENIZE_PREPROCESSOR_LINES`; that mode collapses a whole line into one token
and does not expose directive syntax for analysis.

Directive ranges include logical line continuations (LF and CRLF). The
function-like macro interpretation still follows adjacency after splice
removal, not a whitespace regex. This phase keeps the existing
`c_preprocessor_define_info()` API intact.

### Interpretation limits

Conditional alternatives are recognized **without selecting a branch**.
An `#if 0` branch still has tokens and can be examined for source-level rules.
An AST consumer in Phase 3 must choose how to handle each branch separately.
This infrastructure does not guarantee tokenization of identifiers split by
backslash-newline, complex trigraph translation, or compiler-specific macro
extensions; such syntax may need explicit handling if encountered in the
actual target projects. It does not determine whether a macro call expands to
an expression or function invocation.

## Tests

`meta_preproc_analysis.c` is a standalone `cmeta` test entry point under the
existing include-based `CMETA_IMPLEMENT` convention. Its tests cover nested
and alternative branches, source and token ranges, multiline macro roles,
spaced object-like macros, trivia-skipped tokenization, digraph directives,
comment/string false positives, and malformed conditionals. `meta_tokenize.c`
also tests independent source ownership, byte offsets, and macro-continuation
queries. The pre-existing `meta_preproc.c` tests remain enabled unchanged.

The optional future delimiter-pair index was intentionally deferred: the
current delimiter matching APIs work and there is no performance measurement
justifying an additional mutable index in Phase 2. Likewise, this phase does
not add a parser-specific error-recovery or formatting-rule API.
