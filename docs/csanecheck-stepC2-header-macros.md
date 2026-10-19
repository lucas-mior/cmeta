# Step C2.2: imported macros and bounded multi-token projection

The syntax-only token projection can now use macro definitions from quoted
project-local headers, including transitive includes. The original source
and tokenization remain untouched for source- and token-level rules.

## Import and visibility

- Resolve quoted includes relative to their containing file or configured
  `cbase` roots, just as the typedef discovery pass does.
- Process includes and definitions in source order. Local definitions and
  `#undef` directives override imported definitions at the correct positions.
- Recognize conventional include-guard branches as unconditional. Definitions
  under unresolved feature conditions remain unknown, including definitions
  imported by a conditional include. Do not select a branch by guessing.
- Stop cyclic inclusion by tracking the active path, and bound recursion and
  the number of macro events. If the event bound is exceeded, stop projecting.
- Do not treat system headers as local project headers.

## Syntax-only multi-token projection

- Expand simple object-like and function-like macro replacements, argument
  forwarding, basic `##` identifier pastes and single-name `#` stringification.
- Rescan expansions and guard against cyclic or deep macro expansion.
- Preserve the `STRLIT_LEN` call form even when its macro definition is known,
  so wrapper expansions cannot hide its argument from `STYLE-STRLIT-LEN`.
- Project a replacement only if it fits in the *existing significant token
  positions* occupied by the original invocation. Other expansions remain
  unresolved. No tokens are inserted and no AST token indices are shifted.
- Apply a maximum of 64 tokens per expanded invocation; reject malformed or
  excessively complex expansions rather than guessing their interpretation.
- Keep original token positions and source text for user-facing formatting
  diagnostics. Expanded spelling is used for syntax analysis, not for judging
  how replacement text is formatted.

The `projected_macros` counter now denotes *projected invocations*, not the
number of replacement tokens retained in memory. `projected_tokens` continues
counting the input token spans occupied by successful projections.

## Not yet supported

This is **not a complete C preprocessor**. In particular, there is no general
reallocation of the AST token stream; a one-token object-like macro cannot be
projected to multiple output tokens. Variadic substitutions, complex paste
operations, stringification of arbitrary token streams, system-header macro
imports, and selecting feature-dependent branches are not implemented.
Macros that generate entire declarations or need more tokens than their
invocations have available slots are left to structural recovery. Counting
parsed tokens is not proof of per-rule coverage in such regions.

## Verification

`tests/csanecheck/test_header_macros.py` checks direct and transitive imports,
include precedence, undefinitions, conditional ambiguity, cycles, multiple
replacement tokens, token pasting and source-rule independence. The existing
macro projection and legacy-parity suites must continue to pass.
