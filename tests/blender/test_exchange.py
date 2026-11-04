# SculptForge - Blender bridge: the add-on's exchange code (no Blender needed, only numpy)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 tests/blender/test_exchange.py [--cli path/to/sculptforge-cli]
#
# With --cli, meshes written the way the add-on writes them go through SculptForge (blender_receive, then
# blender_send) and must come back identical: positions, faces and their winding, UVs, polypaint, mask and
# polygroups, for objects with a rotation, a scale, a negative scale (mirrored) and n-gons. CMake runs this
# when it finds Python 3 with numpy (ctest -R blender_exchange).
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

# exchange.py alone: the package's __init__ needs bpy.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "addons", "blender", "sculptforge_bridge"))
import exchange  # noqa: E402

N = exchange.NO_INDEX
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print("FAIL:", what)


# ---------------------------------------------------------------------------- a Blender-like mesh


def grid_with_ngon():
    """A 3x3 grid of quads (z = bumpy), a pentagon and a triangle hanging off it, as Blender stores
    polygons: vertex positions, loop_start / loop_total and the loops' vertices."""
    co = []
    for j in range(4):
        for i in range(4):
            co.append((i * 0.5, j * 0.5, 0.1 * np.sin(i + 2 * j)))
    polys = []
    for j in range(3):
        for i in range(3):
            a = j * 4 + i
            polys.append([a, a + 1, a + 5, a + 4])
    base = len(co)
    co += [(2.0, 0.0, 0.2), (2.4, 0.6, 0.3), (2.1, 1.2, 0.25)]
    polys.append([3, base, base + 1, base + 2, 7])  # pentagon on the grid's right edge
    polys.append([7, base + 2, 11])  # a triangle
    starts, totals, loop_vert = [], [], []
    for p in polys:
        starts.append(len(loop_vert))
        totals.append(len(p))
        loop_vert += p
    return np.array(co), np.array(starts), np.array(totals), np.array(loop_vert)


def only_ngons():
    """A hexagon and two triangles: a mesh with no quads at all (SculptForge must not pair its fans)."""
    co = [(np.cos(a), np.sin(a), 0.0) for a in np.linspace(0, 2 * np.pi, 7)[:-1]] + [(0.0, 0.0, 1.0), (1.5, 0.0, 0.5)]
    polys = [[0, 1, 2, 3, 4, 5], [0, 6, 1], [5, 7, 0]]
    starts, totals, loop_vert = [], [], []
    for p in polys:
        starts.append(len(loop_vert))
        totals.append(len(p))
        loop_vert += p
    return np.array(co), np.array(starts), np.array(totals), np.array(loop_vert)


def rotation(axis, degrees):
    a = np.radians(degrees)
    c, s = np.cos(a), np.sin(a)
    x, y, z = np.asarray(axis, float) / np.linalg.norm(axis)
    return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                     [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                     [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])


def matrix(rot, scale, translate):
    m = np.eye(4)
    m[:3, :3] = rot @ np.diag(scale)
    m[:3, 3] = translate
    return m


def object_arrays(mesh, mirrored, seed):
    co, starts, totals, loop_vert = mesh
    rng = np.random.default_rng(seed)
    loops, face_poly = exchange.polygon_corners(starts, totals, mirrored)
    faces = exchange.faces_from_loops(loops, loop_vert)
    # Values that survive the file exactly: UVs in float32, colours in bytes, mask in 1/65535 steps.
    loop_uv = rng.random((len(loop_vert), 2)).astype(np.float32).astype(np.float64)
    colors = rng.integers(0, 256, (len(co), 3)) / 255.0
    mask = rng.integers(0, 65536, len(co)) / 65535.0
    polygroups = rng.integers(0, 5, len(starts))
    return {"positions": co, "faces": faces, "uvs": loop_uv[np.maximum(loops, 0)], "colors": colors,
            "mask": mask, "groups": polygroups[face_poly]}, loops


# ---------------------------------------------------------------------------- unit tests


