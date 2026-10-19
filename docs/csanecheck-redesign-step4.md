# csanecheck redesign: Step 4 independent top-level constructs

Step 4 adds `csanecheck_toplevel.h` and `csanecheck_toplevel.c`, a
checker-owned structural view over the source-region hierarchy introduced
in Step 3. It does not change `meta.h`, the tokenizer, the preprocessor,
AST parsing, or any existing style rule. The new view is created for every
analyzed file and provided to future rules through `CsaneFile.toplevel`.

## Model and classification

Each `CSANE_REGION_FILE` and `CSANE_REGION_CONDITIONAL_BRANCH` contributes an
independent code stream, assembled from its **direct** `CSANE_REGION_CODE`
children in source order. Ordinary directives and nested conditional groups
act as gaps. All alternatives are visited, but no preprocessor condition is
evaluated. This also permits recognition of a function whose signature or
body surrounds nested directives, provided the enclosing code contains
matching structural delimiters.

The scanner recognizes:

- Function definitions (`CSANE_TOP_FUNCTION_DEFINITION`), with independent
  header and body token ranges.
- Function prototypes (`CSANE_TOP_FUNCTION_DECLARATION`).
- Type declarations and definitions (`CSANE_TOP_TYPE_DECLARATION`).
- Other semicolon-terminated declarations (`CSANE_TOP_DECLARATION`), including
  aggregate initializers and function-pointer variables.
- Standalone uppercase macro invocations (`CSANE_TOP_MACRO_INVOCATION`), with
  or without semicolons.
- Incomplete or unfamiliar fragments (`CSANE_TOP_OPAQUE`).

Parsing requires only significant source tokens and matching delimiters,
not a symbol table or a complete C grammar. Macro-generated function names
such as `CAT(hash_insert_, HASH_TYPE)(...)` remain unexpanded; their
`name_token` refers to the macro's original identifier. Aggregates and
initializer braces are not mistaken for function bodies.

Every significant token in a direct code stream belongs to an item; the
scanner never silently discards unrecognized code. Opaque items preserve
original spans and are **not** a claim of successful rule coverage. When a
conditional branch is wholly within an already recognized outer function,
type, or declaration, its body is retained as an opaque region rather than
misclassified as file-scope declarations. Step 5 will interpret function
statements, and Step 17 will expand the scope of macro/conditional analysis.

Source spans can cross preprocessing gaps. The item's `TokenRange` is a
contiguous range into the original token array and can include directives;
consumers must use the Step 3 region ownership metadata before applying a
rule to those tokens. All objects borrow the original source and regions.
Free top-level views before freeing their backing regions.

## Integration and acceptance

The checker initializes this view after Step 3 region construction and frees
it before the source regions. Existing AST passes, diagnostics, and
`--coverage` output are unchanged. The new structural items are not yet
used to claim additional rule coverage; that transition is scheduled for
Steps 5–18.

Run:

```sh
python3 tests/csanecheck/test_toplevel.py
```

This test compiles an independent C harness and verifies function headers,
macro-based names, prototypes, function-pointer declarations, typedefs,
aggregate initializers, macro invocations, conditional alternatives,
preprocessor-spanning function bodies, and opaque fragments. It verifies
significant-token ownership for all root and `cbase/` `.c` and `.h` files,
including Ryu, with checks for `hash.c`, `rw_function.h`, and `assertions.h`.
The test is registered in `build.sh test`.

The existing migration and whole-project comparisons remain the checks for
unchanged rule behavior. The eventual target of zero unresolved rule-level
coverage is **not** asserted at Step 4: opaque fragments are deliberately
preserved so later work can address them without masking limitations.
