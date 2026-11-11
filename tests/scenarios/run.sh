#!/bin/sh
# The host simulator's scenarios: every script, from a fresh flash each.
#   sh build.sh --host && sh tests/scenarios/run.sh [OUTDIR]
set -eu
cd "$(dirname "$0")/../.."
HOST=app/build/host/fm1-violioliolol_host
OUT="${1:-tests/scenarios/out}"
[ -x "$HOST" ] || { echo "no host simulator: sh build.sh --host" >&2; exit 1; }
mkdir -p "$OUT"
fail=0
for s in tests/scenarios/*.txt; do
    name=$(basename "$s" .txt)
    mkdir -p "$OUT/$name"
    if "$HOST" "$s" "$OUT/$name" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name"
        grep -E 'FAIL|fail' "$OUT/$name.log" | head -20
        fail=1
    fi
done
[ "$fail" = 0 ] && echo "scenarios: PASS"
exit "$fail"
