# csanecheck Phase 4.3: policies and migration comparison

This phase extends the Phase 4.2 executable without changing the `meta.h`
interfaces or replacing the legacy checks during the build. The executable
still implements only nine rules; the remaining rules are Phase 5/6 work.

## Policy resolution

By default, `csanecheck` uses compiled-in fallback function lists. When it
finds a project's `cbase/functions_never_use.txt` and
`cbase/functions_allowed_cbase_only.txt` (or those files at the root of a
standalone `cbase` checkout), it loads them instead. Names are checked as C
identifiers, not substrings in source text.

For `--root`, the policy files are looked up beneath the specified project.
For `--file` and `--file-list`, an existing source file's parent directories
are searched for project policy files, so invoking `csanecheck` from a
separate working directory works. Multiple project roots use a single policy
in this phase: **the last imported lists win**. Run distinct projects
separately if their lists differ.

Use `--config FILE` for explicit, line-based configuration:

```text
# A full-line comment
column-limit 80
columns (^|/)cecup/src/ 100
no-column-limit (^|/)generated_config\.h$
exclude (^|/)generated/
allow-rule SRC-COLUMNS (^|/)special\.h$
never-use prohibited_call
cbase-only low_level_memory
cbase-root /project/common/cbase
```

Supported directives (one per line, whitespace-separated arguments):

- `column-limit N`: default positive column count.
- `columns REGEX N`: override the column limit for matching paths; last
  matching override wins.
- `no-column-limit REGEX`: disable only the line-length rule on matching paths.
- `exclude REGEX`: exclude matching sources from analysis entirely.
- `allow-rule RULE-ID REGEX`: permit one of the nine current rule IDs on a
  matching file; other checks continue normally.
- `never-use IDENTIFIER` and `cbase-only IDENTIFIER`: append names to the
  function policy tables.
- `clear-never-use` and `clear-cbase-only`: reset the corresponding table.
- `never-use-file FILE` and `cbase-only-file FILE`: replace the corresponding
  table with names from a file, with empty lines and `#`-comment lines ignored.
- `cbase-root DIR`: add an explicitly allowed cbase directory.

Paths for the three file/directory directives in config files are relative to
the config file's directory (unless absolute). REGEX values are POSIX extended
regular expressions with no embedded whitespace; use `(^|/)` for a path
segment boundary when matching either absolute or relative source paths.
Unknown directives, rule IDs, invalid names, bad numbers, unreadable files,
and invalid regexes result in exit status 2. Blank lines and full-line `#`
comments are accepted. The checker does not auto-load configuration files.

Command-line equivalents are available for source exclusions, cbase roots,
module selection, and line-length exemptions:

```sh
bin/csanecheck --root . --config policy.txt --stats
bin/csanecheck --root . --exclude '(^|/)generated/'
bin/csanecheck --file src/a.c --no-column-limit 'a\.c$'
bin/csanecheck --root . --filter foo.c --max-depth 1
bin/csanecheck --file src/a.c --cbase-root vendor/cbase
```

Policy directives and command-line overrides are applied in argument order,
after importing the project list defaults. In particular, `clear-never-use`
in a config file can replace rather than merely extend the project defaults.

## Legacy compatibility

The environment variables `TEST_FILTER`, `TEST_MAXDEPTH`, and
`TEST_EXCLUDE_PATTERN` are recognized. An explicit `--filter` or
`--max-depth` overrides its environment equivalent. `--exclude` extends the
environment exclusion regex. A test filter selects the named source module
and headers, with the special `ryu` selection also selecting direct vendored
Ryu C sources. The Ryu subtree remains included at arbitrary depth despite
`TEST_MAXDEPTH`. The checked paths exclude `stc`, `.meta.h`, and build-output
directories as in Phase 4.1.

Source path rules use canonical `realpath()` membership for cbase exemptions,
so a symlink inside cbase pointing to an external file is *not* exempt, while
a symlink outside cbase pointing to an internal cbase file *is* exempt.
Vendored Ryu rules also use the canonical root and apply only to direct source
files under `ryu/`. The standalone `cbase` repository discovers `ryu/` at the
root, not only `cbase/ryu/` in a dependent project. The legacy special case
for `piscou.1` with `config.h` disables its column limit.

## Comparison and validation

The new, offline-only comparison script first runs the frozen Phase 1 legacy
fixtures and then `csanecheck` on the same sources, comparing the expected
**target** rule IDs rather than matching regex false positives verbatim.
Only the nine implemented rule IDs are evaluated; this is not full parity
with the old Perl/Python checks.

```sh
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
python3 tests/csanecheck/compare.py bin/csanecheck
python3 tests/csanecheck/compare.py bin/csanecheck \
    --project cmeta=. --project cbase=cbase --maxdepth 1
```

The last command reports source counts and per-rule differences against the
existing shell/Perl/Python pipeline. It does not fail on project differences
unless `--fail-on-difference` is supplied: AST-based recognition can
intentionally fix legacy false positives, and remaining rules have not yet
been migrated. Fixture failures always return nonzero.

`tests/csanecheck/test_policy.py` covers policy files, invalid directives,
source filters, canonical symlinks, standalone Ryu discovery, and file-list
invocation from outside a checkout.

## Not yet implemented

`csanecheck` does not implement the remaining Phase 5/6 guideline rules,
macro expansion, compiler-equivalent typedef/import resolution, caching, or
replacement of the legacy build hook. The full `cy_meta` test suite is not a
direct requirement of this phase because no shared parser API was changed.
The checker runs independently of Perl and Python; those tools remain only
for legacy build checking and the offline comparison tests.
