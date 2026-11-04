#!/bin/bash
# Called by rez-build from packaging/rez with the build directory as the working directory.
set -euo pipefail
source_dir="$REZ_BUILD_SOURCE_PATH/../.."
python="${SCULPTFORGE_PYTHON:-$(command -v python3 || true)}"
python_args=(-DSCULPTFORGE_BUILD_PYTHON=OFF)
if [ -n "$python" ]; then
  python_args=(-DSCULPTFORGE_BUILD_PYTHON=ON "-DPython_EXECUTABLE=$python" -DSCULPTFORGE_PYTHON_INSTALL_DIR=python)
fi
cmake -S "$source_dir" -B . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$REZ_BUILD_INSTALL_PATH" \
  -DCMAKE_INSTALL_BINDIR=bin -DCMAKE_INSTALL_LIBDIR=lib -DSCULPTFORGE_MOLD=OFF "${python_args[@]}"
cmake --build . --parallel "${REZ_BUILD_THREAD_COUNT:-4}"
ctest --output-on-failure
if [ "${1:-}" = "install" ]; then
  cmake --install .
fi
