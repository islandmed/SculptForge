#!/usr/bin/env bash
# ThreadSanitizer smoke: builds sculptforge with -fsanitize=thread in build-tsan (RelWithDebInfo) and runs
# tests/tsan/*.sfui offscreen; fails on any report left after tests/tsan/tsan.supp. Slow (a full TSan build, then
# the app at a fraction of its speed): run it by hand or nightly, through ~/Work/.sf/heavy.sh on the laptop.
#   tools/test/tsan.sh [build dir, default build-tsan]
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:-$root/build-tsan}
flags="-fsanitize=thread -fno-omit-frame-pointer"
[ -f "$build/CMakeCache.txt" ] || cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread >/dev/null
cmake --build "$build" -j"${JOBS:-3}" --target sculptforge >/dev/null
logs=$(mktemp -d)
status=0
for script in "$root"/tests/tsan/*.sfui; do
  name=$(basename "$script" .sfui)
  TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1 suppressions=$root/tests/tsan/tsan.supp log_path=$logs/$name" \
    SDL_VIDEO_DRIVER=offscreen "$build/sculptforge" --ui-test "$script" --ui-out "$logs/$name-shots" >/dev/null 2>&1 || {
      echo "tsan: $name: the script failed"; status=1; }
  if reports=$(cat "$logs/$name".* 2>/dev/null | grep -c "WARNING: ThreadSanitizer"); [ "$reports" -gt 0 ]; then
    echo "tsan: $name: $reports report(s):"
    grep -h -A3 "WARNING: ThreadSanitizer" "$logs/$name".* | head -40
    status=1
  else
    echo "tsan: $name: clean"
  fi
done
[ $status -eq 0 ] && rm -rf "$logs" || echo "tsan: logs in $logs"
exit $status
