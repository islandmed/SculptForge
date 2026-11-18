# SculptForge - the Python API: documents, arrays, Parts, selection sets, commands and errors.
# SPDX-License-Identifier: GPL-3.0-or-later
import numpy as np
import pytest

import sculptforge as sf


@pytest.fixture
def sphere():
    doc = sf.Document()
    assert "triangles" in sf.run(doc, "new_sphere", subdivisions=3)
    return doc


def test_version_and_names():
    assert sf.__version__.count(".") == 2
    assert sf.Document.__module__ == "sculptforge" and issubclass(sf.CommandError, RuntimeError)
    assert set(sf.__all__) <= set(dir(sf))


def test_mesh_arrays(sphere):
    v, t = sphere.vertices, sphere.triangles
    assert v.shape == (642, 3) and v.dtype == np.float32
    assert t.shape == (1280, 3) and t.dtype == np.uint32
    assert sphere.normals.shape == v.shape and "642 vertices" in repr(sphere)


def test_attributes_round_trip_and_survive_subdivision(sphere):
    v = sphere.vertices
    assert sphere.mask is None and sphere.colors is None and sphere.facegroups is None and sphere.hidden is None
    sphere.mask = (v[:, 1] > 0).astype(np.float32)
    sphere.colors = np.tile(np.float32([1, 0, 0]), (len(v), 1))
    sphere.facegroups = np.arange(1280, dtype=np.uint32) % 3
    assert sphere.mask.dtype == np.float32 and sphere.colors.shape == (642, 3)
    sf.run(sphere, "subdivide")
    assert sphere.mask.shape == (sphere.vertices.shape[0],)
    assert sphere.facegroups.shape == (sphere.triangles.shape[0],)
    with pytest.raises(ValueError):
        sphere.mask = [0.0, 1.0]
    sphere.colors = None
    assert sphere.colors is None


def test_layers(sphere):
    sf.run(sphere, "layer_add", name="Smile")
    assert sphere.layers == [("Smile", 1.0)]
    assert sphere.layer_delta(0).shape == (642, 3)
    with pytest.raises(IndexError):
        sphere.layer_delta(3)


def test_parts():
    doc = sf.Document()
    sf.run(doc, "new_sphere", subdivisions=2)
    sf.run(doc, "part_append", type="cube", name="Box")
    assert [p["name"] for p in doc.parts] == ["Sphere", "Box"] and doc.active_part == 1
    doc.active_part = "Sphere"
    assert doc.active_part == 0 and doc.triangles.shape[0] == 320
    with pytest.raises(IndexError):
        doc.active_part = 5


def test_set_mesh():
    doc = sf.Document()
    doc.set_mesh(np.float32([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]]), [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]])
    assert doc.triangles.shape == (4, 3)
    with pytest.raises(ValueError):
        doc.set_mesh([[0, 0, 0]], [[0, 1, 2]])


def test_selection_sets(sphere, tmp_path):
    top = (sphere.vertices[:, 1] > 0).astype(np.float32)
    assert "Stored" in sphere.set_selection_set("Top", mask=top, positions=sphere.vertices)
    info = sphere.selection_sets[0]
    assert info["name"] == "Top" and info["channels"] == ["mask", "morph"] and info["fits"]
    pulled = sphere.selection_set("Top")
    assert np.array_equal(pulled["mask"], top) and pulled["positions"].shape == (642, 3)
    sf.run(sphere, "selection_set_restore", name="Top")
    assert np.array_equal(sphere.mask, top)
    with pytest.raises(KeyError):
        sphere.selection_set("Nope")
    with pytest.raises(ValueError):
        sphere.set_selection_set("Bad", mask=[1.0, 0.0])
    path = tmp_path / "sets.sfsets"
    sf.run(sphere, "selection_sets_export", path=path)
    other = sf.Document()
    sf.run(other, "new_sphere", subdivisions=3)
    sf.run(other, "selection_sets_import", path=path)
    assert [s["name"] for s in other.selection_sets] == ["Top"]


def test_commands_and_schema():
    names = {c["name"] for c in sf.commands()}
    assert {"new_sphere", "remesh", "decimate", "stroke", "subdivide", "export_glb", "export_usd"} <= names
    stroke = next(c for c in sf.commands() if c["name"] == "stroke")
    assert {"points", "brush", "radius", "strength"} <= {p["name"] for p in stroke["params"]}
    assert '"name":"remesh"' in sf.tool_schema()


def test_errors(sphere, tmp_path):
    with pytest.raises(sf.CommandError, match="open"):
        sf.run(sphere, "open", path=tmp_path / "missing.obj")
    with pytest.raises(sf.CommandError):
        sf.run(sphere, "no_such_command")
    with pytest.raises(sf.CommandError, match="no_such_command"):
        sf.run_script(sphere, "subdivide; no_such_command")

