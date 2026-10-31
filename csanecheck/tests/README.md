# csanecheck regression tests

Run from the repository root with a freshly built `bin/csanecheck`:

```sh
python3 -m unittest discover -s csanecheck/tests -p 'test_*.py' -v
```

`./build.sh test csanecheck` builds and runs these tests. Do not invoke
`build.sh check` as part of the test suite.

The suite checks csanecheck directly. It covers source and preprocessor rules,
formatting, calls and arguments, declarations, control flow, parser recovery,
forbidden APIs, source discovery, rule inventory, coverage accounting, and
other checker-specific diagnostics. `test_full_tree_coverage.py` verifies
that unresolved coverage does not exceed the checked-in allowlist.

The obsolete Perl-oracle/differential tests have been removed because
`cbase/functions_forbidden_patterns.pl` is no longer present. The
`test_call_wrap.py` neighbor-grouping regression runs csanecheck directly.
`test_full_tree_discovery.py` independently checks the wrapper source-selection
policy; it does not run a Perl checker. The C lexer/structure test programs are
compiled and run by their corresponding Python test modules.
