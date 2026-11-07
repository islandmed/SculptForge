# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Steps of tools/creature-bones/build.sh:
#   build.py raw <work> [name...]   mesh each piece's distance field (surface nets) -> <work>/raw/<id>.obj
#   build.py script <work>          print the sculptforge-cli script that quad-remeshes them -> <work>/quad/<id>.obj
#   build.py pack <work> <out.obj.z> one OBJ, an `o` object per piece in set order, centred, zlib-compressed
import os, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces, sdf


def ident(name): return name.lower().replace(" ", "-")


def raw(work, names):
    os.makedirs(os.path.join(work, "raw"), exist_ok=True)
    for name, f, lo, hi, cell, mirror, _ in pieces.PIECES:
        if names and name not in names: continue
        v, q, open_edges = sdf.surface_nets(f, lo, hi, cell, os.path.join(work, "raw", ident(name) + ".obj"), name, mirror)
        print(f"{name}: {v} vertices, {q} quads, {open_edges} open edges", flush=True)
        if open_edges: sys.exit(f"{name}: the surface isn't closed")


def script(work):
    lines = []
    for name, _, _, _, _, mirror, target in pieces.PIECES:
        i = ident(name)
        lines.append(f"open_obj path={work}/raw/{i}.obj")
        lines.append(f"qremesh target={target} adaptive=0.3" + (" symmetry=x symmetry_center=0" if mirror else ""))
        lines.append(f"save_obj path={work}/quad/{i}.obj")
    print("\n".join(lines))


def read_obj(path):
    verts, faces = [], []
    for line in open(path):
        w = line.split()
        if not w: continue
        if w[0] == "v": verts.append(tuple(float(x) for x in w[1:4]))
        elif w[0] == "f": faces.append([int(x.split("/")[0]) for x in w[1:]])
    return verts, faces


def check_closed_piece(nverts, faces):
    """'' for one closed, manifold piece, else what's wrong (a piece must sculpt and divide cleanly)."""
    edges = {}
    for f in faces:
        for a in range(len(f)):
            u, v = f[a], f[(a + 1) % len(f)]
            edges[(min(u, v), max(u, v))] = edges.get((min(u, v), max(u, v)), 0) + 1
    bad = sum(1 for n in edges.values() if n != 2)
    if bad: return f"{bad} open or non-manifold edges"
    parent = list(range(nverts + 1))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for f in faces:
        for v in f[1:]: parent[find(v)] = find(f[0])
    parts = len({find(v) for f in faces for v in f})
    return f"{parts} separate pieces" if parts != 1 else ""


def pack(work, out):
    text, base, report = [], 0, []
    text.append("# SculptForge \"Creature bones\" insert set: original procedural pieces by the SculptForge project\n"
                "# (tools/creature-bones), GPL-3.0-or-later. Metres, +Y up, +Z forward.\n")
    for name, _, _, _, _, mirror, _ in pieces.PIECES:
        verts, faces = read_obj(os.path.join(work, "quad", ident(name) + ".obj"))
        problem = check_closed_piece(len(verts), faces)
        if problem: sys.exit(f"{name}: {problem}")
        lo = [min(v[k] for v in verts) for k in range(3)]
        hi = [max(v[k] for v in verts) for k in range(3)]
        c = [(lo[k] + hi[k]) / 2 for k in range(3)]
        if mirror: c[0] = 0.0  # keep the mirror plane at x = 0
        c[1] = lo[1]  # pivot at the bottom: the piece stands on y = 0
        text.append(f"o {name}\n")
        text.extend(f"v {v[0] - c[0]:.5f} {v[1] - c[1]:.5f} {v[2] - c[2]:.5f}\n" for v in verts)
        text.extend("f " + " ".join(str(i + base) for i in f) + "\n" for f in faces)
        base += len(verts)
        quads = sum(1 for f in faces if len(f) == 4)
        report.append(f"{name}: {len(verts)} vertices, {len(faces)} faces ({quads} quads), "
                      f"{hi[0] - lo[0]:.2f} x {hi[1] - lo[1]:.2f} x {hi[2] - lo[2]:.2f} m")
    data = "".join(text).encode()
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f: f.write(zlib.compress(data, 9))
    print("\n".join(report))
    print(f"{out}: {len(data) / 1e6:.2f} MB OBJ, {os.path.getsize(out) / 1e6:.2f} MB compressed")


if __name__ == "__main__":
    step, work = sys.argv[1], os.path.abspath(sys.argv[2])
    if step == "raw": raw(work, sys.argv[3:])
    elif step == "script": script(work)
    elif step == "pack": pack(work, sys.argv[3])
    else: sys.exit(__doc__)
