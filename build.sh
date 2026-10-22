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
build|callgrind|check|debug|debug-fast|fast_feedback|install|test)
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
CFLAGS="$CFLAGS -std=c11"
LDFLAGS="$LDFLAGS -lm"

case "$mode" in
callgrind)
    CFLAGS="$CFLAGS -g3 -O2"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=0"
    ;;
check)
    (
        common_build_run_analyzers fast_feedback
    )
    echo "static analysis finished."
    exit
    ;;
debug|test)
    CFLAGS="$CFLAGS -g3"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -g2 -O2 -flto"
    CFLAGS="$CFLAGS -fsanitize-trap=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
build)
    if common_command_exists zig; then
        CC="zig cc"
        CFLAGS="$CFLAGS -target x86_64-linux-musl"
        LDFLAGS="$LDFLAGS -static"
    fi
    CFLAGS="$CFLAGS -g -O2 -flto"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=0"
    ;;
install)
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

case "$mode" in
build|callgrind|debug|fast_feedback|debug-fast)
    trace_on
    $CC $CPPFLAGS $CFLAGS -o bin/csanecheck csanecheck/main.c $LDFLAGS
    cp -f bin/csanecheck cbase/csanecheck
    trace_off
    ;;
*)
    ;;
esac

if [ "$mode" = callgrind ]; then
    if ! common_command_exists valgrind; then
        error "valgrind is required for callgrind mode.\n"
        exit 1
    fi

    callgrind_out=${CALLGRIND_OUT:-csanecheck.callgrind}
    rm -f "$callgrind_out" "${callgrind_out}.txt"

    trace_on
    set +e
    valgrind --tool=callgrind --smc-check=none \
        --callgrind-out-file="$callgrind_out" \
        --dump-instr=yes \
        $CALLGRIND_FLAGS \
        bin/csanecheck $CALLGRIND_ARGS
    csanecheck_status=$?
    set -e
    if [ "$csanecheck_status" -gt 1 ]; then
        exit "$csanecheck_status"
    fi
    python3 cbase/callgrind-parse.py "$callgrind_out" \
        > "${callgrind_out}.txt"
    trace_off
    echo "$callgrind_out"
    exit
fi

if [ "$mode" = install ]; then
    trace_on
    if [ ! -x bin/csanecheck ]; then
        ./build.sh build
    fi
    install -Dm755 bin/csanecheck "${DESTDIR}${PREFIX}/bin/csanecheck"
    cp -f bin/csanecheck cbase/csanecheck
    trace_off
fi

if [ "$mode" = test ]; then
    TEST_MAXDEPTH=1 common_test "$target" .
    if [ -z "$target" ] || [ "$target" = csanecheck ]; then
        python3 -m unittest discover -s csanecheck/tests -p 'test_*.py' -v
    fi
fi
