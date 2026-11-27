#!/usr/bin/env bash
# T0 on GitHub Actions (.github/workflows/t0.yml, docs/testing-flow.md): the same checks as ~/Work/.sf/t0.sh on the PC:
# check_marks, then a build and only the tests this branch's changes can break (tools/test/impacted.py, against its
# merge base with origin/main), CPU only (Mesa llvmpipe, SDL offscreen). t0.yml runs it as jobs: one build, N test jobs
# (the tests split by measured duration, tools/ci/shard.py), one verdict. Each phase is one mode of this script:
#   tools/ci/t0.sh --warm                 the build only (t0.yml on main: saves the compiler cache every T0 starts from)
#   tools/ci/t0.sh --build [DIR]          marks, gallery gate, configure, build, test selection, shard plan -> DIR/plan.json
#                                         (DIR/result.json too when the run ended here: red, or green with no tests)
#   tools/ci/t0.sh --iwyu PLAN_DIR [DIR]  iwyu_lite on the changed files -> DIR/iwyu.json
#   tools/ci/t0.sh --test K PLAN_DIR [DIR] shard K of the plan -> DIR/shard.json (+ ctest.log, times.tsv, failed.txt, output.txt)
#   tools/ci/t0.sh --verdict DIR PLAN_DIR RESULT_DIR...   merges them into DIR/result.json + summary.md (tools/ci/verdict.py)
#   tools/ci/t0.sh [DIR]                  all of it on this machine, one test shard (default DIR t0-result)
# Never fails: the verdict goes to <out>/result.json ({"result": "green"|"red", "stage", "failed", "regex", ...}),
# <out>/summary.md (the run summary) and <out>/output.txt (ctest --output-on-failure on the failed tests), so a red T0
# sends nobody a failure email. ~/Work/.sf/t0-ci.sh pushes a branch to t0/<branch> and reads result.json back.
mode=all; k=0
case "$1" in
  --warm) mode=warm; shift ;;
  --build) mode=build; shift ;;
  --iwyu) mode=iwyu; shift ;;
  --test) mode=test; k=$2; shift 2 ;;
  --verdict) shift; mkdir -p "$1"; exec python3 -I tools/ci/verdict.py merge "$@" ;;
esac
SELF=$0
if [ $mode = all ]; then
  out=${1:-t0-result}; export SF_T0_MAX_SHARDS=1
  bash "$SELF" --build "$out/build"
  j() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1] + "/plan.json")).get(sys.argv[2], ""))' "$out/build" "$1"; }
  dirs=()
  if [ "$(j early)" != True ]; then
    [ "$(j iwyu)" = True ] && { bash "$SELF" --iwyu "$out/build" "$out/iwyu"; dirs+=("$out/iwyu"); }
    [ "$(j n)" != 0 ] && { bash "$SELF" --test 0 "$out/build" "$out/shard0"; dirs+=("$out/shard0"); }
  fi
  exec bash "$SELF" --verdict "$out" "$out/build" "${dirs[@]}"
fi
if [ $mode = iwyu ] || [ $mode = test ]; then plan=$1; out=${2:-t0-$mode}; else out=${1:-t0-build}; fi
mkdir -p "$out"
start=$(date +%s); build_s=0; test_s=0; stage=marks; regex=""; failed=""; result=red; base=""; tier=ready

# The build phase ended the run (red, or green with nothing to test): result.json + summary.md, and a plan.json saying so.
finish() {
  python3 -I tools/ci/verdict.py finish "$out" "$result" "$stage" "$regex" "$build_s" "$test_s" "$(($(date +%s) - start))" "$base" "$failed" "$tier"
  python3 -I -c 'import json,sys; json.dump({"early": True, "started": float(sys.argv[2]), "result": sys.argv[1]}, open(sys.argv[3] + "/plan.json", "w"))' \
    "$result" "$start" "$out"
  [ -n "$GITHUB_OUTPUT" ] && printf 'shards=[]\niwyu=false\nearly=true\n' >> "$GITHUB_OUTPUT"
  echo "T0 $result${failed:+: $failed}$([ "$tier" = push ] && echo ' (push tier: not a READY verdict)')"
  exit 0
}

