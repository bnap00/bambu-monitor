#!/bin/sh
# Builds and runs the parser unit test on the host (needs `pio run` once for ArduinoJson).
set -e
cd "$(dirname "$0")"
ROOT=../..
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined \
  -I../sim/stubs -I$ROOT/src -I$ROOT/include -I$ROOT/.pio/libdeps/t-encoder-pro/ArduinoJson/src \
  test_parse.cpp $ROOT/src/printer/bambu_parse.cpp -o test_parse
./test_parse
