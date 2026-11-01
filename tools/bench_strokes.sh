#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Big-brush stroke benchmark: the stroke SculptForge draws itself (--measure-stroke, a fast two-turn circle with a
# 200 px brush, X symmetry) on spheres of about 5M, 21M and 33M triangles (two of them made by Divide), with the
# frame times, how far the brush trails the pointer, press and release. See docs/performance.md.
#
#   tools/bench_strokes.sh [options] BINARY [BINARY_B]
#
# A binary may carry its own environment: VAR=VAL[,VAR=VAL...]@BINARY (e.g. SF_PREDICTIVE_STROKE=0@build/sculptforge
# against build/sculptforge compares predictive strokes off and on with one build).
#   -s SIZES   comma list of 5, 21, 33 (default all three)
#   -n RUNS    runs per size and binary (default 3); with two binaries the runs alternate A, B, A, B...
#   -r PX      brush radius in screen pixels (default 200)
#   -t SEC     stroke length in seconds (default 3)
#   -e VAR=VAL environment for every run (repeatable), e.g. -e SF_PREDICTIVE_STROKE=0
#
# Each run prints SculptForge's own summary line; at the end a table of the means per size and binary.
# Needs a display (the stroke is drawn in the real window); run it on an otherwise idle machine.
set -euo pipefail

sizes="5,21,33"
runs=3
radius=200
seconds=3
envs=()
while getopts "s:n:r:t:e:h" opt; do
    case $opt in
    s) sizes=$OPTARG ;;
    n) runs=$OPTARG ;;
    r) radius=$OPTARG ;;
    t) seconds=$OPTARG ;;
    e) envs+=("$OPTARG") ;;
    *) sed -n "5,21p" "$0"; exit 1 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { sed -n "5,21p" "$0"; exit 1; }
bins=("$@")

scene_for() {
    case $1 in
    5) echo "new_sphere subdivisions=8; subdiv_divide" ;;                     # 5.2M triangles
    21) echo "new_sphere subdivisions=8; subdiv_divide; subdiv_divide" ;;     # 21.0M
    33) echo "new_sphere triangles=2100000; subdiv_divide; subdiv_divide" ;;  # 33.6M
    *) echo "unknown size $1 (use 5, 21 or 33)" >&2; exit 1 ;;
    esac
}

results=$(mktemp)
trap 'rm -f "$results"' EXIT
IFS=, read -ra sizeList <<<"$sizes"
for size in "${sizeList[@]}"; do
    scene=$(scene_for "$size")
    for ((i = 1; i <= runs; i++)); do
        for b in "${!bins[@]}"; do
            bin=${bins[$b]}
            own=()
            if [[ $bin == *@* ]]; then
                IFS=, read -ra own <<<"${bin%%@*}"
                bin=${bin#*@}
            fi
            label=$([ "$b" = 0 ] && echo A || echo B)
            out=$(env ${envs[@]+"${envs[@]}"} ${own[@]+"${own[@]}"} "$bin" --run "$scene" --measure-stroke "$seconds,$radius" 2>&1 || true)
            line=$(printf '%s\n' "$out" | grep -E '^Stroke|^  (off|press|brush|release|frame)' || true)
            echo "== ${size}M  $label run $i  (${bins[$b]})"
            printf '%s\n' "$line"
            # The machine-readable line: frames mean, worst, surface lag, preview lag, press, first surface, release, until done, p95, p99.
            printf '%s\n' "$out" | sed -n "s/^bench: /$size $label /p" >>"$results"
        done
    done
done

echo
echo "Means (ms / px): size, binary, frame mean, frame worst, surface lag, preview lag, press, first surface, release, release until done, frame p95, frame p99"
awk '{ k = $1 " " $2; n[k]++; for (i = 3; i <= NF; i++) s[k, i] += $i; nf[k] = NF }
     END { for (k in n) { printf "%-6s", k; for (i = 3; i <= nf[k]; i++) printf " %8.1f", s[k, i] / n[k]; printf "   (%d runs)\n", n[k] } }' \
    "$results" | sort -n
