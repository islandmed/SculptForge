#!/usr/bin/env python3
# bench_scene_names: the proof-mode scene names the command line accepts (standard-100m among them), the usage text, and
# that a run under SF_HW_SIMULATE records it (settings.hw_simulate) in its JSON (core-100m: the quickest scene).
#   bench_scene_names.py APP OUT_DIR
import json, os, subprocess, sys

app, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
env = {"SDL_VIDEO_DRIVER": "offscreen", "SF_NO_UPDATE_CHECK": "1", "PATH": "/usr/bin:/bin"}
bad = subprocess.run([app, "--benchmark", "standard-nope"], capture_output=True, text=True, env=env, timeout=30)
assert bad.returncode == 1, f"an unknown scene should exit 1, got {bad.returncode}"
for name in ("standard-15m", "standard-33m", "standard-100m", "core-100m", "core-200m", "core-400m", "core-800m"):
    assert name in bad.stderr, f"{name} missing from the unknown-scene message: {bad.stderr!r}"
usage = subprocess.run([app, "--help"], capture_output=True, text=True, env=env, timeout=30)
assert "standard-100m" in usage.stdout + usage.stderr, "--help does not mention standard-100m"
assert "core-800m" in usage.stdout + usage.stderr, "--help does not mention core-800m"
# core-800m on a small machine (1.3 GB free: 1240 MiB, then a full disk): one plain line and exit 1, before anything is written
swap = os.path.join(out, "swap-800m")
env = dict(env, SF_MICRO_SWAP_DIR=swap, SF_AVAILABLE_MEMORY_MB="1240")
low = subprocess.run([app, "--benchmark", "core-800m", "--benchmark-no-video"], capture_output=True, text=True, env=env, timeout=60)
assert low.returncode == 1 and "core-800m skipped: needs ~6.4 GB free, 1.3 GB available" in low.stderr, (low.returncode, low.stderr[-400:])
assert "Writing the core-800m scene" not in low.stderr and not os.path.exists(os.path.join(swap, "core-800m-v1.sfproj")), "wrote the scene anyway"
env["SF_AVAILABLE_MEMORY_MB"] = "16000"
env["SF_AVAILABLE_DISK_MB"] = "300"
full = subprocess.run([app, "--benchmark", "core-800m", "--benchmark-no-video"], capture_output=True, text=True, env=env, timeout=60)
assert full.returncode == 1 and "free disk space" in full.stderr and "Writing the core-800m scene" not in full.stderr, (full.returncode, full.stderr[-400:])
for k in ("SF_MICRO_SWAP_DIR", "SF_AVAILABLE_MEMORY_MB", "SF_AVAILABLE_DISK_MB"):
    env.pop(k)
path = os.path.join(out, "hw-simulate.json")
if os.path.exists(path):
    os.remove(path)
env["SF_HW_SIMULATE"] = "floor"
run = subprocess.run([app, "--benchmark", "core-100m", "--benchmark-no-video", "--benchmark-out", path], capture_output=True, text=True, env=env, timeout=400)
assert run.returncode == 0, f"core-100m run exited {run.returncode}: {run.stderr[-400:]}"
settings = json.load(open(path))["settings"]
assert settings.get("hw_simulate") == "floor" and settings.get("scene") == "core-100m", settings
env.pop("SF_HW_SIMULATE")
os.remove(path)
run = subprocess.run([app, "--benchmark", "core-100m", "--benchmark-no-video", "--benchmark-out", path], capture_output=True, text=True, env=env, timeout=400)
assert run.returncode == 0 and "hw_simulate" not in json.load(open(path))["settings"], "an unsimulated run must not record hw_simulate"
print("bench_scene_names: ok")
