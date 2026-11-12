#!/usr/bin/env python3
"""e2e record-clip: `sculptforge --ui-test x.sfui --record out.mp4 --fps N` writes every frame of the UI test to a non-empty
mp4 (the before/after clips of bug fixes, ~/Work/.sf/qa-plan.md), and ffprobe counts as many frames as the app says it ran.

Usage: record_clip.py APP SCRIPT OUT_DIR   (SDL_VIDEO_DRIVER=offscreen). Skipped (exit 77) without ffmpeg and ffprobe.
"""
import json
import os
import re
import shutil
import subprocess
import sys

app, script, out = sys.argv[1], sys.argv[2], sys.argv[3]
if not (shutil.which("ffmpeg") and shutil.which("ffprobe")):
    print("SKIP ffmpeg/ffprobe not installed")
    sys.exit(77)
os.makedirs(out, exist_ok=True)
clip = os.path.join(out, "clip.mp4")
if os.path.exists(clip):
    os.remove(clip)
env = dict(os.environ, SDL_VIDEO_DRIVER=os.environ.get("SDL_VIDEO_DRIVER", "offscreen"))
r = subprocess.run([app, "--ui-test", script, "--record", clip, "--fps", "10", "--size", "640x400", "--ui-out", out],
                   env=env, capture_output=True, text=True, timeout=240)
print(r.stdout + r.stderr)
failures = []
m = re.search(r"PASS \S+ \((\d+) frames\)", r.stdout)
if r.returncode != 0 or not m:
    failures.append("app exit %d, no PASS line" % r.returncode)
elif not os.path.exists(clip) or os.path.getsize(clip) == 0:
    failures.append("no mp4 written")
else:
    ran = int(m.group(1))
    p = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                        "stream=nb_read_frames,width,height", "-of", "json", clip], capture_output=True, text=True)
    s = json.loads(p.stdout)["streams"][0]
    got = int(s["nb_read_frames"])
    print("app ran %d frames, mp4 has %d (%sx%s), %d bytes" % (ran, got, s["width"], s["height"], os.path.getsize(clip)))
    if got != ran:
        failures.append("frame count: app %d, mp4 %d" % (ran, got))
    if ran < 5:
        failures.append("too few frames to mean anything: %d" % ran)
for f in failures:
    print("FAIL " + f)
print("PASS record_clip" if not failures else "FAILED")
sys.exit(1 if failures else 0)
