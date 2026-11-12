#!/bin/sh
# Native DSP tests and the host throughput harness: plain cc, no toolchain.
#   tests/engine/run.sh          every test, then the benchmark
#   tests/engine/run.sh tests    the tests only
# The flags are the device's discipline: C99, no implicit double anywhere.
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
CFLAGS="-std=c99 -O2 -ffp-contract=off -Wall -Wextra -Werror -Wdouble-promotion -pedantic"
OUT=tests/engine/build
mkdir -p "$OUT"
DSP="dsp/string_model.c dsp/bowed_model.c dsp/body_model.c dsp/instrument.c dsp/engine.c"

# the DSP must not lean on a C library: nothing but <stdint.h> in dsp/
if grep -n '#include <' dsp/*.c dsp/*.h | grep -v '<stdint.h>'; then
    echo "FAIL: dsp/ may include <stdint.h> only" >&2
    exit 1
fi
# and no double precision: every literal with a decimal point or exponent ends in f
if grep -nE '(^|[^A-Za-z0-9_.])[0-9]+\.[0-9]*([eE][-+]?[0-9]+)?([^0-9fFeE.]|$)' dsp/*.c dsp/*.h |
   grep -vE '^[^:]+:[0-9]+:\s*(/\*|\*|//)' | grep -vE '/\*.*[0-9]\.[0-9].*\*/\s*$' ; then
    echo "FAIL: a floating literal without the f suffix in dsp/" >&2
    exit 1
fi

"$CC" $CFLAGS -Idsp dsp/string_model.c tests/engine/test_string_model.c -lm -o "$OUT/test_string_model"
"$CC" $CFLAGS -Idsp dsp/bowed_model.c tests/engine/test_bowed_model.c -lm -o "$OUT/test_bowed_model"
"$CC" $CFLAGS -Idsp dsp/body_model.c tests/engine/test_body_output.c -lm -o "$OUT/test_body_output"
"$CC" $CFLAGS -Idsp dsp/instrument.c tests/engine/test_instrument.c -lm -o "$OUT/test_instrument"
"$CC" $CFLAGS -Idsp $DSP tests/engine/test_engine.c -lm -o "$OUT/test_engine"
"$OUT/test_string_model"
"$OUT/test_bowed_model"
"$OUT/test_body_output"
"$OUT/test_instrument"
"$OUT/test_engine"
[ "${1:-}" = tests ] && exit 0
"$CC" $CFLAGS -Idsp $DSP tests/engine/bench_engine.c -o "$OUT/bench_engine"
"$OUT/bench_engine"
