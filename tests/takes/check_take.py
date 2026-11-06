#!/usr/bin/env python3
# bench_take_sphere: plays tests/takes/sphere.sfui with sculptforge --benchmark-script and checks what comes out. The
# CSV has a row per frame, strokes with their lag; the take ran in real time (at least as long as its script, and its
# frame times add up to its wall clock within 5%); the video is as long as the take within 5% (ffprobe, when there).
#   check_take.py APP TAKE OUT_DIR [--min-seconds S]
import argparse, csv, os, shutil, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("app")
ap.add_argument("take")
ap.add_argument("out")
ap.add_argument("--min-seconds", type=float, default=4.5)
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
video, table = os.path.join(a.out, "take.mp4"), os.path.join(a.out, "take.csv")
for f in (video, table):
    if os.path.exists(f):
        os.remove(f)
ffmpeg = shutil.which("ffmpeg") is not None
cmd = [a.app, "--benchmark-script", a.take, "--size", "1280x720", "--benchmark-out", table]
cmd += ["--benchmark-video", video] if ffmpeg else ["--benchmark-no-video"]
env = dict(os.environ, SF_NO_UPDATE_CHECK="1")
start = time.monotonic()
run = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=240)
elapsed = time.monotonic() - start
print(run.stdout, end="")
fails = []
if run.returncode != 0:
    fails.append(f"exit code {run.returncode}:\n{run.stderr[-3000:]}")
if "skipped (a take checks and shoots nothing)" not in run.stderr + run.stdout:
    fails.append("the shot and expectation weren't reported as skipped")
rows = []
if os.path.exists(table):
    with open(table, newline="") as f:
        r = csv.DictReader(f)
        if r.fieldnames != ["frame", "t_s", "frame_ms", "stroking", "lag_ms", "lag_px"]:
            fails.append(f"CSV columns {r.fieldnames}")
        rows = list(r)
else:
    fails.append("no CSV")
if rows:
    wall = float(rows[-1]["t_s"])
    total = sum(float(x["frame_ms"]) for x in rows) / 1000.0
    lagged = [float(x["lag_ms"]) for x in rows if x["lag_ms"]]
    stroking = sum(1 for x in rows if x["stroking"] == "1")
    print(f"{len(rows)} frames, {wall:.2f} s (process {elapsed:.1f} s), frame times {total:.2f} s, "
          f"{stroking} stroking, {len(lagged)} with lag")
    if wall < a.min_seconds:
        fails.append(f"the take took {wall:.2f} s, under its script's {a.min_seconds} s: not real time")
    if wall > elapsed:
        fails.append(f"the take's clock ({wall:.2f} s) ran past the process's ({elapsed:.2f} s)")
    if abs(total - wall) > 0.05 * wall:
        fails.append(f"frame times add up to {total:.2f} s, the wall clock {wall:.2f} s (more than 5% apart)")
    if stroking < 5 or len(lagged) < 3:
        fails.append(f"too few stroke frames ({stroking}) or lag samples ({len(lagged)})")
    if any(l < 0 or l > 60000 for l in lagged):
        fails.append("a lag out of range")
    if ffmpeg:
        if not os.path.exists(video) or os.path.getsize(video) == 0:
            fails.append("no video")
        elif shutil.which("ffprobe"):
            probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", video],
                                   capture_output=True, text=True)
            try:
                length = float(probe.stdout.strip())
                print(f"video {length:.2f} s")
                if abs(length - wall) > 0.05 * wall:
                    fails.append(f"the video is {length:.2f} s, the take {wall:.2f} s (more than 5% apart)")
            except ValueError:
                fails.append(f"ffprobe: {probe.stderr.strip()}")
for f in fails:
    print("FAIL", f)
sys.exit(1 if fails else 0)
