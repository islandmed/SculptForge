# SculptForge bridge for Blender - Gaussian splats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Everything here works without bpy (tests/blender/test_exchange.py runs it with plain Python and numpy).
#
# Splats travel as standard 3D Gaussian Splatting PLY files, the layout SculptForge, Blender 5.3's importer and
# most capture tools read and write: positions, f_dc_* (the band-0 spherical harmonic), f_rest_* (the other bands,
# one colour channel after the other), logit opacity, log scales and a w, x, y, z rotation. Blender 5.3 holds the
# same values decoded: linear opacity and scale, a quaternion, "radiance:base" (f_dc and opacity) and one
# "radiance:sh_<k>" per further coefficient (the k-th f_rest of each channel). SplatArrays is that decoded form.

import os

import numpy as np

SH_C0 = 0.28209479177387814  # the band-0 spherical harmonic


class SplatArrays:
    """Gaussian splats as arrays: positions (N, 3), rotations (N, 4, w x y z, unit), scales (N, 3, linear),
    opacities (N,, 0..1), sh (N, K, 3) with the band-0 coefficient first (K = (degree + 1) ** 2)."""

    def __init__(self, positions, rotations, scales, opacities, sh):
        self.positions = np.asarray(positions, dtype=np.float32).reshape(-1, 3)
        n = len(self.positions)
        self.rotations = np.asarray(rotations, dtype=np.float32).reshape(n, 4)
        self.scales = np.asarray(scales, dtype=np.float32).reshape(n, 3)
        self.opacities = np.asarray(opacities, dtype=np.float32).reshape(n)
        self.sh = np.asarray(sh, dtype=np.float32).reshape(n, -1, 3)

    def __len__(self):
        return len(self.positions)

    @property
    def degree(self):
        return degree_for_coefficients(self.sh.shape[1])

    def colors(self):
        """The view-independent colour, sRGB 0..1."""
        return 0.5 + SH_C0 * self.sh[:, 0, :]


def degree_for_coefficients(k):
    """SH degree of k coefficients per channel (1, 4, 9, 16), or -1 when k is no such count."""
    d = int(round(np.sqrt(k))) - 1
    return d if d >= 0 and (d + 1) ** 2 == k else -1


def normalized_rotations(q):
    q = np.asarray(q, dtype=np.float64).reshape(-1, 4)
    length = np.linalg.norm(q, axis=1, keepdims=True)
    unit = np.tile(np.array([1.0, 0.0, 0.0, 0.0]), (len(q), 1))
    return np.where(length > 1e-12, q / np.maximum(length, 1e-12), unit)


# --------------------------------------------------------------------------------------------------
# PLY
# --------------------------------------------------------------------------------------------------


def ply_bytes(a):
    """The 3DGS PLY file of `a` (binary little endian)."""
    k = a.sh.shape[1]
    per_channel = k - 1
    names = ["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"]
    names += ["f_rest_%d" % i for i in range(3 * per_channel)]
    names += ["opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"]
    header = "ply\nformat binary_little_endian 1.0\ncomment Written by the SculptForge add-on for Blender\n"
    header += "element vertex %d\n" % len(a) + "".join("property float %s\n" % n for n in names) + "end_header\n"
    n = len(a)
    out = np.zeros((n, len(names)), dtype="<f4")
    out[:, 0:3] = a.positions
    out[:, 6:9] = a.sh[:, 0, :]
    if per_channel:
        # f_rest: all of red's coefficients, then green's, then blue's.
        out[:, 9:9 + 3 * per_channel] = np.transpose(a.sh[:, 1:, :], (0, 2, 1)).reshape(n, 3 * per_channel)
    o = 9 + 3 * per_channel
    p = np.clip(a.opacities.astype(np.float64), 1e-6, 1.0 - 1e-6)
    out[:, o] = np.log(p / (1.0 - p))
    out[:, o + 1:o + 4] = np.log(np.maximum(a.scales.astype(np.float64), 1e-12))
    out[:, o + 4:o + 8] = normalized_rotations(a.rotations)
    return header.encode("ascii") + out.tobytes()


def write_ply(path, a):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(ply_bytes(a))
    os.replace(tmp, path)


_PLY_TYPES = {"char": "i1", "int8": "i1", "uchar": "u1", "uint8": "u1", "short": "<i2", "int16": "<i2",
              "ushort": "<u2", "uint16": "<u2", "int": "<i4", "int32": "<i4", "uint": "<u4", "uint32": "<u4",
              "float": "<f4", "float32": "<f4", "double": "<f8", "float64": "<f8"}


