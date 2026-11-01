# SculptForge - ComfyUI nodes: converting ComfyUI MESH values to and from mesh files.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ComfyUI's core 3D nodes (Hunyuan3D VAE decode -> VoxelToMesh, TRELLIS, Get 3D Components,
# SaveGLB...) pass comfy_api.latest.Types.MESH: vertices (B, N, 3) float, faces (B, M, 3) int,
# optional vertex_colors (B, N, 3|4) in 0-1 and normals (B, N, 3), with vertex_counts /
# face_counts (B,) when a batch is zero-padded. Coordinates are glTF-style (Y up), the same
# convention SculptForge uses, so no axis swap is needed. Meshes cross the process boundary as
# binary PLY, which SculptForge reads and writes with vertex colours (vertex paint).
import struct

import numpy as np

_PLY_TYPES = {
    "char": "i1", "int8": "i1", "uchar": "u1", "uint8": "u1",
    "short": "i2", "int16": "i2", "ushort": "u2", "uint16": "u2",
    "int": "i4", "int32": "i4", "uint": "u4", "uint32": "u4",
    "float": "f4", "float32": "f4", "double": "f8", "float64": "f8",
}


def write_ply(path, vertices, faces, colors=None):
    """Write a binary little-endian PLY. colors: (N, 3|4) floats in 0-1, or None."""
    vertices = np.ascontiguousarray(vertices, dtype="<f4").reshape(-1, 3)
    faces = np.ascontiguousarray(faces, dtype="<i4").reshape(-1, 3)
    if len(faces) and (faces.min() < 0 or faces.max() >= len(vertices)):
        raise ValueError(f"face index out of range for {len(vertices)} vertices")
    fields = [("x", "<f4"), ("y", "<f4"), ("z", "<f4")]
    header = ["ply", "format binary_little_endian 1.0", "comment Written by ComfyUI-SculptForge",
              f"element vertex {len(vertices)}", "property float x", "property float y", "property float z"]
    if colors is not None:
        fields += [("red", "u1"), ("green", "u1"), ("blue", "u1")]
        header += ["property uchar red", "property uchar green", "property uchar blue"]
    header += [f"element face {len(faces)}", "property list uchar int vertex_indices", "end_header"]

    vert_data = np.empty(len(vertices), dtype=fields)
    vert_data["x"], vert_data["y"], vert_data["z"] = vertices[:, 0], vertices[:, 1], vertices[:, 2]
    if colors is not None:
        rgb = np.clip(np.asarray(colors, dtype=np.float32)[:, :3] * 255.0 + 0.5, 0, 255).astype("u1")
        vert_data["red"], vert_data["green"], vert_data["blue"] = rgb[:, 0], rgb[:, 1], rgb[:, 2]
    face_data = np.empty(len(faces), dtype=[("n", "u1"), ("i", "<i4", (3,))])
    face_data["n"] = 3
    face_data["i"] = faces

    with open(path, "wb") as f:
        f.write(("\n".join(header) + "\n").encode("ascii"))
        f.write(vert_data.tobytes())
        f.write(face_data.tobytes())


def read_ply(path):
    """Read a binary little-endian PLY mesh as dict(vertices, faces, normals, colors)."""
    with open(path, "rb") as f:
        data = f.read()
    end = data.find(b"end_header")
    if not data.startswith(b"ply") or end < 0:
        raise ValueError(f"{path} is not a PLY file")
    body = data.index(b"\n", end) + 1
    elements = []  # (name, count, [(prop, dtype)] or ("list", count_dtype, index_dtype))
    for line in data[:end].decode("ascii", "replace").splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "format" and parts[1] != "binary_little_endian":
            raise ValueError(f"{path}: only binary little-endian PLY is supported, not {parts[1]}")
        if parts[0] == "element":
            elements.append((parts[1], int(parts[2]), []))
        elif parts[0] == "property":
            if parts[1] == "list":
                elements[-1][2].append((parts[4], ("list", _PLY_TYPES[parts[2]], _PLY_TYPES[parts[3]])))
            else:
                elements[-1][2].append((parts[2], _PLY_TYPES[parts[1]]))

    offset = body
    vertex = faces = None
    for name, count, props in elements:
        lists = [p for p in props if isinstance(p[1], tuple)]
        if not lists:
            dtype = np.dtype([(p, "<" + t) for p, t in props])
            arr = np.frombuffer(data, dtype=dtype, count=count, offset=offset)
            offset += dtype.itemsize * count
            if name == "vertex":
                vertex = arr
            continue
        if name != "face" or len(props) != 1:
            raise ValueError(f"{path}: unsupported PLY element '{name}'")
        count_t, index_t = lists[0][1][1], lists[0][1][2]
        faces, offset = _read_faces(data, offset, count, "<" + count_t, "<" + index_t)

    if vertex is None or faces is None:
        raise ValueError(f"{path}: PLY has no vertex/face elements")
    names = vertex.dtype.names
    out = {
        "vertices": np.stack([vertex["x"], vertex["y"], vertex["z"]], axis=1).astype(np.float32),
        "faces": faces,
        "normals": None,
        "colors": None,
    }
    if all(n in names for n in ("nx", "ny", "nz")):
        out["normals"] = np.stack([vertex["nx"], vertex["ny"], vertex["nz"]], axis=1).astype(np.float32)
    if all(n in names for n in ("red", "green", "blue")):
        rgb = np.stack([vertex["red"], vertex["green"], vertex["blue"]], axis=1).astype(np.float32)
        if vertex.dtype["red"].kind in "ui":
            rgb /= float(np.iinfo(vertex.dtype["red"]).max)
        out["colors"] = rgb
    return out


