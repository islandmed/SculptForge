#!/bin/bash
# R&D spike rnd/mega-poly: builds the standalone GL 3.3 harness (no product code, no CMake).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build-megapoly
g++ -O2 -std=c++20 -Wall -Wno-unused-function tests/tools/rnd_megapoly_bench.cpp src/render/GL.cpp -Isrc $(pkg-config --cflags --libs sdl3) -lpthread -o build-megapoly/sf_rnd_megapoly_bench
echo built build-megapoly/sf_rnd_megapoly_bench
