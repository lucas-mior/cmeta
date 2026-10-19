# `csanecheck` compatibility tests

These tests are **not** a substitute for the existing Perl, shell, or Python
checkers. None of those checkers are removed, disabled, or modified.

## Run

From the repository root, first build the C program and then run the suite:

```sh
./build.sh build
python3 -m unittest discover -s csanecheck/tests -p 'test_*.py' -v
```

`./build.sh test` also runs the compatibility suite after the preexisting C
test step. To test a different binary, set `CSANECHECK_BINARY` to its absolute
path. Python 3's standard library is the only test dependency.

## Fixtures and baseline

`legacy_cases.json` contains **95** test inputs and exact diagnostic-message
snapshots captured from `cbase/test_functions_forbidden_patterns.py` (81
inputs) and `cbase/test_status_temporary_patterns.py` (14 inputs). The
original test function name, source filename, file-list mode, and diagnostic
line/message are retained. The reference messages are mapped to the matching
`csanecheck` rule identifiers before comparison; paths and column numbers are
not compared because their formats differ. Rule counts **are** compared, so an
unrelated diagnostic cannot satisfy an expected rule.

`known_differences.json` lists **20** previously observed fixture-level
divergences, each with the *exact* missing and extra rule counts and a reason.
The compatibility test fails if a new divergence appears, if an existing one
changes, or if a waiver becomes obsolete following a fix. Update or delete the
corresponding entry as the implementation improves. A green baseline therefore
**does not mean full parity**. Two additional expected-failure tests cover
status temporaries in an `if` condition and a nested scope, which are not part
of the original unit fixture corpus.

For an independently reproduced snapshot, run:

```sh
python3 csanecheck/tests/capture_legacy.py --check
```

To refresh it after intentionally changing the original tests:

```sh
python3 csanecheck/tests/capture_legacy.py --write
```

The snapshot generator executes the original Python test suites and invokes
the original Perl and status checkers. It intentionally runs only on demand.
If new legacy diagnostic messages appear, add their mapping to
`LEGACY_MESSAGES` in `test_compatibility.py`. Any newly revealed differences
must be investigated, not automatically waived.

## What the suite does and does not cover

In addition to legacy snippets, the Python test covers API policy lists,
`cbase`-only APIs, `--root`/`--file`/`--file-list`, paths containing spaces,
duplicate file-list entries, source-selection options, and CLI error codes.
The reference corpus consists of snippets (including intentionally incomplete
C fragments), not solely valid translation units. Some known discrepancies
are caused by lack of file-scope structural coverage; Steps 2–4 will resolve
these as appropriate.

The original shell tests (discovery, environment variables, symlinks, caching
and profiling) and direct status-checker cache tests are not represented by
all their operations in the 95 captured snippets. They remain active in their
original test suites and must be covered with further dedicated compatibility
tests before claiming that the shell script can be replaced.

**This suite is a regression baseline for Step 1, not certification that
`csanecheck` is a drop-in replacement.**
