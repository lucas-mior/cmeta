# Phase 1 checker regression data

See `../../docs/csanecheck-phase1.md` for the inventory, source discovery
contract, known legacy false positives, and reproduction instructions.

- `rules.json`: stable provisional identifiers and legacy diagnostic mapping.
- `cases.json`: fixture paths, logical source paths, scanner to invoke, actual
  legacy IDs and intended future IDs (which may differ).
- `cases/*.txt`: C snippets saved as non-C extensions so normal source
  discovery cannot accidentally lint deliberate violations.
- `baseline.py fixtures`: runs all isolated cases (including those that invoke
  the shell entry point) and demands a triggering case for every active ID.
- `baseline.py projects`: captures or verifies a deterministic source-set
  snapshot from one or more project roots using the same legacy shell checker.
- `project-baseline.json`: baseline for the uploaded cmeta/cbase/cystemas
  trees at `TEST_MAXDEPTH=1`.

The Python runner is not a dependency of the eventual `csanecheck` executable.

## Phase 5.1 AST structural tests

Run `python3 tests/csanecheck/test_structure.py bin/csanecheck` after
building the executable. These cases check `AST-DECL-ONE` and the optional
`--coverage` counters; the offline comparison script also includes the
legacy declaration fixtures. See `docs/csanecheck-phase5.1.md` for scope and
known coverage limits.
