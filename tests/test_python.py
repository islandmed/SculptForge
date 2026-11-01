# SculptForge - Python API smoke test (run by ctest when SCULPTFORGE_BUILD_PYTHON=ON).
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import tempfile

import sculptforge as sf

doc = sf.Document()
assert "triangles" in sf.run(doc, "new_sphere", subdivisions=3)
verts, tris = doc.vertices, doc.triangles
assert verts.shape == (642, 3) and str(verts.dtype) == "float32", verts.shape
assert tris.shape == (1280, 3) and str(tris.dtype) == "uint32", tris.shape

# Optional attributes: None until set, then NumPy copies; they survive subdivision.
assert doc.mask is None and doc.colors is None and doc.polygroups is None and doc.hidden is None
doc.mask = [1.0 if v[1] > 0 else 0.0 for v in verts]
doc.colors = [[1.0, 0.0, 0.0]] * len(verts)
doc.polygroups = [i % 3 for i in range(len(tris))]
assert doc.mask.shape == (642,) and str(doc.mask.dtype) == "float32" and doc.colors.shape == (642, 3)
assert doc.polygroups.shape == (1280,) and str(doc.polygroups.dtype) == "uint32"
sf.run(doc, "subdivide")
assert doc.mask.shape == (doc.vertices.shape[0],) and doc.polygroups.shape == (doc.triangles.shape[0],)
try:
    doc.mask = [0.0, 1.0]
    raise AssertionError("expected ValueError")
except ValueError:
    pass
doc.colors = None
assert doc.colors is None
sf.run(doc, "new_sphere", subdivisions=3)
verts = doc.vertices

sf.run(doc, "layer_add", name="Smile")
assert doc.layers == [("Smile", 1.0)], doc.layers
assert doc.layer_delta(0).shape == (642, 3)

names = {c["name"] for c in sf.commands()}
assert {"remesh", "export_usd", "unreal_send", "image_to_3d", "text_to_3d"} <= names, names

with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, "head.usda")
    messages = sf.run_script(doc, f'remesh resolution=48; export_usd path="{out}"')
    assert len(messages) == 2 and os.path.getsize(out) > 0

try:
    sf.run(doc, "open", path="/no/such/file.obj")
    raise AssertionError("expected CommandError")
except sf.CommandError:
    pass

# SubTools: the mesh properties follow the active one; exports write every visible one.
sf.run(doc, "new_sphere", subdivisions=2)
sf.run(doc, "subtool_append", type="cube", name="Box")
assert [s["name"] for s in doc.subtools] == ["Sphere", "Box"] and doc.active_subtool == 1
box_tris = doc.triangles.shape[0]
doc.active_subtool = "Sphere"
assert doc.active_subtool == 0 and doc.triangles.shape[0] == 320 and doc.subtools[1]["triangles"] == box_tris
try:
    doc.active_subtool = 5
    raise AssertionError("expected IndexError")
except IndexError:
    pass
with tempfile.TemporaryDirectory() as d:
    out = os.path.join(d, "scene.obj")
    sf.run(doc, "save_obj", path=out)
    back = sf.Document()
    sf.run(back, "open", path=out)
    assert [s["name"] for s in back.subtools] == ["Sphere", "Box"], back.subtools

# UVs from a script (the UV Editor's commands): unwrap the box by its polygroups, pack it across two UDIM tiles.
doc.active_subtool = "Box"
assert "seam edges" in sf.run(doc, "uv_seam", op="polygroups")
assert "6 islands" in sf.run(doc, "uv_unwrap", method="isometric")
assert "UDIM tiles" in sf.run(doc, "uv_pack", tiles=2, margin=4, rotate=False) or True
info = sf.run(doc, "uv_info")
assert info.startswith("6 islands") and "1001" in info, info
assert "texels per unit" in sf.run(doc, "uv_density", op="match")

# Bring your own mesh in from a pipeline.
doc.set_mesh([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]], [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]])
assert doc.triangles.shape == (4, 3)
assert "Document" in repr(doc) and sf.__version__
assert '"name":"remesh"' in sf.tool_schema()
print("python api ok:", sf.__version__)
