#!/bin/sh
# Build and test fm1-violioliolol using the pinned fm1-base submodule.
set -eu
cd "$(dirname "$0")"
case "${1:-}" in
  test)
    exec sh tests/engine/run.sh
    ;;
esac
if [ ! -f base/tools/build.py ]; then
  git submodule update --init --recursive
fi
exec python3 base/tools/build.py app "$@"