def test_polygon_corners():
    co, starts, totals, loop_vert = grid_with_ngon()
    loops, face_poly = exchange.polygon_corners(starts, totals)
    check(len(loops) == 9 + 3 + 1, "9 quads, a pentagon as 3 triangles, 1 triangle")
    check(list(loops[9]) == [36, 37, 38, -1] and list(loops[10]) == [36, 38, 39, -1] and list(loops[11]) == [36, 39, 40, -1],
          "pentagon fan (0, i, i+1)")
    check(list(face_poly[9:13]) == [9, 9, 9, 10], "fan faces point at their polygon")
    m, _ = exchange.polygon_corners(starts, totals, mirrored=True)
    check(list(m[0]) == [0, 3, 2, 1], "mirrored quad reversed")
    check(list(m[12]) == [41, 43, 42, -1], "mirrored triangle reversed")
    # Mirrored fan = fan of the reversed polygon (0, 4, 3, 2, 1).
    check([list(r) for r in m[9:12]] == [[36, 40, 39, -1], [36, 39, 38, -1], [36, 38, 37, -1]], "mirrored pentagon fan")


def test_canonical_and_reverse():
    f = np.array([[5, 2, 7, 1], [4, 9, 3, N]], dtype=np.uint32)
    c, r = exchange.canonical_faces(f)
    check(c.tolist() == [[1, 5, 2, 7], [3, 4, 9, N]] and r.tolist() == [3, 2], "canonical rotation keeps winding")
    rev, data = exchange.reverse_faces(f, np.arange(8).reshape(2, 4))
    check(rev.tolist() == [[5, 1, 7, 2], [4, 3, 9, N]], "reverse_faces")
    check(data.tolist() == [[0, 3, 2, 1], [4, 6, 5, 7]], "reverse_faces moves corner data along")
    check(exchange.same_topology(f, np.roll(f[:1], 1, axis=1).tolist() + [[9, 3, 4, N]]), "rotated faces are the same")
    check(not exchange.same_topology(f, rev), "reversed faces are not")
    rotated = np.array([[7, 1, 5, 2], [3, 4, 9, N]], dtype=np.uint32)
    cm = exchange.corner_map(f, rotated)
    check(all(f[i, s] == rotated[i, cm[i, s]] for i in range(2) for s in range(3)) and f[0, 3] == rotated[0, cm[0, 3]],
          "corner_map finds each corner's vertex")


def test_goz_round_trip(tmp):
    arrays, _ = object_arrays(grid_with_ngon(), False, 1)
    path = os.path.join(tmp, "t.GoZ")
    exchange.write_goz(path, "Grid Ünïcode.001", **arrays)
    g = exchange.read_goz(path)
    check(g.name == "Grid_n_code_001", "GoZ name escaped like GoB (got %r)" % g.name)
    check(np.allclose(g.positions, arrays["positions"], atol=1e-6), "GoZ positions")
    check(np.array_equal(g.faces, arrays["faces"]), "GoZ faces")
    tri = arrays["faces"][:, 3] == N
    check(np.allclose(g.uvs[~tri], arrays["uvs"][~tri]) and np.allclose(g.uvs[tri, :3], arrays["uvs"][tri, :3]), "GoZ UVs")
    check(np.allclose(g.colors, arrays["colors"]), "GoZ polypaint")
    check(np.allclose(g.mask, arrays["mask"], atol=1e-9), "GoZ mask")
    check(np.array_equal(g.groups, arrays["groups"]), "GoZ polygroups")
    with open(path, "rb") as f:
        head = f.read(64)
    check(head.startswith(b"GoZb 1.0 classic GoZ Binary......"), "GoZ header as GoB writes it")


def test_manifest(tmp):
    folder = os.path.join(tmp, "m")
    check(exchange.revision(folder) == -1, "no manifest yet")
    r1 = exchange.write_manifest(folder, [{"name": "A", "file": "r1_000.GoZ"}])
    open(os.path.join(folder, "r1_000.GoZ"), "wb").close()
    check(exchange.next_file_prefix(folder) == "r2_", "next prefix")
    open(os.path.join(folder, "r2_000.GoZ"), "wb").close()
    r2 = exchange.write_manifest(folder, [{"name": "A", "file": "r2_000.GoZ"}])
    check((r1, r2) == (1, 2), "revisions count up")
    check(not os.path.exists(os.path.join(folder, "r1_000.GoZ")), "old files removed")
    check(os.path.exists(os.path.join(folder, "r2_000.GoZ")), "current files kept")
    with open(exchange.manifest_path(folder), "w") as f:
        f.write('{"format": "sculptforge-bridge", "objects": [{"name": "x", "file": "../evil"}]}')
    try:
        exchange.read_manifest(folder)
        check(False, "a manifest naming ../evil is refused")
    except ValueError:
        pass


