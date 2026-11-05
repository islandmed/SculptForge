#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scoreboard rows (docs/design/perf-scoreboard.md) from benchmark results.

    python3 tools/perf_scoreboard.py TIER result.json [result.json ...]

Each result is a `sculptforge --benchmark <scene> --benchmark-out <file>` JSON. Prints one markdown row per brush
phase (committed lag p50 / p99, release, orbit fps, memory); with several results for the same scene and phase
(interleaved repeats) the cells show the range."""
import json
import sys

PHASES = [("clay small", "25 px"), ("clay large", "120 px"), ("240 px brush", "240 px")]


def rng(values, fmt="{:.0f}"):
    values = [v for v in values if v is not None]
    if not values:
        return "-"
    lo, hi = min(values), max(values)
    return fmt.format(lo) if fmt.format(lo) == fmt.format(hi) else fmt.format(lo) + "-" + fmt.format(hi)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    tier, rows = sys.argv[1], {}
    for path in sys.argv[2:]:
        with open(path) as f:
            root = json.load(f)
        scene = root.get("settings", {}).get("scene", "?")
        commit = root.get("sculptforge", {}).get("commit", "?")
        for run in root.get("runs", []):
            phases = {p["name"]: p for p in run.get("phases", [])}
            orbit = phases.get("orbit", {})
            for name, label in PHASES:
                p = phases.get(name)
                if not p or "lag_ms" not in p:
                    continue
                r = rows.setdefault((scene, run["label"], label), {"commit": set(), "p50": [], "p99": [], "rel": [],
                                                                    "fps": [], "ram": [], "gpu": []})
                r["commit"].add(commit)
                r["p50"].append(p["lag_ms"]["p50"])
                r["p99"].append(p["lag_ms"]["p99"])
                r["rel"].append(p.get("release_done_ms"))
                r["fps"].append(orbit.get("fps"))
                r["ram"].append(run.get("peak_ram_mb"))
                r["gpu"].append(run.get("gpu_buffers_mb"))
    for (scene, label, brush), r in rows.items():
        mem = rng([x / 1024 for x in r["ram"] if x], "{:.1f}") + " GB + " + rng(r["gpu"]) + " MB GPU"
        print(f"| {label} | {brush} | {tier} | {rng(r['p50'])} / {rng(r['p99'])} ms | {rng(r['rel'])} ms | "
              f"{rng(r['fps'])} | {mem} | {', '.join(sorted(r['commit']))} |")


if __name__ == "__main__":
    main()
