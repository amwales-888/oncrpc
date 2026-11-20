#!/bin/sh
# check_exports.sh - does the shared library export exactly the public API?
#
#   sh tests/check_exports.sh elf   build/linux/release/liboncrpc.so   [nm]
#   sh tests/check_exports.sh macho build/macos/release/liboncrpc.dylib [nm]
#
# The public API is every function declared with ONCRPC_API in src/xdr.h,
# src/oncrpc.h and src/xspec.h. Each must be exported (or programs using
# the shared library will not link), and nothing else may be (the library's
# internal functions are hidden with -fvisibility=hidden).
set -eu

KIND=$1
LIB=$2
NM=${3:-nm}
SRC=$(cd "$(dirname "$0")/../src" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# The first oncRPC... name followed by "(" on each ONCRPC_API line.
cat "$SRC/xdr.h" "$SRC/oncrpc.h" "$SRC/xspec.h" |
    awk '/ONCRPC_API/ && match($0, /oncRPC[A-Za-z0-9_]*[ \t]*\(/) {
             name = substr($0, RSTART, RLENGTH); sub(/[ \t]*\($/, "", name); print name }' |
    sort -u > "$WORK/declared"

case $KIND in
elf)
    # Defined symbols in the dynamic symbol table, less the linker's own.
    "$NM" -D --defined-only "$LIB" | awk 'NF == 3 { print $3 }' |
        grep -v -x -e _init -e _fini -e _edata -e _end -e __bss_start |
        sort -u > "$WORK/exported" ;;
macho)
    "$NM" -gU "$LIB" | awk 'NF == 3 { print $3 }' | sed 's/^_//' |
        sort -u > "$WORK/exported" ;;
*)
    echo "check_exports.sh: unknown kind $KIND" >&2
    exit 2 ;;
esac

missing=$(comm -23 "$WORK/declared" "$WORK/exported")
extra=$(comm -13 "$WORK/declared" "$WORK/exported")
if [ -n "$missing" ] || [ -n "$extra" ]; then
    [ -z "$missing" ] || printf 'not exported but declared public:\n%s\n' "$missing"
    [ -z "$extra" ] || printf 'exported but not public:\n%s\n' "$extra"
    echo "check-exports: FAILED"
    exit 1
fi
echo "check-exports: $(wc -l < "$WORK/declared" | tr -d ' ') public functions exported, nothing else"
