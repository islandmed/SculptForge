#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The iPad app in the simulator (.github/workflows/ios.yml): on a base iPad and on the biggest iPad Pro, it installs, starts
# (a screenshot of the start), then runs packaging/ios/smoke.sfui (opens the bust, sculpts, undoes; its own pictures at that
# iPad's size). Exit 1 when the app doesn't start or the script fails; the app's log is beside the pictures.
#   tools/ci/ios-sim-shots.sh APP OUTDIR
# Prints "SF_IOS_SIM=<udid>" of the first simulator last (the core suite runs there next).
set -uo pipefail
app=$1 out=$2
mkdir -p "$out"
out=$(cd "$out" && pwd)
script=$(cd "$(dirname "$0")/../.." && pwd)/packaging/ios/smoke.sfui
bundle=app.sculptforge.ipad
runtime=$(xcrun simctl list runtimes -j | python3 -c '
import json, sys
r = [x for x in json.load(sys.stdin)["runtimes"] if x.get("isAvailable") and x.get("platform", "iOS") == "iOS" and "iOS" in x["name"]]
print(sorted(r, key=lambda x: [int(p) for p in x["version"].split(".")])[-1]["identifier"])')
# The base iPad (A-series: the floor tier) and the largest iPad Pro this Xcode knows.
pick() { xcrun simctl list devicetypes -j | python3 -c '
import json, re, sys
names = [d["name"] for d in json.load(sys.stdin)["devicetypes"]]
for pattern in sys.argv[1:]:
    hits = [n for n in names if re.fullmatch(pattern, n)]
    if hits:
        print(sorted(hits)[-1]); break' "$@"; }
types=("$(pick 'iPad \(A16\)' 'iPad \(10th generation\)' 'iPad \(9th generation\)')"
       "$(pick 'iPad Pro 13-inch \(M\d\)' 'iPad Pro \(12.9-inch\) \(6th generation\)')")
fail=0 first=
for type in "${types[@]}"; do
  [ -n "$type" ] || { echo "no such iPad simulator in this Xcode"; fail=1; continue; }
  name=$(echo "$type" | tr -c 'A-Za-z0-9\n' '-' | tr -s '-' | sed 's/-$//')
  udid=$(xcrun simctl create "sf-$name" "$type" "$runtime")
  first=${first:-$udid}
  xcrun simctl boot "$udid"
  xcrun simctl bootstatus "$udid" -b >/dev/null
  xcrun simctl install "$udid" "$app" || { echo "$type: install failed"; fail=1; continue; }
  # 1. it starts: the start screen after a while
  xcrun simctl launch --terminate-running-process --stdout="$out/$name-start.log" --stderr="$out/$name-start.log" "$udid" $bundle
  sleep 40 # (the simulator draws with a software renderer: a few seconds a frame)
  xcrun simctl io "$udid" screenshot "$out/$name-start.png" >/dev/null
  xcrun simctl spawn "$udid" launchctl list | grep -q "UIKitApplication:$bundle" ||
    { echo "$type: the app is not running 40 s after it started (log: $name-start.log)"; fail=1; }
  xcrun simctl terminate "$udid" $bundle 2>/dev/null
  # 2. the smoke script: open, sculpt, undo, at this iPad's own pixel size (the start log's "window ... (WxH px")
  size=$(sed -nE 's/.*window [0-9]+x[0-9]+ \(([0-9]+x[0-9]+) px.*/\1/p' "$out/$name-start.log" | head -1)
  mkdir -p "$out/$name"
  xcrun simctl launch --stdout="$out/$name-smoke.log" --stderr="$out/$name-smoke.log" "$udid" $bundle \
    --ui-test "$script" --ui-out "$out/$name" ${size:+--size "$size"}
  for _ in $(seq 1 600); do # (up to 20 minutes: software rendering at full iPad size)
    grep -qE '^(PASS|FAIL) ' "$out/$name-smoke.log" 2>/dev/null && break
    sleep 2
  done
  grep -E '^(PASS|FAIL) |OpenGL|GL ES' "$out/$name-smoke.log" | head -5
  grep -q '^PASS ' "$out/$name-smoke.log" || { echo "$type: smoke script did not pass (log: $name-smoke.log)"; fail=1; }
  xcrun simctl terminate "$udid" $bundle 2>/dev/null
done
# (crash reports of the app, if any)
find ~/Library/Logs/DiagnosticReports -name 'SculptForge*' -newer "$app" -exec cp {} "$out/" \; 2>/dev/null
echo "SF_IOS_SIM=$first"
exit $fail
