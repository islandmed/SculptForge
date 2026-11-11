#!/usr/bin/env bash
# T0 on GitHub Actions (.github/workflows/t0.yml, docs/testing-flow.md): the same checks as ~/Work/.sf/t0.sh on the PC:
# check_marks, then a build and only the tests this branch's changes can break (tools/test/impacted.py, against its
# merge base with origin/main), CPU only (Mesa llvmpipe, SDL offscreen).
#   tools/ci/t0.sh [out dir, default t0-result]
#   tools/ci/t0.sh --warm       the build only (t0.yml on main: saves the compiler cache every T0 starts from)
# Never fails: the verdict goes to <out>/result.json ({"result": "green"|"red", "stage", "failed", "regex", ...}),
# <out>/summary.md (the run summary) and <out>/output.txt (ctest --output-on-failure on the failed tests), so a red T0
# sends nobody a failure email. ~/Work/.sf/t0-ci.sh pushes a branch to t0/<branch> and reads result.json back.
warm=""; [ "$1" = --warm ] && { warm=1; shift; }
out=${1:-t0-result}
mkdir -p "$out"
start=$(date +%s); build_s=0; test_s=0; stage=marks; regex=""; failed=""; result=red; base=""; tier=ready
finish() {
  python3 - "$out" "$result" "$stage" "$regex" "$build_s" "$test_s" "$(($(date +%s) - start))" "$base" "$failed" "$tier" <<'EOF'
import json, os, sys
out, result, stage, regex, b, t, total, base, failed, tier = sys.argv[1:]
r = {"result": result, "stage": stage, "sha": os.environ.get("GITHUB_SHA", ""), "base": base, "regex": regex,
     "failed": failed.split(), "build_s": int(b), "test_s": int(t), "total_s": int(total), "tier": tier}
json.dump(r, open(f"{out}/result.json", "w"), indent=1)
# (a push-tier green is the per-push smoke, never the READY verdict: its title says so)
lines = [f"## T0 {result}" + ("" if result == "green" else f" ({stage})") + (" (push tier: not a READY verdict)" if tier == "push" else ""), "",
         f"- commit `{r['sha'][:8]}`, base `{base[:8]}`; build {b} s, tests {t} s, {total} s in all",
         f"- tests: `{regex or 'none'}`"]
if r["failed"]:
    lines += ["- failed: " + " ".join(f"`{n}`" for n in r["failed"])]
for name, title in (("near.txt", "near the budget: split, speed up or list in tests/slow_tests.txt before a stage fails it"),
                    ("failed.txt", "LastTestsFailed + FAILED lines"), ("output.txt", "output on failure (last 300 lines)")):
    p = f"{out}/{name}"
    if os.path.exists(p) and os.path.getsize(p):
        lines += ["", f"### {title}", "", "```", open(p, errors="replace").read()[-60000:].rstrip(), "```"]
open(f"{out}/summary.md", "w").write("\n".join(lines) + "\n")
EOF
  echo "T0 $result${failed:+: $failed}$([ "$tier" = push ] && echo ' (push tier: not a READY verdict)')"
  exit 0
}

# Marks first (as t0.sh): another app's name in a comment is red before anything is built.
if [ -z "$warm" ] && ! python3 -I tools/check_marks.py > "$out/output.txt" 2>&1; then failed=check_marks; finish; fi
: > "$out/output.txt"

base=$(git merge-base origin/main HEAD 2>/dev/null || echo origin/main)
# t0-ci.sh adds t0.yml to a branch based before it in one commit "t0-ci overlay: ...": tests come from the branch's own files.
tip=HEAD; git log -1 --format=%s | grep -q '^t0-ci overlay' && tip=HEAD^
stage=configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSCULPTFORGE_USE_SYSTEM_SDL=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DSCULPTFORGE_BUILD_PYTHON=ON ${pythonLocation:+-DPython_EXECUTABLE="$pythonLocation/bin/python3"} > "$out/configure.log" 2>&1 ||
  { tail -60 "$out/configure.log" > "$out/output.txt"; finish; }

# The whole build (a fresh build folder has no .ninja_deps, so headers can't map to tests before it; ccache makes the
# rest cheap), then the impacted tests.
stage=build
t=$(date +%s)
ccache -z >/dev/null 2>&1
cmake --build build --parallel 4 > "$out/build.log" 2>&1; rc=$?
build_s=$(($(date +%s) - t))
ccache -s 2>/dev/null | tail -12
[ -n "$warm" ] && { echo "warm build: exit $rc, $build_s s"; exit 0; }
if [ $rc -ne 0 ]; then grep -E -A3 'error|FAILED' "$out/build.log" | head -150 > "$out/output.txt"; finish; fi