# ---------------------------------------------------------------------------- through SculptForge


def test_round_trip_through_sculptforge(tmp, cli):
    root = os.path.join(tmp, "exchange")
    out = exchange.outbox(root)
    os.makedirs(out)
    rot = rotation((1.0, 2.0, 0.5), 37.0)
    objects = [
        ("Grid", grid_with_ngon(), matrix(rot, (1.0, 1.0, 1.0), (0.3, -1.2, 2.0))),
        ("Grid.scaled", grid_with_ngon(), matrix(rot.T, (2.0, 0.5, 3.0), (-4.0, 1.0, 0.0))),
        ("Mirrored", grid_with_ngon(), matrix(np.eye(3), (-1.0, 1.0, 1.0), (1.0, 1.0, 1.0))),
        ("Hexagon ünïcode", only_ngons(), matrix(rotation((0, 0, 1), 90), (1, 1, 1), (0, 0, 5))),
    ]
    sent = {}
    entries = []
    for k, (name, mesh, m) in enumerate(objects):
        mirrored = np.linalg.det(m[:3, :3]) < 0
        arrays, _ = object_arrays(mesh, mirrored, k + 10)
        file = "r1_%03d.GoZ" % k
        exchange.write_goz(os.path.join(out, file), name, **arrays)
        entries.append({"name": name, "file": file, "matrix_world": m.tolist()})
        sent[name] = (mesh, m, arrays)
    exchange.write_manifest(out, entries, "test")

    r = subprocess.run([cli, "-c", 'blender_receive folder="%s"; blender_send folder="%s"' % (root, root)],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    check(r.returncode == 0, "sculptforge-cli ran (%s)" % r.stderr.strip())
    if r.returncode:
        return
    manifest = exchange.read_manifest(exchange.inbox(root))
    names = [o["name"] for o in manifest["objects"]]
    check(names == [o[0] for o in objects], "every object comes back under its Blender name (got %s)" % names)
    for entry in manifest["objects"]:
        name = entry["name"]
        if name not in sent:
            continue
        mesh, m, arrays = sent[name]
        g = exchange.read_goz(os.path.join(exchange.inbox(root), entry["file"]))
        # What the add-on does on receive (update_object): into object space, and the faces compared with the
        # ones it sent (reversed for a mirrored object).
        local = exchange.transform_points(np.linalg.inv(m), g.positions)
        mirrored = np.linalg.det(m[:3, :3]) < 0
        co, starts, totals, loop_vert = mesh
        loops, face_poly = exchange.polygon_corners(starts, totals, mirrored)
        expect = exchange.faces_from_loops(loops, loop_vert)
        same = exchange.same_topology(expect, g.faces)
        check(same, "%s: same faces and winding" % name)
        err = np.abs(local - co).max()
        check(err < 1e-5, "%s: positions back in object space (max error %.2g)" % (name, err))
        # World placement in SculptForge's file: Blender world space.
        world = exchange.transform_points(m, co)
        check(np.abs(g.positions - world).max() < 1e-5, "%s: placed in the scene as in Blender" % name)
        # Placed with a negative scale, the reversed faces wind outwards again: the normals SculptForge sees
        # point the way Blender's do (here: the grid's first quad, whose Blender normal is +z before the matrix).
        f0 = g.faces[0]
        n = np.cross(g.positions[f0[1]] - g.positions[f0[0]], g.positions[f0[2]] - g.positions[f0[0]])
        p = co[mesh[3][mesh[1][0]:mesh[1][0] + 3]]
        nb = np.cross(p[1] - p[0], p[2] - p[0])
        expect_n = np.linalg.inv(m[:3, :3]).T @ nb
        check(np.dot(n, expect_n) > 0, "%s: winding faces outwards in SculptForge" % name)
        if not same:
            continue
        cm = exchange.corner_map(expect, g.faces)
        # Per loop: the UV that was sent and the one that came back.
        loop_uv_sent = np.zeros((len(loop_vert), 2))
        loop_uv_sent[loops[loops >= 0]] = arrays["uvs"][loops >= 0]
        back = np.take_along_axis(g.uvs, cm[:, :, None].repeat(2, axis=2), axis=1)
        loop_uv_back = np.zeros_like(loop_uv_sent)
        loop_uv_back[loops[loops >= 0]] = back[loops >= 0]
        check(np.allclose(loop_uv_back, loop_uv_sent, atol=1e-6), "%s: UVs" % name)
        check(g.colors is not None and np.allclose(g.colors, arrays["colors"]), "%s: polypaint" % name)
        check(g.mask is not None and np.allclose(g.mask, arrays["mask"], atol=1e-9), "%s: mask" % name)
        check(g.groups is not None and np.array_equal(g.groups, arrays["groups"]), "%s: polygroups" % name)

    # A second send from SculptForge: the next revision, the previous files gone.
    r = subprocess.run([cli, "-c", 'blender_receive folder="%s"; blender_send folder="%s"' % (root, root)],
                       capture_output=True, text=True)
    check(exchange.revision(exchange.inbox(root)) == 2, "SculptForge's revision counts up")
    left = sorted(f for f in os.listdir(exchange.inbox(root)) if f.startswith("r1_"))
    check(not left, "SculptForge removes its earlier files (left: %s)" % left)


def test_shapes_through_sculptforge(tmp, cli):
    """Sculpt layers (here the 52 ARKit facial shapes) travel as a shapes file beside the .GoZ, which holds the base
    shape: read_shapes gives the names in order and offsets in Blender's axes (jawOpen drops the chin: -Z)."""
    root = os.path.join(tmp, "shapes")
    r = subprocess.run([cli, "-c", 'new_head; face_shapes_generate; blender_send folder="%s"' % root],
                       capture_output=True, text=True)
    check(r.returncode == 0, "sculptforge-cli new_head; face_shapes_generate; blender_send (%s)" % r.stderr.strip())
    if r.returncode:
        return
    folder = exchange.inbox(root)  # (the add-on's inbox: what SculptForge sent)
    manifest = exchange.read_manifest(folder)
    head = [o for o in manifest["objects"] if o["name"] == "Head"]
    check(len(head) == 1 and head[0].get("shapes"), "the Head object names its shapes file")
    if not head or not head[0].get("shapes"):
        return
    g = exchange.read_goz(os.path.join(folder, head[0]["file"]))
    nv, shapes = exchange.read_shapes(os.path.join(folder, head[0]["shapes"]))
    check(nv == len(g.positions), "shapes file vertex count matches the .GoZ")
    names = [s[0] for s in shapes]
    check(len(names) == 52 and names[0] == "browDownLeft" and names[-1] == "tongueOut" and "jawOpen" in names,
          "the 52 ARKit names, in order")
    jaw = shapes[names.index("jawOpen")]
    check(len(jaw[2]) > 100 and jaw[3][:, 2].min() < -0.01, "jawOpen moves the chin down (Blender -Z)")
    check(all(s[1] == 0.0 for s in shapes), "weights are sent (all 0 after generating)")
    eyes = [o for o in manifest["objects"] if o["name"] == "Eyes"]
    check(len(eyes) == 1 and eyes[0].get("shapes"), "the Eyes SubTool has its eyeLook shapes too")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", help="sculptforge-cli, for the round trip through SculptForge")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        test_polygon_corners()
        test_canonical_and_reverse()
        test_goz_round_trip(tmp)
        test_manifest(tmp)
        if args.cli:
            test_round_trip_through_sculptforge(tmp, args.cli)
            test_shapes_through_sculptforge(tmp, args.cli)
    if failures:
        print("%d check(s) failed" % len(failures))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
