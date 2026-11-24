#!/usr/bin/env python3
# bench_pack_run: sculptforge --benchmark-pack on a small pack (the repository's pack builder, the manifest cut down to
# two scenes): exit 0, the result is a valid sculptforge-benchmark/1 file with the pack's hash, a run and a tier label per
# scene, the hardware fields (CPU, GPU, driver, RAM, OS, tier) and per-stroke p50/p99/fps/slowest frame; under
# SF_HW_SIMULATE=floor it says so; a damaged pack and a missing folder exit 1 before any window.
#   bench_pack_run.py APP WORK_DIR
import importlib.util, json, os, pathlib, subprocess, sys

app, work = sys.argv[1], pathlib.Path(sys.argv[2])
root = pathlib.Path(__file__).resolve().parents[1]
work.mkdir(parents=True, exist_ok=True)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


submit = load("submit", root / "tools" / "benchmark" / "submit.py")
make_pack = load("make_pack", root / "tools" / "benchmark" / "make_pack.py")
pack = work / "pack"
make_pack.build(pack, work / "pack.zip")
manifest = json.loads((pack / "pack.json").read_text())
manifest["scenes"] = [dict(s, divide=d, triangles=5632 * 4 ** d) for s, d in zip(manifest["scenes"][:2], (2, 3))]
(pack / "pack.json").write_text(json.dumps(manifest, indent=2) + "\n")
digest = make_pack.pack_hash(pack)

# Windows keeps its environment and SDL's own video driver: CI's Mesa there is WGL (opengl32.dll), and the offscreen
# driver needs EGL ("Could not load EGL library"). Elsewhere: offscreen, with a bare PATH.
env = dict(os.environ, SF_NO_UPDATE_CHECK="1")
if os.name != "nt":
    env = {"SDL_VIDEO_DRIVER": "offscreen", "SF_NO_UPDATE_CHECK": "1", "PATH": "/usr/bin:/bin"}
out = work / "results.json"
out.unlink(missing_ok=True)
run = subprocess.run([app, "--benchmark-pack", str(pack), "--out", str(out), "--benchmark-seconds", "1"],
                     capture_output=True, text=True, env=env, timeout=600)
assert run.returncode == 0, f"exit {run.returncode}: {run.stderr[-600:]}"
assert out.stat().st_size < 30000, f"{out.stat().st_size} bytes for two scenes: three must fit an issue form's 65,536 characters"
doc = json.loads(out.read_text())
# (The manifest was cut to two scenes, so this is not the published pack: the check refuses it, and passes with its own hash.)
errs = submit.validate(doc)
assert errs and all("settings.pack.sha256" in e for e in errs), errs
submit.known_pack_hashes = lambda: {"1": digest}
errs = submit.validate(doc)
assert not errs, errs
st = doc["settings"]
assert st["scene"] == "pack-1" and st["stroke_set"] == "pack/1" and not st["video"], st
assert st["pack"] == {"name": "SculptForge bench pack", "version": 1, "sha256": digest}, st["pack"]
assert [r["label"] for r in doc["runs"]] == [s["id"] for s in manifest["scenes"]], doc["runs"]
assert [r["tier"] for r in doc["runs"]] == ["floor", "standard"]
assert [r["triangles"] for r in doc["runs"]] == [s["triangles"] for s in manifest["scenes"]]
hw = doc["hardware"]
for k in ("cpu", "gpu", "gpu_name", "driver", "os", "hw_tier"):
    assert isinstance(hw.get(k), str) and hw[k], (k, hw)
assert hw["ram_gb"] > 0 and hw["logical_cores"] > 0 and len(hw["framebuffer"]) == 2, hw
assert hw["driver"] in hw["gpu"] and hw["gpu_name"] in hw["gpu"], hw
for r in doc["runs"]:
    names = [p["name"] for p in r["phases"]]
    assert names[:6] == ["clay small", "clay medium", "clay large", "move", "smooth", "240 px brush"] and "orbit" in names, names
    for p in r["phases"]:
        f = p["frame_ms"]
        assert f["p50"] > 0 and f["p99"] >= f["p50"] and p["fps"] > 0 and p["frames"] > 0 and p["worst_stall_ms"] >= 0, p
assert "hw_simulate" not in st
# The floor tier on any computer: the same run with the entry-level tier forced says so, in the settings and the hardware.
out2 = work / "floor.json"
out2.unlink(missing_ok=True)
run = subprocess.run([app, "--benchmark-pack", str(pack), "--out", str(out2), "--benchmark-seconds", "1"],
                     capture_output=True, text=True, env=dict(env, SF_HW_SIMULATE="floor"), timeout=600)
assert run.returncode == 0, f"floor run exit {run.returncode}: {run.stderr[-600:]}"
d2 = json.loads(out2.read_text())
assert d2["settings"]["hw_simulate"] == "floor" and d2["hardware"]["hw_tier"] == "Floor", (d2["settings"], d2["hardware"])
assert not submit.validate(d2)
# A pack that was altered, and one that isn't there.
data = (pack / "creature-head.sfproj").read_bytes()
(pack / "creature-head.sfproj").unlink()
(work / "no.json").unlink(missing_ok=True)  # (a rerun in the same build dir: the last run's file must not count)
bad = subprocess.run([app, "--benchmark-pack", str(pack), "--out", str(work / "no.json")], capture_output=True, text=True, env=env, timeout=60)
assert bad.returncode == 1 and "creature-head.sfproj" in bad.stderr and not (work / "no.json").exists(), (bad.returncode, bad.stderr)
(work / "no.json").unlink(missing_ok=True)
(pack / "creature-head.sfproj").write_bytes(data[: len(data) // 2])  # cut in half: the scenes won't build
bad = subprocess.run([app, "--benchmark-pack", str(pack), "--out", str(work / "no.json"), "--benchmark-seconds", "1"],
                     capture_output=True, text=True, env=env, timeout=300)
assert bad.returncode == 1, f"a pack that won't build should exit 1, got {bad.returncode}: {bad.stderr[-400:]}"
assert submit.validate(json.loads((work / "no.json").read_text())), "a pack that failed to build must not validate"
none = subprocess.run([app, "--benchmark-pack", str(work / "nothing-here")], capture_output=True, text=True, env=env, timeout=60)
assert none.returncode == 1 and "pack.json" in none.stderr, (none.returncode, none.stderr)
print("bench_pack_run: ok")