def read_ply(path):
    """A binary little endian 3DGS PLY (SculptForge's, Blender's importer's, most capture tools') as SplatArrays.
    Raises ValueError when the file is not one."""
    with open(path, "rb") as f:
        data = f.read()
    end = data.find(b"end_header\n")
    if not data.startswith(b"ply") or end < 0:
        raise ValueError("Not a PLY file: %s" % path)
    lines = data[:end].decode("ascii", "replace").split("\n")
    if "format binary_little_endian 1.0" not in lines:
        raise ValueError("Only binary little endian PLY splat files are read: %s" % path)
    count, fields, in_vertex = 0, [], False
    for line in lines:
        words = line.split()
        if words[:1] == ["element"]:
            in_vertex = words[1] == "vertex"
            if in_vertex:
                count = int(words[2])
            elif int(words[2]) > 0:
                raise ValueError("The PLY file has %s data: it is not a splat file" % words[1])
        elif in_vertex and words[:1] == ["property"]:
            if words[1] == "list" or words[1] not in _PLY_TYPES:
                raise ValueError("The PLY file has a property type that splats do not use: %s" % line)
            fields.append((words[2], _PLY_TYPES[words[1]]))
    table = np.frombuffer(data, dtype=np.dtype(fields), count=count, offset=end + len(b"end_header\n"))
    names = set(table.dtype.names)
    need = {"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2",
            "rot_0", "rot_1", "rot_2", "rot_3"}
    if not need <= names:
        raise ValueError("Not a Gaussian splat PLY file (missing %s)" % ", ".join(sorted(need - names)))

    def col(name):
        return table[name].astype(np.float64)

    n = count
    rest = 0
    while "f_rest_%d" % rest in names:
        rest += 1
    per_channel = rest // 3 if rest % 3 == 0 and degree_for_coefficients(rest // 3 + 1) >= 0 else 0
    sh = np.zeros((n, per_channel + 1, 3))
    for c in range(3):
        sh[:, 0, c] = col("f_dc_%d" % c)
        for i in range(per_channel):
            sh[:, 1 + i, c] = col("f_rest_%d" % (c * per_channel + i))
    opacity = 1.0 / (1.0 + np.exp(-col("opacity")))
    scales = np.exp(np.stack([col("scale_0"), col("scale_1"), col("scale_2")], axis=1))
    rotations = normalized_rotations(np.stack([col("rot_%d" % i) for i in range(4)], axis=1))
    positions = np.stack([col("x"), col("y"), col("z")], axis=1)
    return SplatArrays(positions, rotations, scales, opacity, sh)


# --------------------------------------------------------------------------------------------------
# The compact .splat format (32 bytes per splat, used by web viewers)
# --------------------------------------------------------------------------------------------------


def read_dot_splat(path):
    """A .splat file: position and linear scale as float32, RGBA as bytes, rotation as bytes (w x y z, 128 = 0)."""
    with open(path, "rb") as f:
        data = f.read()
    if not data or len(data) % 32:
        raise ValueError("Not a .splat file (its size must be a multiple of 32 bytes): %s" % path)
    n = len(data) // 32
    rec = np.frombuffer(data, dtype=np.uint8).reshape(n, 32)
    floats = np.frombuffer(rec[:, :24].tobytes(), dtype="<f4").reshape(n, 6)
    rgba = rec[:, 24:28].astype(np.float64) / 255.0
    q = (rec[:, 28:32].astype(np.float64) - 128.0) / 128.0
    sh = ((rgba[:, :3] - 0.5) / SH_C0).reshape(n, 1, 3)
    return SplatArrays(floats[:, :3], normalized_rotations(q), floats[:, 3:], rgba[:, 3], sh)



