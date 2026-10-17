#!/bin/sh -e

# shellcheck disable=SC2086
# shellcheck disable=SC2034

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

# shellcheck source=./cbase/common.sh
. "./cbase/common.sh"

script=$(basename "$0")
mode="test"
target=
if [ "$#" -gt 0 ]; then
    common_build_parse_args "$@"
fi

case "$mode" in
debug|test)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

common_build_print_invocation "$script"

CC=$(common_get_compiler "$mode")
CPPFLAGS="$CPPFLAGS -I. -Icbase"
CFLAGS="$CFLAGS -std=c11 -Wfatal-errors -g3 -DDEBUGGING=1"
LDFLAGS="$LDFLAGS -lm"

TEST_MAXDEPTH=1 common_test "$target" .
