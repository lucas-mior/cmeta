#!/bin/sh -e

# shellcheck disable=SC2086
# shellcheck disable=SC2034

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. "./cbase/common.sh"

script=$(basename "$0")
common_build_parse_args "$@"

case "$mode" in
build|debug|debug-fast|fast_feedback|install|test)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

common_build_print_invocation "$script"

if [ "$mode" = install ]; then
    PREFIX="${PREFIX:-/usr/local}"
    DESTDIR="${DESTDIR:-}"
    case "$PREFIX" in
    /*)
        ;;
    *)
        error "PREFIX must be an absolute path: %s\n" "$PREFIX"
        exit 1
        ;;
    esac
fi

CC=$(common_get_compiler "$mode")
CPPFLAGS="$CPPFLAGS -I. -Icbase"
CFLAGS="$CFLAGS -std=c11 -Wfatal-errors"
LDFLAGS="$LDFLAGS -lm"

case "$mode" in
debug|test)
    CFLAGS="$CFLAGS -g3"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -g2 -O2 -flto"
    CFLAGS="$CFLAGS -fsanitize-trap=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
build|install)
    CFLAGS="$CFLAGS -g -O2 -flto"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=0"
    ;;
fast_feedback)
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=0"
    ;;
esac

mkdir -p bin
# One translation unit keeps the shared cbase/cmeta implementation model.
# Do not execute csanecheck while bootstrapping its own binary.
# shellcheck disable=SC2086
trace_on
$CC $CPPFLAGS $CFLAGS -o bin/csanecheck csanecheck_main.c $LDFLAGS
trace_off

if [ "$mode" = install ]; then
    trace_on
    install -Dm755 bin/csanecheck "${DESTDIR}${PREFIX}/bin/csanecheck"
    trace_off
fi

if [ "$mode" = test ]; then
    TEST_MAXDEPTH=1 common_test "$target" .
    if [ -z "$target" ]; then
        python3 tests/csanecheck/test_cli.py bin/csanecheck
        python3 tests/csanecheck/test_rules.py bin/csanecheck
        python3 tests/csanecheck/test_policy.py bin/csanecheck
        python3 tests/csanecheck/test_structure.py bin/csanecheck
        python3 tests/csanecheck/test_control.py bin/csanecheck
        python3 tests/csanecheck/test_context.py bin/csanecheck
        python3 tests/csanecheck/test_layout.py bin/csanecheck
        python3 tests/csanecheck/test_types.py bin/csanecheck
        python3 tests/csanecheck/test_passes.py bin/csanecheck
        python3 tests/csanecheck/test_annotations.py bin/csanecheck
        python3 tests/csanecheck/test_recovery.py bin/csanecheck
        python3 tests/csanecheck/test_macro_expansion.py bin/csanecheck
        python3 tests/csanecheck/test_header_macros.py bin/csanecheck
        python3 tests/csanecheck/test_calls.py bin/csanecheck
        python3 tests/csanecheck/test_wrap.py bin/csanecheck
        python3 tests/csanecheck/audit_rules.py
        python3 tests/csanecheck/test_tokens.py
        python3 tests/csanecheck/compare.py bin/csanecheck
        python3 tests/csanecheck/test_install.py
    fi
fi