def rotation_matrix_to_quaternion(r):
    """Unit quaternions (w, x, y, z) of rotation matrices r (N, 3, 3) or (3, 3)."""
    r = np.asarray(r, dtype=np.float64).reshape(-1, 3, 3)
    t = r[:, 0, 0] + r[:, 1, 1] + r[:, 2, 2]
    q = np.zeros((len(r), 4))
    for i in range(len(r)):  # (one matrix in practice: an object's transform)
        m = r[i]
        if t[i] > 0:
            s = np.sqrt(t[i] + 1.0) * 2
            q[i] = [0.25 * s, (m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s]
        elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
            s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
            q[i] = [(m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s]
        elif m[1, 1] > m[2, 2]:
            s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
            q[i] = [(m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s]
        else:
            s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
            q[i] = [(m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s]
    return normalized_rotations(q)


def quaternion_multiply(a, b):
    """Hamilton product of quaternions (w, x, y, z): a (4,) or (N, 4) times b (N, 4)."""
    aw, ax, ay, az = np.moveaxis(np.broadcast_to(a, np.shape(b)), -1, 0)
    bw, bx, by, bz = np.moveaxis(np.asarray(b), -1, 0)
    return np.stack([aw * bw - ax * bx - ay * by - az * bz, aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw], axis=-1)


def sh_band(l, d):
    """The 3DGS basis functions of band l (1-3) at unit directions d (M, 3): (M, 2l + 1), in the f_rest order."""
    x, y, z = d[:, 0], d[:, 1], d[:, 2]
    xx, yy, zz = x * x, y * y, z * z
    if l == 1:
        c = 0.4886025119029199
        return np.stack([-c * y, c * z, -c * x], axis=1)
    if l == 2:
        return np.stack([1.0925484305920792 * x * y, -1.0925484305920792 * y * z, 0.31539156525252005 * (2 * zz - xx - yy),
                         -1.0925484305920792 * x * z, 0.5462742152960396 * (xx - yy)], axis=1)
    return np.stack([-0.5900435899266435 * y * (3 * xx - yy), 2.890611442640554 * x * y * z,
                     -0.4570457994644658 * y * (4 * zz - xx - yy), 0.3731763325901154 * z * (2 * zz - 3 * xx - 3 * yy),
                     -0.4570457994644658 * x * (4 * zz - xx - yy), 1.445305721320277 * z * (xx - yy),
                     -0.5900435899266435 * x * (xx - 3 * yy)], axis=1)


def eval_sh(sh, d):
    """View-dependent colour (unclamped, 0.5 + the sum) of coefficients sh (N, K, 3) seen along unit directions d (N, 3)."""
    c = 0.5 + SH_C0 * sh[:, 0, :]
    at = 1
    for l in range(1, degree_for_coefficients(sh.shape[1]) + 1):
        c = c + np.einsum("nj,njc->nc", sh_band(l, d), sh[:, at:at + 2 * l + 1, :])
        at += 2 * l + 1
    return c


def _fibonacci_sphere(m=32):
    k = np.arange(m) + 0.5
    z = 1.0 - 2.0 * k / m
    r = np.sqrt(1.0 - z * z)
    a = 2.399963229728653 * (k - 0.5)
    return np.stack([r * np.cos(a), r * np.sin(a), z], axis=1)


def rotated_sh(sh, rot):
    """Coefficients sh (N, K, 3) of splats turned by the 3x3 rotation rot: what was seen from d is seen from rot @ d.
    Per band, the exact matrix by least squares on 32 directions (a turned band-l function is a band-l function);
    the same as SculptForge's ShRotation (core/Splats.cpp)."""
    sh = np.asarray(sh, dtype=np.float64)
    out = sh.copy()
    dirs = _fibonacci_sphere()
    back = dirs @ np.asarray(rot, dtype=np.float64)  # rot^T d, each row
    at = 1
    for l in range(1, degree_for_coefficients(sh.shape[1]) + 1):
        n = 2 * l + 1
        m = np.linalg.lstsq(sh_band(l, dirs), sh_band(l, back), rcond=None)[0]  # (n, n)
        out[:, at:at + n, :] = np.einsum("ij,njc->nic", m, sh[:, at:at + n, :])
        at += n
    return out.astype(np.float32)


def baked(a, matrix):
    """`a` placed by a 4x4 matrix (rows as Blender prints it): positions moved, rotations turned, scales scaled by
    the matrix's uniform scale (the cube root of its determinant), and the view-dependent colour (spherical harmonics
    above band 0) turned with them."""
    m = np.asarray(matrix, dtype=np.float64)
    lin = m[:3, :3]
    scale = np.cbrt(abs(np.linalg.det(lin)))
    rot = lin / scale if scale > 1e-12 else np.eye(3)
    q = rotation_matrix_to_quaternion(rot)[0]
    sh = a.sh
    if abs(abs(q[0]) - 1.0) > 1e-7 and sh.shape[1] > 1:
        sh = rotated_sh(sh, quaternion_to_matrix(q))
    return SplatArrays(a.positions.astype(np.float64) @ lin.T + m[:3, 3], quaternion_multiply(q, a.rotations),
                       a.scales * scale, a.opacities, sh)


def quaternion_to_matrix(q):
    """The 3x3 rotation of a unit quaternion (w, x, y, z)."""
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
