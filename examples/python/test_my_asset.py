# SculptForge for Python: testing your own asset rules with pytest. Run: pytest test_my_asset.py
# Replace the sphere with sf.run(doc, "open", path="your_asset.fbx") and the rules with your studio's.
# SPDX-License-Identifier: GPL-3.0-or-later
import numpy as np
import pytest

import sculptforge as sf


@pytest.fixture
def asset():
    doc = sf.Document()
    sf.run(doc, "new_sphere", subdivisions=4)
    return doc


def test_triangle_budget(asset):
    assert asset.triangles.shape[0] <= 10_000


def test_fits_in_a_two_unit_box(asset):
    v = asset.vertices
    assert (v.max(axis=0) - v.min(axis=0) <= 2.0 + 1e-4).all()


def test_no_degenerate_triangles(asset):
    v, t = asset.vertices, asset.triangles
    area = np.linalg.norm(np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]]), axis=1) / 2
    assert area.min() > 1e-8


def test_exports_to_glb(asset, tmp_path):
    sf.run(asset, "export_glb", path=tmp_path / "asset.glb")
    assert (tmp_path / "asset.glb").read_bytes()[:4] == b"glTF"
