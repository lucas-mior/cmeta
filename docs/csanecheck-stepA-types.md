# Parser tolerance — Step A: typedef discovery and inference

`csanecheck` now discovers typedef names from locally resolvable quoted
`#include` chains before parsing each source file. It searches relative to the
including file, then in configured `cbase` roots. It canonicalizes visited
paths and stops at a fixed include-depth limit. It does not run the system C
preprocessor, import angle-bracket system headers, or evaluate `#if`.

The discovery pass extracts aliases from ordinary typedef declarations,
including pointer, function-pointer, and array aliases. It recognizes
file-wide include guards (`#ifndef` and `#if !defined`) and does not mistake
them for feature conditions, including headers with optional implementation
sections after the closing guard. Other conditional typedefs are not
collected as unconditional facts. When a
header is missing, only known system and cbase aliases are seeded. No errors
are emitted solely because a quoted include is absent.

The C declaration parser also has an **opt-in** inference mode enabled by
`csanecheck`, not by `cmeta`'s default public API. An unknown identifier can
act as a type name when a storage/qualifier prefix, an external-declaration
position, or a parameter-declaration position provides sufficient structure.
The *complete* declaration must parse before any inferred binding is kept;
otherwise rollback restores the previous scope and AST. Inferred bindings are
marked `C_NAME_INFERRED_TYPEDEF`, and an inferred declaration records the
original type token at `CDeclaration.inferred_type_token`. Source text is
never copied into AST nodes.

Unknown `a*b;` inside a block remains ambiguous and is not silently turned
into a declaration. A known ordinary identifier shadows a discovered or
inferred typedef, and multiplication involving known ordinary operands still
triggers `FMT-MULTIPLY` when its spacing violates policy. Cast/type-name
probing does **not** use speculative inference.

## Validation

Run the focused regressions:

```sh
./build.sh build
python3 tests/csanecheck/test_types.py bin/csanecheck
python3 tests/csanecheck/test_layout.py bin/csanecheck
```

`tests/csanecheck/test_types.py` includes a violation inside a function whose
return and parameter types are discovered from transitive headers, a fallback
case without headers, and regression cases for ordinary multiplication,
shadowing, typedef declaration order, and missing includes.

## Deliberate limitations

- Annotation macros and unrecognized attribute forms remain Step B.
- Recovery after an unrecognized function header remains Step C.
- Conditional-only aliases are not assumed to be globally available.
- Without typedef context, `Unknown *value;` inside a block is inherently
  ambiguous with multiplication. It can remain unsupported rather than
  guessed incorrectly.
- Typedef discovery is lexical and intentionally incomplete; it does not
  interpret macro-generated typedefs or implement compiler include paths.

## Include-guard recognition

The header collector recognizes an include guard by the structure of its
preprocessor group, not by its position in the file or percentage of bytes
covered. An eligible top-level group has exactly one `#ifndef NAME` or
`#if !defined(NAME)` branch, starts with the matching `#define NAME`, and has
a matching `#endif`. Only comments, whitespace, or preprocessing directives
may occur outside that group. This permits preliminary conditional includes
and trailing implementation-only include sections.

Feature-dependent groups (including compound conditions), groups with
`#elif`/`#else`, groups without a matching definition, and groups leaving
ordinary C declarations outside the guard are not treated as unconditional.
Typedef names nested inside feature-dependent branches are not imported.

The regression suite covers the two conditional sections in `meta_ast.h`, the
implementation section after `META_H`, and compound literals in `meta_stmt.c`.
