#!/bin/sh
# Runs every example: starts each server on a test port, runs its client,
# stops it with SIGTERM (background jobs of a script ignore SIGINT).
# 06_portmap runs only if a port mapper (rpcbind) answers; 09_local uses a
# socket in $TMPDIR (default /tmp).
# Usage: run_examples.sh BUILD_EXAMPLES_DIR (the makefiles' "check" passes it)
DIR=${1:-build/linux/release/examples}
BASE=${ONCRPC_TEST_PORT:-39123}
FAILED=0
SERVER=

stop_server() {
    if [ -n "$SERVER" ]; then
        kill -TERM "$SERVER" 2>/dev/null
        wait "$SERVER" 2>/dev/null
        SERVER=
    fi
}
trap stop_server EXIT INT TERM

run_pair() {   # name port
    name=$1
    port=$2
    echo "== $name"
    "$DIR/$name/server" "$port" > "$DIR/$name/server.log" 2>&1 &
    SERVER=$!
    sleep 0.5
    if "$DIR/$name/client" 127.0.0.1 "$port"; then
        echo "-- $name: ok"
    else
        echo "-- $name: FAILED (server log follows)"
        cat "$DIR/$name/server.log"
        FAILED=1
    fi
    stop_server
}

run_pair 01_add     $((BASE + 1))
run_pair 02_strings $((BASE + 2))
run_pair 03_shapes  $((BASE + 3))
run_pair 04_lists   $((BASE + 4))
run_pair 05_async   $((BASE + 5))
run_pair 07_errors  $((BASE + 7))
run_pair 10_tls     $((BASE + 10))   # certificates from examples/10_tls/certs

echo "== 06_portmap"
if command -v rpcinfo > /dev/null 2>&1 && rpcinfo -p 127.0.0.1 > /dev/null 2>&1; then
    "$DIR/06_portmap/server" > "$DIR/06_portmap/server.log" 2>&1 &
    SERVER=$!
    sleep 1
    if "$DIR/06_portmap/client"; then
        echo "-- 06_portmap: ok"
    else
        echo "-- 06_portmap: FAILED (server log follows)"
        cat "$DIR/06_portmap/server.log"
        FAILED=1
    fi
    stop_server
    if rpcinfo -p 127.0.0.1 | grep -q 536871174; then
        echo "-- 06_portmap: FAILED: registration left behind"
        FAILED=1
    fi
else
    echo "-- 06_portmap: skipped (no port mapper running)"
fi

echo "== 09_local"
SOCK="${TMPDIR:-/tmp}/oncrpc-ex-$((BASE + 9)).sock"
rm -f "$SOCK"
"$DIR/09_local/server" "$SOCK" > "$DIR/09_local/server.log" 2>&1 &
SERVER=$!
sleep 0.5
if "$DIR/09_local/client" "$SOCK"; then
    echo "-- 09_local: ok"
else
    echo "-- 09_local: FAILED (server log follows)"
    cat "$DIR/09_local/server.log"
    FAILED=1
fi
stop_server
if [ -e "$SOCK" ]; then
    echo "-- 09_local: FAILED: socket file left behind"
    FAILED=1
fi

echo "== 08_xdr"
if "$DIR/08_xdr/xdr_demo"; then
    echo "-- 08_xdr: ok"
else
    echo "-- 08_xdr: FAILED"
    FAILED=1
fi

if [ $FAILED -ne 0 ]; then
    echo "== some examples FAILED"
    exit 1
fi
echo "== all examples passed"