# ---- build job -------------------------------------------------------------------------------------------------
if [ $mode = build ] || [ $mode = warm ]; then
  # Marks first (as t0.sh): another app's name in a comment is red before anything is built.
  if [ $mode = build ] && ! python3 -I tools/check_marks.py > "$out/output.txt" 2>&1; then failed=check_marks; finish; fi
  : > "$out/output.txt"

  base=$(git merge-base origin/main HEAD 2>/dev/null || echo origin/main)
  # t0-ci.sh adds t0.yml to a branch based before it in one commit "t0-ci overlay: ...": tests come from the branch's own files.
  tip=HEAD; git log -1 --format=%s | grep -q '^t0-ci overlay' && tip=HEAD^
  # The public gallery repository must already carry a change to tools/brush-gallery/repo (tools/ci/gallery_gate.sh): seconds, before the build.
  stage=gallery
  if [ $mode = build ] && ! bash tools/ci/gallery_gate.sh "$base" "$tip" > "$out/output.txt" 2>&1; then failed=gallery_published; finish; fi
  cat "$out/output.txt"; : > "$out/output.txt"
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
  # Hits and misses of this build (the cache a release T0 saves only when it is worth saving: t0.yml, "ccache_save").
  read -r hits misses < <(ccache --print-stats 2>/dev/null | awk -F'\t' '$1=="direct_cache_hit"||$1=="preprocessed_cache_hit"{h+=$2} $1=="cache_miss"{m+=$2} END{print h+0, m+0}')
  echo "T0: compiler cache ${hits:-0} hits, ${misses:-0} misses"
  [ -n "$GITHUB_OUTPUT" ] && echo "ccache_save=$([ "$(( ${misses:-0} * 10 ))" -gt "$(( ${hits:-0} + ${misses:-0} ))" ] && [ "${misses:-0}" -ge 20 ] && echo true || echo false)" >> "$GITHUB_OUTPUT"
  [ $mode = warm ] && { echo "warm build: exit $rc, $build_s s"; exit 0; }
  if [ $rc -ne 0 ]; then grep -E -A3 'error|FAILED' "$out/build.log" | head -150 > "$out/output.txt"; finish; fi

  stage=test
  # Tier (docs/testing-flow.md "Tiers"): a t0/push/<branch> ref (t0-ci.sh with SF_T0_TIER=push) or SF_T0_TIER=push is the per-push
  # smoke: impacted.py's direct picks and PUSH_SMOKE. Any other ref is the READY T0 (smoke, areas, mode matrix, monkey).
  case "${GITHUB_REF_NAME:-}" in t0/push/*) tier=push ;; esac; [ "${SF_T0_TIER:-}" = push ] && tier=push
  tierarg=(); grep -q -- '--tier' tools/test/impacted.py && tierarg=(--tier "$tier")
  mapfile -t files < <(git diff --name-only "$base" "$tip")
  # Without the precompiled standard headers, on the packages' standard library (tools/ci/iwyu_lite.py): the build has the
  # PCH, which hides a missing #include <cstring> that breaks only the Ubuntu 22.04 packages (0.13.11 stage). Only the changed
  # C++ files and the .cpp files that include a changed header; seconds. It runs as its own job (--iwyu) next to the tests.
  iwyu=false
  if printf '%s\n' "${files[@]}" | grep -qE '^(src|tests)/.*\.(cpp|cc|h|hpp)$' && [ -f tools/ci/iwyu_lite.py ]; then iwyu=true; fi
  git diff -U0 "$base" "$tip" -- CMakeLists.txt > "$out/cmake.diff" # (test-only CMake edits run just those tests)
  regex=""; [ ${#files[@]} -gt 0 ] && regex=$(python3 tools/test/impacted.py --build build --base "$base" -v --cmake-diff "$out/cmake.diff" "${tierarg[@]}" \
    --files "${files[@]}" 2> "$out/impacted.log")
  # Areas (tests/areas.map, docs/testing-flow.md): smoke + the changed files' areas join impacted.py's picks, and the
  # sculptforge_tests shards run only those areas' cases (SF_TEST_AREA). A core header, CMake or an unknown path: everything.
  # A branch from before areas (no affected.py) keeps impacted.py's list alone.
  unit_areas=""
  if [ ${#files[@]} -gt 0 ] && [ -f tools/test/affected.py ] && [ -f tests/areas.map ]; then
    plan_json=$(python3 tools/test/affected.py --build build --cmake-diff "$out/cmake.diff" "${tierarg[@]}" --files "${files[@]}" --merge "$regex" --json -v 2>> "$out/impacted.log") &&
      { regex=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["regex"])' <<< "$plan_json")
        unit_areas=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["unit_areas"])' <<< "$plan_json"); }
  fi
  echo "T0 ($tier): ${#files[@]} changed file(s) -> ${regex:-no tests}${unit_areas:+ (unit cases of: $unit_areas)}"
  if [ -z "$regex" ] && [ "$iwyu" = false ]; then result=green; finish; fi
  # "[t0 full]" in the branch tip's subject runs every test (proof that a full run completes here).
  git log -1 --format=%s "$tip" | grep -qF '[t0 full]' && { regex=.; echo "T0: [t0 full] in the commit subject: every test"; }
  n=0
  if [ -n "$regex" ]; then
    python3 -I tools/ci/shard.py plan --build build --regex "$regex" --out "$out/shards.json" --max "${SF_T0_MAX_SHARDS:-8}" || {
      python3 -I -c 'import json,sys; json.dump({"regex": sys.argv[1], "tests": 0, "n": 1, "shards": [[]]}, open(sys.argv[2], "w"))' "$regex" "$out/shards.json"; }
    n=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["n"])' "$out/shards.json")
  fi
  python3 -I - "$out" "$regex" "$unit_areas" "$tier" "$base" "$build_s" "$start" "$n" "$iwyu" "$([ ${#files[@]} -gt 0 ] && printf '%s\n' "${files[@]}" | paste -sd' ')" <<'EOF'
import json, os, sys
out, regex, areas, tier, base, build_s, start, n, iwyu, files = sys.argv[1:]
p = {"regex": regex, "unit_areas": areas, "tier": tier, "base": base, "build_s": int(build_s), "started": float(start),
     "n": int(n), "iwyu": iwyu == "true", "files": files.split()}
if os.path.exists(f"{out}/shards.json"):
    p["shards"] = json.load(open(f"{out}/shards.json"))["shards"]
    p["tests"] = json.load(open(f"{out}/shards.json"))["tests"]
json.dump(p, open(f"{out}/plan.json", "w"))
if os.environ.get("GITHUB_OUTPUT"):
    open(os.environ["GITHUB_OUTPUT"], "a").write(f"shards={json.dumps(list(range(p['n'])))}\niwyu={'true' if p['iwyu'] else 'false'}\nearly=false\n")
EOF
  echo "T0: ${n} test job(s)$([ "$iwyu" = true ] && echo ', iwyu_lite')"
  exit 0
fi

# ---- iwyu job --------------------------------------------------------------------------------------------------
if [ $mode = iwyu ]; then
  mapfile -t files < <(python3 -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1] + "/plan.json")).get("files", [])))' "$plan")
  r=green; t=$(date +%s)
  if python3 -I tools/ci/iwyu_lite.py --build build ${SF_IWYU_CXX:+--cxx "$SF_IWYU_CXX"} -j 4 --max-fanout 40 --files "${files[@]}" > "$out/iwyu.log" 2>&1; then
    echo "T0: $(head -1 "$out/iwyu.log") ($(($(date +%s) - t)) s)"
  else
    cat "$out/iwyu.log"; cp "$out/iwyu.log" "$out/output.txt"; r=red
  fi
  echo "{\"result\": \"$r\", \"s\": $(($(date +%s) - t))}" > "$out/iwyu.json"
  exit 0
fi

# ---- test job (shard K) ----------------------------------------------------------------------------------------
tier=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1] + "/plan.json")).get("tier", "ready"))' "$plan")
export SF_TEST_AREA=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1] + "/plan.json")).get("unit_areas", ""))' "$plan")
[ -z "$SF_TEST_AREA" ] && unset SF_TEST_AREA
rx=$(python3 -I tools/ci/shard.py regex "$plan/shards.json" "$k")
python3 -I tools/ci/shard.py costdata "$plan/shards.json" "$k" build/Testing/Temporary/CTestCostData.txt # ctest starts the slowest first
ntests=$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["shards"][int(sys.argv[2])]))' "$plan/shards.json" "$k")
echo "T0 test job $k: $ntests test(s)"
args=(-j 4); [ "$rx" != . ] && args+=(-R "$rx")
args+=(-E '^marks$') # (passed above, on this tree: ~1 min of a runner's four slots, 11-11 qa-lean)
t=$(date +%s)
# The tests run in the background and the shell waits, so a SIGTERM reaches this trap at once. A full run once died here (exit
# 143, no artifact): the warm UI servers had grown past the runner's 16 GB and the kernel's OOM kill took the job with it
# (tools/test/warm_ui.py MAX_MB). The trap and the memory line below say why, should a runner ever do that again.
trap 'echo "[t0] SIGTERM $(date +%T)"; free -m; ps -eo pid,ppid,rss,etime,args --sort=-rss | cut -c1-150 | head -8; sudo dmesg 2>/dev/null | grep -i -E "out of memory|oom" | tail -3; tail -4 "$out/ctest.log"; exit 143' TERM
( while sleep 60; do echo "[t0 $(date +%T)] mem used $(free -m | awk 'NR==2{print $3 " avail " $7}') MB, $(grep -c ' Test  *#' "$out/ctest.log" 2>/dev/null) tests done"; done ) &
tick=$!
SDL_VIDEO_DRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  python3 tools/test/run.py --build build --junit build/ctest.xml --times "$out/times.tsv" -- "${args[@]}" > "$out/ctest.log" 2>&1 & wait $!; rc=$?
kill $tick 2>/dev/null
pkill -f 'build/sculptforge --ui-serve' 2>/dev/null # (a pool the fixture cleanup missed)
test_s=$(($(date +%s) - t))
tail -40 "$out/ctest.log"
ran=$(sed -n 's/^== \([0-9]*\) run.*/\1/p' "$out/ctest.log" | tail -1)
failed=""; result=red
if [ $rc -eq 0 ] && [ -n "$ran" ]; then result=green
else
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
  # A failing UI script's replay (monkey / mode matrix: replay-*.sfui) and FAIL-*.png go with the artifact: without them a
  # runner-only failure (11-24: ui_monkey seed 4 green on every laptop run) leaves nothing to replay. A few files, not the shots.
  mkdir -p "$out/ui-fails"
  find build/ui-shots -type f \( -name 'replay-*.sfui' -o -name 'FAIL-*.png' \) 2>/dev/null | head -40 | while read -r f; do
    cp "$f" "$out/ui-fails/$(basename "$(dirname "$f")")-$(basename "$f")"
  done
fi
python3 -I -c 'import json,sys; k,res,failed,ran,tests,s=sys.argv[1:7]; json.dump({"shard": int(k), "result": res, "failed": failed.split(), "ran": int(ran or 0), "tests": int(tests), "test_s": int(s)}, open(sys.argv[7] + "/shard.json", "w"))' \
  "$k" "$result" "$failed" "${ran:-0}" "$ntests" "$test_s" "$out"
echo "T0 test job $k: $result${failed:+: $failed} ($ran run, $test_s s)"
exit 0
