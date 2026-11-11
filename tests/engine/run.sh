#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -pedantic"
mkdir -p tests/engine/build
"$CC" $CFLAGS -Idsp dsp/string_model.c tests/engine/test_string_model.c -lm \
    -o tests/engine/build/test_string_model
tests/engine/build/test_string_model
"$CC" $CFLAGS -Idsp dsp/string_model.c tests/engine/bench_string_model.c \
    -o tests/engine/build/bench_string_model
tests/engine/build/bench_string_model
