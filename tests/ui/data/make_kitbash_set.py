#!/usr/bin/env python3
# Writes kitbash_set.glb for insert_set.sfui: an insert set of two objects, "Stud" (an octahedron, 8 triangles)
# and "Wedge" (a tetrahedron, 4 triangles), so the test can tell which one a drag inserted.
import json, os, struct

objects = [
    ("Stud", [(0, 1, 0), (1, 0, 0), (0, 0, 1), (-1, 0, 0), (0, 0, -1), (0, -1, 0)],
     [(0, 2, 1), (0, 3, 2), (0, 4, 3), (0, 1, 4), (5, 1, 2), (5, 2, 3), (5, 3, 4), (5, 4, 1)]),
    ("Wedge", [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)], [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]),
]
binary, views, accessors, meshes, nodes = b"", [], [], [], []
for i, (name, verts, tris) in enumerate(objects):
    pos = b"".join(struct.pack("<3f", *v) for v in verts)
    idx = b"".join(struct.pack("<3H", *t) for t in tris)
    idx += b"\0" * (-len(idx) % 4)
    for data, target in ((pos, 34962), (idx, 34963)):
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data), "target": target})
        binary += data
    lo = [min(v[k] for v in verts) for k in range(3)]
    hi = [max(v[k] for v in verts) for k in range(3)]
    accessors.append({"bufferView": 2 * i, "componentType": 5126, "count": len(verts), "type": "VEC3", "min": lo, "max": hi})
    accessors.append({"bufferView": 2 * i + 1, "componentType": 5123, "count": 3 * len(tris), "type": "SCALAR"})
    meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": 2 * i}, "indices": 2 * i + 1}]})
    nodes.append({"name": name, "mesh": i, "translation": [3.0 * i, 0, 0]})
doc = {"asset": {"version": "2.0", "generator": "make_kitbash_set.py"}, "scene": 0,
       "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes, "accessors": accessors,
       "bufferViews": views, "buffers": [{"byteLength": len(binary)}]}
js = json.dumps(doc, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
out = struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary))
out += struct.pack("<2I", len(js), 0x4E4F534A) + js + struct.pack("<2I", len(binary), 0x004E4942) + binary
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "kitbash_set.glb"), "wb") as f:
    f.write(out)
