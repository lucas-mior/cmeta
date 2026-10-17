# Phase 4.1: executable infrastructure

Build from the cmeta root with `./build.sh build` (release) or
`./build.sh debug` (debug). The executable is `bin/csanecheck`.
`./build.sh test` also builds it and runs the CLI integration suite, along with
normal cmeta unit tests. The build does **not** invoke the new executable on
its own sources during compilation (avoiding a self-bootstrap dependency).

## Installation

`./build.sh install` compiles a release build and installs `csanecheck` to
`/usr/local/bin/csanecheck` by default. Run with appropriate permissions for
that destination. Override the installation prefix or use a staging directory
without root privileges:

```sh
PREFIX=/opt/cmeta ./build.sh install
DESTDIR=/tmp/cmeta-package PREFIX=/usr/local ./build.sh install
```

The second example installs to
`/tmp/cmeta-package/usr/local/bin/csanecheck`. `PREFIX` must be an absolute
path; `DESTDIR` is prepended for packaging. Installation does not copy the
source tree, policy files, or test fixtures: the executable includes default
policy names and may load project-local overrides during analysis.

`./build.sh test` includes installation tests using temporary staging
folders, without modifying `/usr/local`.

## CLI

```
csanecheck [--root DIR] [--file PATH] [--file-list PATH]
           [--max-depth N] [--report-parse-issues] [--strict-parse]
           [--stats]
csanecheck PATH [PATH ...]
```

Without a path, the current directory is used as `--root .`. Positional
paths denote a project root if they are directories, otherwise a single file.
Multiple paths and sources may be combined; textual duplicate paths are
removed and processing occurs in sorted order. File-list lines are paths
relative to the **current working directory**, not the list's parent; CRLF
and empty lines are supported. Explicit `--file` arguments do not require a
`.c` or `.h` suffix.

Project-root discovery inspects `.c` and `.h` files immediately in the root
and under the existing `cbase`, `src`, `test`, `tests`, `examples`, `cyblocks`,
and `tools` directories. It skips `.git`, `bin`, `stc`, generated `*.meta.h`,
and other hidden build folders. The vendored `cbase/ryu` subtree is inspected
regardless of `--max-depth`. Without `--max-depth`, project subdirectories are
recursive. With `--max-depth 1`, only files directly in each selected source
directory are included. Directory symlinks are not followed on POSIX systems.
File I/O or directory traversal failures are reported and cause an error exit.

## Parser and diagnostics

Each selected file is loaded once into a source buffer, tokenized, analyzed
for preprocessor regions, and parsed as a translation unit. AST nodes refer
to the original tokens; no token text is copied into AST nodes. The AST,
preprocessor state, tokenization, and source buffer are freed before the next
file. Existing `cmeta` and `cy_meta` interfaces are unchanged.

Phase 4.1 has **no guideline rules**; a successful parse is not a statement
that source conforms to the coding guidelines. Parse reports are explicitly
opt-in, because typedefs from included headers and complex macros are not
yet resolved. `--report-parse-issues` writes informational diagnostics without
changing the exit status. `--strict-parse` enables the same diagnostics and
exits with 1 when any file has an issue. Typical diagnostics look like:

```
file.c:7:3: note[PARSE-UNSUPPORTED]: unsupported C construct
```

Exit codes: `0` for successful scanning (even with parser limitations, unless
strict), `1` for strict parser issues, and `2` for bad arguments or I/O errors.
`--stats` emits aggregate counts of files, tokens, AST external nodes, parse
issues, and preprocessor issues. Parse errors are deliberately not guideline
violations. The rule engine, configurable exemptions, and legacy filter parity
belong to Phases 4.2 and 4.3.

## Validation

```
CC=gcc ./build.sh test
CC=clang ./build.sh test
python3 tests/csanecheck/baseline.py fixtures
python3 cbase/test_functions_forbidden_patterns.py
```

Existing project smoke checks can use
`bin/csanecheck --root /path/to/project --max-depth 1 --stats`.
The number of unsupported constructs on real projects should be recorded but
not treated as a guideline failure until typedef/macro support improves.