stage=test
# Tier (docs/testing-flow.md "Tiers"): a t0/push/<branch> ref (t0-ci.sh with SF_T0_TIER=push) or SF_T0_TIER=push is the per-push
# smoke: impacted.py's direct picks and PUSH_SMOKE. Any other ref is the READY T0 (smoke, areas, mode matrix, monkey).
case "${GITHUB_REF_NAME:-}" in t0/push/*) tier=push ;; esac; [ "${SF_T0_TIER:-}" = push ] && tier=push
tierarg=(); grep -q -- '--tier' tools/test/impacted.py && tierarg=(--tier "$tier")
mapfile -t files < <(git diff --name-only "$base" "$tip")
git diff -U0 "$base" "$tip" -- CMakeLists.txt > "$out/cmake.diff" # (test-only CMake edits run just those tests)
regex=""; [ ${#files[@]} -gt 0 ] && regex=$(python3 tools/test/impacted.py --build build --base "$base" -v --cmake-diff "$out/cmake.diff" "${tierarg[@]}" \
  --files "${files[@]}" 2> "$out/impacted.log")
# Areas (tests/areas.map, docs/testing-flow.md): smoke + the changed files' areas join impacted.py's picks, and the
# sculptforge_tests shards run only those areas' cases (SF_TEST_AREA). A core header, CMake or an unknown path: everything.
# A branch from before areas (no affected.py) keeps impacted.py's list alone.
if [ ${#files[@]} -gt 0 ] && [ -f tools/test/affected.py ] && [ -f tests/areas.map ]; then
  plan=$(python3 tools/test/affected.py --build build --cmake-diff "$out/cmake.diff" "${tierarg[@]}" --files "${files[@]}" --merge "$regex" --json -v 2>> "$out/impacted.log") &&
    { regex=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["regex"])' <<< "$plan")
      export SF_TEST_AREA=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["unit_areas"])' <<< "$plan"); }
fi
echo "T0 ($tier): ${#files[@]} changed file(s) -> ${regex:-no tests}${SF_TEST_AREA:+ (unit cases of: $SF_TEST_AREA)}"
if [ -z "$regex" ]; then result=green; finish; fi
# "[t0 full]" in the branch tip's subject runs every test (proof that a full run completes here).
git log -1 --format=%s "$tip" | grep -qF '[t0 full]' && { regex=.; echo "T0: [t0 full] in the commit subject: every test"; }
args=(-j 4); [ "$regex" != . ] && args+=(-R "$regex")
args+=(-E '^marks$') # (passed above, on this tree: ~1 min of a runner's four slots, 11-11 qa-lean)
t=$(date +%s)
# The tests run in the background and the shell waits, so a SIGTERM reaches this trap at once. A full run once died here (exit
# 143, no artifact): the warm UI servers had grown past the runner's 16 GB and the kernel's OOM kill took the job with it
# (tools/test/warm_ui.py MAX_MB). The trap and the memory line below say why, should a runner ever do that again.
trap 'echo "[t0] SIGTERM $(date +%T)"; free -m; ps -eo pid,ppid,rss,etime,args --sort=-rss | cut -c1-150 | head -8; sudo dmesg 2>/dev/null | grep -i -E "out of memory|oom" | tail -3; tail -4 "$out/ctest.log"; exit 143' TERM
( while sleep 60; do echo "[t0 $(date +%T)] mem used $(free -m | awk 'NR==2{print $3 " avail " $7}') MB, $(grep -c ' Test  *#' "$out/ctest.log" 2>/dev/null) tests done"; done ) &
tick=$!
SDL_VIDEO_DRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tools/test/run.py --build build --junit build/ctest.xml $(grep -q -- --near-budget tools/test/run.py && echo --near-budget 20) -- "${args[@]}" > "$out/ctest.log" 2>&1 & wait $!; rc=$?
kill $tick 2>/dev/null
pkill -f 'build/sculptforge --ui-serve' 2>/dev/null # (a pool the fixture cleanup missed)
test_s=$(($(date +%s) - t))
tail -40 "$out/ctest.log"
sed -n 's/^near budget: //p' "$out/ctest.log" > "$out/near.txt"
if [ $rc -eq 0 ] && grep -q '^== [0-9]* run' "$out/ctest.log"; then result=green; finish; fi
# Red: what failed (ctest's list from the first run, run.py's FAILED / over-budget lines), then their output.
{ echo "LastTestsFailed.log:"; cat build/Testing/Temporary/LastTestsFailed.log 2>/dev/null
  grep -E '^(FAILED|over budget|GPU lost): ' "$out/ctest.log"; } > "$out/failed.txt"
failed=$( { sed -n 's/^FAILED: \([^ ]*\)$/\1/p; s/^over budget: \([^ ]*\).*/\1/p' "$out/ctest.log"; } | sort -u | tr '\n' ' ')
[ -z "$failed" ] && failed=$(sed -n 's/^[0-9]*://p' build/Testing/Temporary/LastTestsFailed.log 2>/dev/null | sort -u | tr '\n' ' ')
if [ -n "$failed" ]; then
  SDL_VIDEO_DRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe timeout 900 \
    ctest --test-dir build --output-on-failure -R "^($(echo $failed | tr ' ' '|'))\$" 2>&1 | grep -vF '[ OK ]' | tail -300 > "$out/output.txt"
else
  tail -300 "$out/ctest.log" > "$out/output.txt"
fi
finish
