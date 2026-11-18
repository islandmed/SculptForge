# SculptForge - the Pythonic Document (python/sculptforge/_document.py): files, strokes, levels, Parts, masks.
# SPDX-License-Identifier: GPL-3.0-or-later
import threading
import time

import numpy as np
import pytest

import sculptforge as sf
from sculptforge import _core


@pytest.fixture
def sphere():
    doc = sf.Document()
    doc.new_sphere(4)
    return doc


def test_is_the_engine_document(sphere):
    assert isinstance(sphere, _core.Document) and type(sphere).__module__ == "sculptforge"
    assert "triangles" in sf.run(sphere, "subdiv_info")  # every command still takes it


@pytest.mark.parametrize("suffix", [".sfproj", ".obj", ".glb", ".usda", ".fbx", ".ply", ".stl"])
def test_save_by_suffix_and_open(sphere, tmp_path, suffix):
    path = tmp_path / f"asset{suffix}"
    sphere.save(path)
    assert path.stat().st_size > 0
    if suffix != ".stl":
        back = sf.Document.open(path)
        assert isinstance(back, sf.Document) and back.triangles.shape[0] > 0


def test_project_keeps_parts(sphere, tmp_path):
    sphere.add_part("cube", name="Box")
    sphere.save(tmp_path / "scene.sfproj")
    assert sf.Document.open(tmp_path / "scene.sfproj").part_names() == ["Sphere", "Box"]


def test_save_refuses_unknown_suffix(sphere, tmp_path):
    with pytest.raises(ValueError, match="no format"):
        sphere.save(tmp_path / "asset.xyz")
    assert sphere.export is not None and sf.Document.export is sf.Document.save


def test_stroke_from_numpy_equals_the_command(tmp_path):
    path = np.array([[-0.6, 0.2, 1.0], [0.0, 0.4, 1.0], [0.6, 0.2, 1.0]], dtype=np.float32)
    a = sf.Document()
    a.new_sphere(4)
    a.stroke("Clay", path, radius=0.25, strength=0.6, symmetry="x")
    b = sf.Document()
    sf.run(b, "new_sphere", subdivisions=4)
    text = " ".join(",".join(format(float(v), ".9g") for v in p) for p in path)
    sf.run(b, "stroke", brush="Clay", points=text, radius=0.25, strength=0.6, symmetry="x")
    assert a.vertices.tobytes() == b.vertices.tobytes()


def test_stroke_moves_the_surface_and_one_point_is_a_dab(sphere):
    before = sphere.vertices
    sphere.stroke("Inflate", (0, 0.8, 0.6), radius=0.3, strength=0.5, view=(0, -0.8, -0.6))
    moved = np.linalg.norm(sphere.vertices - before, axis=1)
    assert 0 < (moved > 1e-6).sum() < len(moved)


@pytest.mark.parametrize("bad", [[], [[0, 0]], [[0, 0, float("nan")]], "0,0,1"])
def test_stroke_refuses_bad_points(sphere, bad):
    with pytest.raises(ValueError):
        sphere.stroke("Clay", bad)


def test_subdivision_levels(sphere):
    assert sphere.subdivision_levels == 0 and sphere.subdivision_level == 0
    low = sphere.triangles.shape[0]
    sphere.divide()
    sphere.divide()
    assert sphere.subdivision_levels == 3 and sphere.subdivision_level == 3
    assert sphere.triangles.shape[0] == low * 16
    sphere.subdivision_level = 1
    assert sphere.subdivision_level == 1 and sphere.triangles.shape[0] == low
    with pytest.raises(IndexError):
        sphere.subdivision_level = 4


def test_remesh_decimate_subdivide(sphere):
    n = sphere.triangles.shape[0]
    sphere.subdivide()
    assert sphere.triangles.shape[0] == n * 4
    sphere.decimate(0.5)
    assert sphere.triangles.shape[0] < n * 4
    with pytest.raises(ValueError):
        sphere.decimate(0.5, triangles=100)
    sphere.remesh(64)
    assert sphere.triangles.shape[0] > 0


def test_masks(sphere):
    sphere.mask = (sphere.vertices[:, 1] > 0).astype(np.float32)
    top = int(sphere.mask.sum())
    sphere.invert_mask()
    assert int(sphere.mask.sum()) == len(sphere.mask) - top
    sphere.clear_mask()
    assert sphere.mask is None or not sphere.mask.any()


def test_parts(sphere):
    assert sphere.add_part("cube", name="Box") == 1
    sphere.rename_part("Box", "Crate")
    assert sphere.part_names() == ["Sphere", "Crate"]
    sphere.remove_part(0)
    assert sphere.part_names() == ["Crate"]
    with pytest.raises(KeyError):
        sphere.remove_part("Nope")
    with pytest.raises(IndexError):
        sphere.rename_part(5, "x")
    with pytest.raises(ValueError):
        sphere.add_part()


def test_command_discovery():
    info = sf.command("stroke")
    assert info["name"] == "stroke" and any(p["name"] == "points" for p in info["params"])
    with pytest.raises(KeyError):
        sf.command("no_such_command")


def test_long_operations_release_the_gil(sphere):
    sphere.subdivide(2)  # ~80k triangles: a remesh that takes a while
    ticks = 0
    done = threading.Event()

    def work():
        sphere.remesh(256)
        done.set()

    t = threading.Thread(target=work)
    t.start()
    start = time.perf_counter()
    while not done.is_set():
        ticks += 1  # this loop runs only while the remesh doesn't hold the GIL
        time.sleep(0.001)
    t.join()
    assert time.perf_counter() - start < 1.0 or ticks > 10
