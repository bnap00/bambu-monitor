#!/bin/sh
# Host UI tools (needs `pio run` once for the LVGL sources):
#   ./sim     renders every screen to out/*.ppm (python3 sheets.py -> PNGs)
#   ./stress  hammers the knob with random timing under ASan/UBSan
set -e
cd "$(dirname "$0")"
ROOT=../..
LV=$ROOT/.pio/libdeps/t-encoder-pro/lvgl
INC="-Istubs -I$ROOT/include -I$ROOT/src -I$LV -I$LV/.. -DLV_CONF_INCLUDE_SIMPLE"
mkdir -p obj out
if [ ! -f obj/liblvgl.a ]; then
  for f in $(find $LV/src -name '*.c'); do
    o=obj/lv_$(echo ${f#$LV/} | sed 's|/|_|g').o
    gcc -O1 -c $INC $f -o $o
  done
  ar rcs obj/liblvgl.a obj/*.o
fi
g++ -std=gnu++17 -O1 -g $INC -DFW_VERSION='"sim"' sim_main.cpp $ROOT/src/ui/ui.cpp obj/liblvgl.a -o sim
# fast-knob regression test (LVGL screen-load crash), built with sanitizers
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined $INC -DFW_VERSION='"sim"' stress.cpp $ROOT/src/ui/ui.cpp obj/liblvgl.a -o stress
