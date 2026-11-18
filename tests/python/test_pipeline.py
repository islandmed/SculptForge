# SculptForge - a procedural asset end to end: sphere -> strokes -> remesh -> export, the same bytes every run and
# the same as sculptforge-cli; and saves never leave a half-written file.
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import subprocess

import numpy as np
import pytest

import sculptforge as sf

# One script, read by both sf.run_script and sculptforge-cli. Paths are added per test.
STEPS = [
    "new_sphere subdivisions=5",
    'stroke brush=Clay points="-0.6,0.2,1 0,0.4,1 0.6,0.2,1" radius=0.25 strength=0.6',
    'stroke brush=Crease points="-0.5,-0.3,1 0.5,-0.3,1" radius=0.12 strength=0.5',
    'stroke brush=Inflate points="0,0.8,1" radius=0.3 strength=0.4 symmetry=x',
    'stroke brush=Smooth points="-0.6,0.2,1 0.6,0.2,1" radius=0.2 strength=0.5',
]


def build(doc):
    for line in STEPS:
        sf.run_script(doc, line)
    return doc


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_strokes_change_the_surface():
    plain = sf.Document()
    sf.run(plain, "new_sphere", subdivisions=5)
    doc = build(sf.Document())
    moved = np.linalg.norm(doc.vertices - plain.vertices, axis=1)
    assert moved.max() > 0.01 and np.isfinite(doc.vertices).all()
    assert (moved > 1e-6).mean() < 0.9  # local strokes, not a global change


def test_deterministic(tmp_path):
    a, b = build(sf.Document()), build(sf.Document())
    assert a.vertices.tobytes() == b.vertices.tobytes()
    sf.run(a, "save_obj", path=tmp_path / "a.obj")
    sf.run(b, "save_obj", path=tmp_path / "b.obj")
    assert digest(tmp_path / "a.obj") == digest(tmp_path / "b.obj")


@pytest.mark.parametrize("ext", ["obj", "glb", "usda", "fbx", "ply", "stl"])
def test_exports(tmp_path, ext):
    doc = build(sf.Document())
    sf.run(doc, "remesh", resolution=48)
    cmd = {"obj": "save_obj", "glb": "export_glb", "usda": "export_usd", "fbx": "export_fbx", "ply": "export_ply",
           "stl": "export_stl"}[ext]
    out = tmp_path / f"asset.{ext}"
    message = sf.run(doc, cmd, path=out)
    assert out.stat().st_size > 0 and ".sculptforge-write-" not in message
    assert not [p for p in tmp_path.iterdir() if p.name.startswith(".sculptforge-write-")]
    if ext in ("obj", "glb", "usda", "fbx", "ply"):
        back = sf.Document()
        sf.run(back, "open", path=out)
        assert back.triangles.shape[0] > 0


def test_failed_export_keeps_the_old_file(tmp_path):
    out = tmp_path / "keep.obj"
    out.write_text("previous\n")
    doc = sf.Document()  # an empty scene: nothing to export
    with pytest.raises(sf.CommandError):
        sf.run(doc, "save_obj", path=out)
    assert out.read_text() == "previous\n"
    assert not [p for p in tmp_path.iterdir() if p.name.startswith(".sculptforge-write-")]


def test_export_replaces_atomically(tmp_path):
    out = tmp_path / "asset.obj"
    out.write_text("old\n")
    doc = build(sf.Document())
    sf.run(doc, "save_obj", path=out)
    assert out.read_text().startswith(("#", "v", "o", "mtllib", "g"))


def test_same_result_as_the_cli(tmp_path, cli):
    py_out, cli_out = tmp_path / "py.obj", tmp_path / "cli.obj"
    build(doc := sf.Document())
    sf.run(doc, "save_obj", path=py_out)
    script = "; ".join(STEPS) + f'; save_obj path="{cli_out.as_posix()}"'
    subprocess.run([cli, "-q", "--no-packs", "-c", script], check=True, timeout=300)
    assert digest(py_out) == digest(cli_out)


def test_multi_file_export_moves_every_file(tmp_path):
    # export_lods writes one file per level; all of them arrive, in a folder whose name isn't ASCII.
    folder = tmp_path / "Zoë modèles"
    folder.mkdir()
    doc = build(sf.Document())
    message = sf.run(doc, "export_lods", path=folder / "asset.glb", levels=3)
    files = sorted(p.name for p in folder.iterdir())
    assert len([f for f in files if f.endswith(".glb")]) >= 2, files
    assert not [f for f in files if f.startswith(".sculptforge-write-")], files
    assert ".sculptforge-write-" not in message
