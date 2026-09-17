#!/bin/sh
# Builds and runs the display-list replay probe.
#
# A shell script rather than a CMake target on purpose: upstream has no test
# infrastructure, and adding one would put this branch in conflict with every
# future merge for the sake of a single probe. This needs no ROM and no GPU.
#
#   port/tests/run_dl_replay.sh [build-dir]
#
# The build dir defaults to ./build and only has to have been CONFIGURED --
# the probe needs the generated port/include headers, not a built game.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build}
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

if [ ! -d "$BUILD/port/include" ]; then
    echo "configure first, e.g.:  cmake -S . -B build" >&2
    echo "(looked for $BUILD/port/include)" >&2
    exit 1
fi

INC="-I$ROOT/port/shim -I$ROOT -I$ROOT/include -I$ROOT/include/PR \
     -I$ROOT/src -I$ROOT/src/game -I$ROOT/src/libultra -I$ROOT/src/libultra/audio \
     -I$ROOT/port/include -I$ROOT/port/fast3d -I$BUILD/port/include"
DEFS="-DPORT -DVERSION_US -DLANG_US -D_LANGUAGE_C"
SDL=$(sdl2-config --cflags 2>/dev/null || true)

g++ -std=gnu++20 -O1 -c "$ROOT/port/fast3d/gfx_pc.cpp"        -o "$OUT/gfx_pc.o" $INC $SDL $DEFS
g++ -std=gnu++20 -O1 -c "$ROOT/port/tests/test_dl_replay.cpp" -o "$OUT/probe.o"  $INC $SDL $DEFS
g++ -std=gnu++20 -o "$OUT/probe" "$OUT/probe.o" "$OUT/gfx_pc.o" -lm

"$OUT/probe"
