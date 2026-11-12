#!/usr/bin/env bash
# Builds the browser simulator with Emscripten:
#   LVGL_DIR=path/to/lvgl AJSON_DIR=path/to/ArduinoJson sim/build_web.sh [out_dir]
# Output: out_dir/index.html, gridmix-sim.js, gridmix-sim.wasm (serve the folder over http to try it).
set -euo pipefail
cd "$(dirname "$0")/.."
LVGL_DIR=${LVGL_DIR:?set LVGL_DIR to an LVGL 9.6 checkout}
AJSON_DIR=${AJSON_DIR:?set AJSON_DIR to an ArduinoJson 7 checkout}
OUT=${1:-sim-web}
BUILD=${BUILD_DIR:-.sim-web-build}
mkdir -p "$OUT" "$BUILD/obj"

CFLAGS="-O2 -DGRIDMIX_SIM -DLV_CONF_INCLUDE_SIMPLE -Iinclude -I$LVGL_DIR -Ilib/gridcore -Ilib/gridui -I$AJSON_DIR/src"

if [ ! -f "$BUILD/liblvgl.a" ]; then
  echo "Compiling LVGL for WebAssembly (first run only)..."
  find "$LVGL_DIR/src" -name '*.c' | xargs -P "$(nproc)" -I{} sh -c 'o="'"$BUILD"'/obj/$(echo {} | md5sum | cut -c1-16).o"; emcc '"$CFLAGS"' -w -c {} -o "$o"'
  emar rcs "$BUILD/liblvgl.a" "$BUILD"/obj/*.o
fi

for f in lib/gridui/fonts/*.c; do emcc $CFLAGS -c "$f" -o "$BUILD/$(basename "$f" .c).o"; done
em++ -std=c++17 $CFLAGS -Wno-deprecated-declarations \
  sim/web_main.cpp lib/gridui/*.cpp lib/gridcore/*.cpp "$BUILD"/gm_*.o "$BUILD/liblvgl.a" \
  -sMODULARIZE=1 -sEXPORT_NAME=GridMixSim -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU16 \
  -o "$OUT/gridmix-sim.js"
# Stamp the file names with the build so browsers fetch the new simulator after every update
BUILD_ID=${BUILD_ID:-$(date +%s)}
sed "s/__BUILD__/$BUILD_ID/g" sim/web/index.html > "$OUT/index.html"
echo "Browser simulator -> $OUT"
