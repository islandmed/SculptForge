#!/usr/bin/env bash
# Edit-one-file -> rebuilt binaries: how long the edit-build loop takes for the common cases (docs/build-speed.md).
# tools/dev/edit_timing.sh BUILD_DIR [JOBS]   (BUILD_DIR configured and built already; run from the source tree)
# Each case changes one file's content (a comment line, so ccache misses like a real edit), rebuilds sculptforge,
# sculptforge_tests and sculptforge-cli, and prints the wall time plus the compile and link seconds from .ninja_log;
# then puts the file back and rebuilds (a ccache hit). With Make instead of Ninja only the wall time is printed.
set -euo pipefail
build=$1; jobs=${2:-$(nproc)}
targets=(sculptforge_tests sculptforge-cli)
grep -q '^SCULPTFORGE_BUILD_APP:BOOL=ON' "$build/CMakeCache.txt" && targets+=(sculptforge)
log=$build/.ninja_log; [ -f "$log" ] || log=/dev/null
cases=(
  "core_cpp src/core/Sculpt.cpp"
  "header src/core/Mesh.h"
  "app_cpp src/app/PanelBrushes.cpp"
  "app_shared_cpp src/app/AssetLibrary.cpp"
  "test_cpp tests/test_brushes.cpp"
)
cmake --build "$build" -j"$jobs" --target "${targets[@]}" >/dev/null 2>&1
printf '%-15s %8s %6s %9s %8s  %s\n' case wall_s units compile_s link_s links
for c in "${cases[@]}"; do
  set -- $c; name=$1; file=$2
  cp "$file" "$build/.edit_timing.bak"
  printf '\n// edit_timing %s\n' "$(date +%s%N)" >> "$file"
  lines=$(wc -l < "$log")
  t0=$(date +%s.%N)
  cmake --build "$build" -j"$jobs" --target "${targets[@]}" >/dev/null 2>&1 || { cp "$build/.edit_timing.bak" "$file"; exit 1; }
  t1=$(date +%s.%N)
  cp "$build/.edit_timing.bak" "$file"
  # .ninja_log v5: start_ms end_ms mtime output hash; the new lines are this build's steps.
  tail -n +"$((lines + 1))" "$log" | awk -v wall="$(awk "BEGIN{print $t1 - $t0}")" -v name="$name" '
    { d = ($2 - $1) / 1000; if ($4 ~ /\.(o|obj)$/) { n++; c += d } else if ($4 ~ /\.(a|lib)$/) { a += d } else { l += d; links = links sprintf("%s=%.1f ", $4, d) } }
    END { printf "%-15s %8.1f %6d %9.1f %8.1f  %s\n", name, wall, n, c, l + a, links }'
  cmake --build "$build" -j"$jobs" --target "${targets[@]}" >/dev/null 2>&1
done
