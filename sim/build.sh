#!/usr/bin/env bash
# Builds and runs the desktop simulator. Needs LVGL and ArduinoJson checkouts:
#   LVGL_DIR=path/to/lvgl AJSON_DIR=path/to/ArduinoJson sim/build.sh [out_dir] [--frames N]
set -euo pipefail
cd "$(dirname "$0")/.."
LVGL_DIR=${LVGL_DIR:?set LVGL_DIR to an LVGL 9.6 checkout}
AJSON_DIR=${AJSON_DIR:?set AJSON_DIR to an ArduinoJson 7 checkout}
OUT=${1:-sim-out}
BUILD=${BUILD_DIR:-.sim-build}
mkdir -p "$OUT" "$BUILD/obj"

CFLAGS="-O2 -DGRIDMIX_SIM -DLV_CONF_INCLUDE_SIMPLE -Iinclude -I$LVGL_DIR -Ilib/gridcore -Ilib/gridui -I$AJSON_DIR/src"

# LVGL is big: compile it once and keep the objects
if [ ! -f "$BUILD/liblvgl.a" ]; then
  echo "Compiling LVGL (first run only)..."
  find "$LVGL_DIR/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'o="'"$BUILD"'/obj/$(echo {} | md5sum | cut -c1-16).o"; cc '"$CFLAGS"' -w -c {} -o "$o"'
  ar rcs "$BUILD/liblvgl.a" "$BUILD"/obj/*.o
fi

for f in lib/gridui/fonts/*.c; do cc $CFLAGS -c "$f" -o "$BUILD/$(basename "$f" .c).o"; done
c++ -std=c++17 $CFLAGS -Wall -Wno-unused-function -Wno-deprecated-declarations \
  sim/sim_main.cpp lib/gridui/*.cpp lib/gridcore/*.cpp \
  "$BUILD"/gm_*.o "$BUILD/liblvgl.a" -lm -o "$BUILD/sim"
"$BUILD/sim" "$OUT" "${@:2}"
