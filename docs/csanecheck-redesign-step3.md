# csanecheck redesign: Step 3 source regions

Step 3 adds `csanecheck_regions.h` and `csanecheck_regions.c`. They build a
checker-owned, preprocessor-aware hierarchy over the **original** source and
`Tokenization`. They do not change the tokenizer API, select conditional
branches, expand macros, or parse C declarations and expressions.

## Representation

- A `CSANE_REGION_FILE` owns the entire original source range.
- Code between directives and nested conditional groups has
  `CSANE_REGION_CODE` regions.
- `#if`, `#ifdef`, and `#ifndef` begin `CSANE_REGION_CONDITIONAL_GROUP`
  regions. Each `#if` / `#elif` / `#else` alternative has its own
  `CSANE_REGION_CONDITIONAL_BRANCH`, containing its selector directive,
  original source body, and any nested groups. `#endif` belongs to the group,
  not to a selected branch.
- Directives retain their original spans. `#define` uses
  `CSANE_REGION_MACRO_DEFINE`, with separate function-like parameter lists,
  replacement text, and unclassified directive text. Physical line splices
  remain in the source ranges.
- Children partition their parent's byte range in source order. Empty byte
  spans do not create separate regions. Every source byte belongs to one
  innermost region. Each token can be located by its original byte offset.

The `CsaneRegions` object borrows both the tokenization and
`CPreprocessorAnalysis`, and must be freed before either of those owners.
Regions expose their parent, next sibling, source and token ranges, and
original directive/group/branch identifiers. `csane_regions_at_offset`,
`csane_regions_at_token`, and `csane_regions_enclosing` provide read-only
navigation. Consumers should treat `CSANE_REGION_DIRECTIVE_TEXT` as raw
preprocessor syntax, **not** ordinary C code.

If the preprocessor analysis contains structural diagnostics, region creation
still succeeds, but `complete` is false and directives are partitioned
without claiming a trustworthy conditional nesting. Such input must not be
reported as fully analyzed. On valid input, the group/branch hierarchy uses
preprocessor metadata without interpreting any condition as true or false.

## Integration and tests

`csanecheck` creates this view after preprocessing each file and supplies it
through `CsaneFile.regions`. Current style rule execution, AST construction,
parse diagnostics, and existing coverage output are unchanged. Later redesign
steps will migrate rules to these regions.

Run the dedicated test with:

```sh
python3 tests/csanecheck/test_regions.py
```

The test verifies source partitions, token positions and preprocessor roles,
macro continuation lines, nested and alternative branches (including
non-C standalone fragments), malformed preprocessing fallback, and every
root-level and `cbase/` `.c` and `.h` source file. It is registered in
`build.sh test`. Existing rule-comparison tests remain the compatibility
check; region presence is not yet proof of per-rule coverage.
