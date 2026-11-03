#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Precision at extreme zoom on 33M triangles (tests/perf/precision_33m.sfui): too heavy for CI, run by hand before a
# release that touches brushes or the camera. On a HiDPI screen it also checks the 2x framebuffer.
#   tools/precision_check.sh [BUILD_DIR]
set -euo pipefail
build=${1:-build}
here=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
nice -n 19 ionice -c3 "$build/sculptforge" --ui-test "$here/tests/perf/precision_33m.sfui" --ui-out "$out"