def _read_faces(data, offset, count, count_t, index_t):
    """Faces of a PLY list property, triangulated as fans. Fast path when all are triangles."""
    tri = np.dtype([("n", count_t), ("i", index_t, (3,))])
    if count == 0:
        return np.zeros((0, 3), np.int64), offset
    if offset + tri.itemsize * count <= len(data):
        arr = np.frombuffer(data, dtype=tri, count=count, offset=offset)
        if np.all(arr["n"] == 3):
            return arr["i"].astype(np.int64), offset + tri.itemsize * count
    cs, isz = np.dtype(count_t), np.dtype(index_t)
    out = []
    for _ in range(count):
        n = int(np.frombuffer(data, dtype=cs, count=1, offset=offset)[0])
        offset += cs.itemsize
        idx = np.frombuffer(data, dtype=isz, count=n, offset=offset)
        offset += isz.itemsize * n
        for k in range(1, n - 1):
            out.append((idx[0], idx[k], idx[k + 1]))
    return np.asarray(out, dtype=np.int64).reshape(-1, 3), offset


# ---------------------------------------------------------------- ComfyUI MESH


def is_comfy_mesh(value):
    return hasattr(value, "vertices") and hasattr(value, "faces") and not isinstance(value, (str, bytes))


def _np(t):
    if t is None:
        return None
    if hasattr(t, "detach"):
        t = t.detach().float().cpu() if t.is_floating_point() else t.detach().cpu()
        return t.numpy()
    return np.asarray(t)


def comfy_mesh_item(mesh, index=0):
    """(vertices, faces, colors) numpy arrays of one item of a ComfyUI MESH batch."""
    vertices, faces, colors = mesh.vertices[index], mesh.faces[index], getattr(mesh, "vertex_colors", None)
    colors = colors[index] if colors is not None else None
    counts = getattr(mesh, "vertex_counts", None)
    if counts is not None:
        nv, nf = int(mesh.vertex_counts[index]), int(mesh.face_counts[index])
        vertices, faces = vertices[:nv], faces[:nf]
        colors = colors[:nv] if colors is not None else None
    return _np(vertices), _np(faces), _np(colors)


def comfy_mesh_to_ply(mesh, path, index=0):
    vertices, faces, colors = comfy_mesh_item(mesh, index)
    if vertices is None or len(vertices) == 0 or len(faces) == 0:
        raise ValueError("the MESH is empty (no vertices or faces)")
    write_ply(path, vertices, faces, colors)


class _FallbackMesh:
    """Stand-in with Types.MESH's attributes, for ComfyUI versions without comfy_api.latest."""

    def __init__(self, vertices, faces, vertex_colors=None, normals=None):
        self.vertices, self.faces = vertices, faces
        self.vertex_colors, self.normals = vertex_colors, normals
        self.uvs = self.texture = self.metallic_roughness = None
        self.vertex_counts = self.face_counts = None
        self.tangents = self.normal_map = self.material = self.emissive = None
        self.unlit = self.occlusion_in_mr = False


def mesh_class():
    try:
        from comfy_api.latest import Types
        return Types.MESH
    except Exception:
        return _FallbackMesh


def ply_to_comfy_mesh(path):
    """Read a PLY written by SculptForge into a ComfyUI MESH (batch of one)."""
    import torch

    m = read_ply(path)
    batch = lambda a: None if a is None else torch.from_numpy(np.ascontiguousarray(a))[None]
    cls = mesh_class()
    vertices, faces = batch(m["vertices"]), batch(m["faces"])
    try:
        return cls(vertices=vertices, faces=faces, vertex_colors=batch(m["colors"]), normals=batch(m["normals"]))
    except TypeError:  # older ComfyUI: MESH(vertices, faces) only
        return cls(vertices, faces)


def glb_bytes_magic(path):
    """True if the file starts like a binary glTF (sanity check for exported GLBs)."""
    with open(path, "rb") as f:
        head = f.read(12)
    return len(head) == 12 and head[:4] == b"glTF" and struct.unpack("<I", head[4:8])[0] == 2
