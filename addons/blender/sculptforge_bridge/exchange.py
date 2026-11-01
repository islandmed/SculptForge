# SculptForge bridge for Blender - exchange with SculptForge
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Everything here works without bpy, so it can be tested with plain Python and numpy
# (tests/blender/test_exchange.py). The add-on (__init__.py) moves data between these arrays and
# Blender meshes.
#
# The two programs share a per-user folder (docs/blender.md):
#   <root>/to_sculptforge/manifest.json + r<rev>_<k>.GoZ   written here
#   <root>/to_blender/manifest.json     + r<rev>_<k>.GoZ   written by SculptForge
#   <root>/app.json    where SculptForge is installed (it writes this when it starts)
#   <root>/alive.json  SculptForge's heartbeat (pid, time), rewritten every 2 s while it runs
# Meshes travel as ZBrush .GoZ files, which SculptForge reads and writes already. The .GoZ layout and
# the axis convention follow GoB (github.com/JoseConseco/GoB, GPL), the Blender GoZ add-on by Daniel
# Grauer, JoseConseco and contributors: many thanks to them.

import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import time

import numpy as np

FORMAT = "sculptforge-bridge"
VERSION = 1
NO_INDEX = 0xFFFFFFFF
HEARTBEAT_SECONDS = 6.0  # SculptForge rewrites alive.json every 2 s

# --------------------------------------------------------------------------------------------------
# Folder and manifest
# --------------------------------------------------------------------------------------------------


def default_root():
    """The exchange folder SculptForge uses: <its user data folder>/blender."""
    custom = os.environ.get("SCULPTFORGE_BLENDER_EXCHANGE")
    if custom:
        return custom
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Roaming")
        return os.path.join(base, "SculptForge", "SculptForge", "blender")
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/SculptForge/SculptForge/blender")
    xdg = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(xdg, "SculptForge", "SculptForge", "blender")


def inbox(root):
    """Where SculptForge writes for Blender."""
    return os.path.join(root, "to_blender")


def outbox(root):
    """Where Blender writes for SculptForge."""
    return os.path.join(root, "to_sculptforge")


def manifest_path(folder):
    return os.path.join(folder, "manifest.json")


def file_time(path):
    try:
        return os.stat(path).st_mtime_ns
    except OSError:
        return 0


def read_manifest(folder):
    """The manifest in `folder` as a dict, or None when there is none. Raises ValueError when it is bad."""
    try:
        with open(manifest_path(folder), "rb") as f:
            data = json.loads(f.read().decode("utf-8"))
    except FileNotFoundError:
        return None
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as e:
        raise ValueError("Cannot read the bridge manifest: %s" % e)
    if not isinstance(data, dict) or data.get("format") != FORMAT:
        raise ValueError("Not a SculptForge bridge manifest")
    if data.get("version", 1) > VERSION:
        raise ValueError("The manifest is from a newer SculptForge; update the add-on")
    for obj in data.get("objects", []):
        name = obj.get("file", "")
        if not name or os.path.basename(name) != name or name in (".", ".."):
            raise ValueError("The manifest names a bad file: %r" % name)
    return data


def revision(folder):
    try:
        m = read_manifest(folder)
    except ValueError:
        return -1
    return -1 if m is None else int(m.get("revision", 0))


def _write_atomically(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def write_manifest(folder, objects, sender_version=""):
    """Writes manifest.json for `objects` (dicts with name, file and optionally matrix_world as 4 rows) with
    the next revision, then removes the files of earlier sends. Returns the revision."""
    os.makedirs(folder, exist_ok=True)
    rev = max(revision(folder), 0) + 1
    manifest = {
        "format": FORMAT,
        "version": VERSION,
        "revision": rev,
        "sender": "blender",
        "sender_version": sender_version,
        "time": time.time(),
        "objects": objects,
    }
    _write_atomically(manifest_path(folder), json.dumps(manifest, indent=2).encode("utf-8"))
    keep = {o["file"] for o in objects}
    for path in glob.glob(os.path.join(folder, "r*_*")):
        if os.path.basename(path) not in keep and not os.path.basename(path).startswith("r%d_" % rev):
            try:
                os.remove(path)
            except OSError:
                pass  # still open on Windows: the next send tries again
    return rev


def next_file_prefix(folder):
    """File name prefix of the next send (r<revision>_), so files never overwrite ones still being read."""
    return "r%d_" % (max(revision(folder), 0) + 1)


# --------------------------------------------------------------------------------------------------
# Axes: Blender is Z-up, ZBrush (GoZ) Y-down. GoB maps GoZ (x, y, z) to Blender (x, z, -y).
# SculptForge reads GoZ as Y-up (x, -y, -z), so Blender (x, y, z) arrives as (x, z, -y): metres both.
# --------------------------------------------------------------------------------------------------


def blender_to_goz(co):
    co = np.asarray(co, dtype=np.float64).reshape(-1, 3)
    return np.stack([co[:, 0], -co[:, 2], co[:, 1]], axis=1)


def goz_to_blender(co):
    co = np.asarray(co, dtype=np.float64).reshape(-1, 3)
    return np.stack([co[:, 0], co[:, 2], -co[:, 1]], axis=1)


def transform_points(matrix, co):
    """`matrix` (4x4, rows as Blender prints it) applied to points (N, 3)."""
    m = np.asarray(matrix, dtype=np.float64)
    return np.asarray(co, dtype=np.float64) @ m[:3, :3].T + m[:3, 3]


# --------------------------------------------------------------------------------------------------
# Faces: Blender polygons -> GoZ faces (triangles and quads; n-gons as triangle fans)
# --------------------------------------------------------------------------------------------------


def polygon_corners(loop_start, loop_total, mirrored=False):
    """GoZ faces for Blender polygons, as loop indices.

    Returns (loops, face_polygon): loops is (F, 4) with -1 in the 4th column of triangles, face_polygon (F,)
    the polygon each face comes from. Triangles and quads stay as they are; an n-gon becomes the fan
    (0, i, i+1). `mirrored` reverses every polygon's winding (for objects with a negative scale, whose
    normals would point inwards once SculptForge places them).
    """
    start = np.asarray(loop_start, dtype=np.int64)
    n = np.asarray(loop_total, dtype=np.int64)
    k = np.where(n <= 4, 1, n - 2)
    face_poly = np.repeat(np.arange(len(n), dtype=np.int64), k)
    first = np.cumsum(k) - k
    j = np.arange(len(face_poly), dtype=np.int64) - first[face_poly]
    nn = n[face_poly]
    small = nn <= 4
    c = np.empty((len(face_poly), 4), dtype=np.int64)
    c[:, 0] = 0
    c[:, 1] = np.where(small, 1, j + 1)
    c[:, 2] = np.where(small, 2, j + 2)
    c[:, 3] = np.where(nn == 4, 3, -1)
    if mirrored:
        c = np.where(c > 0, nn[:, None] - c, c)
    loops = np.where(c >= 0, start[face_poly][:, None] + c, -1)
    return loops, face_poly


def faces_from_loops(loops, loop_vertex):
    """Vertex indices (F, 4, uint32, NO_INDEX for the missing 4th corner of triangles)."""
    lv = np.asarray(loop_vertex, dtype=np.int64)
    out = np.where(loops >= 0, lv[np.maximum(loops, 0)], NO_INDEX)
    return out.astype(np.uint32)


def reverse_faces(faces, corner_data=None):
    """Reverses the winding of GoZ faces (a, b, c, d) -> (a, d, c, b), (a, b, c) -> (a, c, b), and the same
    for per-corner data (F, 4, ...)."""
    faces = np.asarray(faces)
    quad = faces[:, 3] != NO_INDEX
    perm = np.where(quad[:, None], np.array([0, 3, 2, 1]), np.array([0, 2, 1, 3]))
    out = np.take_along_axis(faces, perm, axis=1)
    if corner_data is None:
        return out, None
    data = np.asarray(corner_data)
    idx = perm.reshape(perm.shape + (1,) * (data.ndim - 2))
    return out, np.take_along_axis(data, np.broadcast_to(idx, data.shape), axis=1)


def canonical_faces(faces):
    """Each face rotated to start at its smallest vertex (winding kept), and the rotation used (F,)."""
    f = np.asarray(faces, dtype=np.int64)
    quad = f[:, 3] != NO_INDEX
    k = np.where(quad, 4, 3)
    valid = np.arange(4)[None, :] < k[:, None]
    r = np.argmin(np.where(valid, f, np.iinfo(np.int64).max), axis=1)
    idx = (r[:, None] + np.arange(4)[None, :]) % k[:, None]
    out = np.take_along_axis(f, idx, axis=1)
    out[~quad, 3] = NO_INDEX
    return out, r


def same_topology(sent_faces, received_faces):
    """True when SculptForge sent back the faces it was given (in order; each may start at another corner)."""
    a, b = np.asarray(sent_faces), np.asarray(received_faces)
    if a.shape != b.shape:
        return False
    return bool(np.array_equal(canonical_faces(a)[0], canonical_faces(b)[0]))


def corner_map(sent_faces, received_faces):
    """For each sent face corner, the received face's corner at the same vertex: (F, 4), for faces with the
    same topology (same_topology)."""
    _, rs = canonical_faces(sent_faces)
    _, rr = canonical_faces(received_faces)
    k = np.where(np.asarray(sent_faces)[:, 3] != NO_INDEX, 4, 3)
    s = np.arange(4)[None, :]
    return np.where(s < k[:, None], (s - rs[:, None] + rr[:, None]) % k[:, None], 3)


# --------------------------------------------------------------------------------------------------
# .GoZ files (docs/goz.md)
# --------------------------------------------------------------------------------------------------

TAG_MESH_NAME = 0x1389
TAG_VERTICES = 0x2711
TAG_FACES = 0x4E21
TAG_UVS = 0x61A9
TAG_MASK = 0x7532
TAG_POLYPAINT = 0x88B9
TAG_POLYGROUPS = 0x9C41
HEADER = b"GoZb 1.0 ZBrush GoZ Binary" + b"." * 6


def goz_name(name):
    """GoZ object names: ASCII letters, digits, '_' and '-' (GoB's escaping); the manifest keeps the real name."""
    out = ""
    for ch in name:
        if ch.isascii() and (ch.isalnum() or ch in "_-"):
            out += ch
        elif not out.endswith("_"):
            out += "_"
    out = out.strip("_-")
    return out or "Object"


def _section(tag, payload, count):
    return struct.pack("<IIQ", tag, len(payload) + 16, count) + payload


def write_goz(path, name, positions, faces, uvs=None, colors=None, mask=None, groups=None):
    """Writes a .GoZ file.

    positions (N, 3) Blender coordinates; faces (F, 4) vertex indices with NO_INDEX as the 4th of
    triangles; uvs (F, 4, 2) per face corner (V up, as Blender has it); colors (N, 3) sRGB 0..1; mask (N,)
    0 = free, 1 = masked; groups (F,) polygroup ids (0..65535).
    """
    name = goz_name(name).encode("ascii")
    pos = blender_to_goz(positions).astype("<f4")
    faces = np.ascontiguousarray(faces, dtype="<u4").reshape(-1, 4)
    parts = [HEADER, struct.pack("<IIQ", 1, len(name) + 24, 1), b"GoZMesh_", name,
             struct.pack("<IIQI", TAG_MESH_NAME, 20, 1, 0),
             _section(TAG_VERTICES, pos.tobytes(), len(pos)),
             _section(TAG_FACES, faces.tobytes(), len(faces))]
    if uvs is not None:
        uv = np.array(uvs, dtype="<f4").reshape(-1, 4, 2)
        uv[:, :, 1] = 1.0 - uv[:, :, 1]  # GoZ stores V downwards
        uv[faces[:, 3] == NO_INDEX, 3] = (0.0, 1.0)  # GoB's padding for triangles
        parts.append(_section(TAG_UVS, uv.tobytes(), len(faces)))
    if colors is not None:
        c = np.clip(np.asarray(colors, dtype=np.float64).reshape(-1, 3), 0.0, 1.0)
        bgra = np.zeros((len(c), 4), dtype=np.uint8)
        bgra[:, :3] = np.floor(c[:, ::-1] * 255.0 + 0.5).astype(np.uint8)
        parts.append(_section(TAG_POLYPAINT, bgra.tobytes(), len(c)))
    if mask is not None:
        m = np.clip(np.asarray(mask, dtype=np.float64), 0.0, 1.0)
        parts.append(_section(TAG_MASK, np.floor((1.0 - m) * 65535.0 + 0.5).astype("<u2").tobytes(), len(m)))
    if groups is not None:
        g = np.asarray(groups, dtype=np.int64)
        if len(g) and (g.min() < 0 or g.max() > 0xFFFF):  # GoZ groups are 16-bit: renumber 0, 1, 2...
            g = np.unique(g, return_inverse=True)[1] & 0xFFFF
        parts.append(_section(TAG_POLYGROUPS, g.astype("<u2").tobytes(), len(g)))
    parts.append(b"\0" * 16)
    _write_atomically(path, b"".join(parts))


class GozMesh:
    """A .GoZ file read into Blender coordinates (see write_goz for the arrays)."""

    def __init__(self):
        self.name = ""
        self.positions = np.zeros((0, 3))
        self.faces = np.zeros((0, 4), dtype=np.uint32)
        self.uvs = None
        self.colors = None
        self.mask = None
        self.groups = None


def read_goz(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 40 or data[:4] != b"GoZb":
        raise ValueError("Not a GoZ file: %s" % path)
    out = GozMesh()
    (name_len,) = struct.unpack_from("<I", data, 36)
    if name_len >= 24 and 32 + name_len <= len(data):
        out.name = data[56:32 + name_len].decode("ascii", "replace")
    pos = 32 + max(name_len, 8)
    while pos + 8 <= len(data):
        tag, length = struct.unpack_from("<II", data, pos)
        if tag == 0 or length < 8:
            break
        body = data[pos + 8:min(pos + length, len(data))]
        pos += length
        if len(body) < 8:
            continue
        (count,) = struct.unpack_from("<Q", body, 0)
        payload = body[8:]

        def records(size):
            return min(count, len(payload) // size)

        if tag == TAG_VERTICES:
            v = np.frombuffer(payload, dtype="<f4", count=records(12) * 3).reshape(-1, 3)
            out.positions = goz_to_blender(v)
        elif tag == TAG_FACES:
            out.faces = np.frombuffer(payload, dtype="<u4", count=records(16) * 4).reshape(-1, 4).copy()
        elif tag == TAG_UVS:
            uv = np.frombuffer(payload, dtype="<f4", count=records(32) * 8).reshape(-1, 4, 2).astype(np.float64)
            uv[:, :, 1] = 1.0 - uv[:, :, 1]
            out.uvs = uv
        elif tag == TAG_POLYPAINT:
            c = np.frombuffer(payload, dtype=np.uint8, count=records(4) * 4).reshape(-1, 4)
            out.colors = c[:, 2::-1].astype(np.float64) / 255.0
        elif tag == TAG_MASK:
            out.mask = 1.0 - np.frombuffer(payload, dtype="<u2", count=records(2)).astype(np.float64) / 65535.0
        elif tag == TAG_POLYGROUPS:
            out.groups = np.frombuffer(payload, dtype="<u2", count=records(2)).astype(np.int64)
    nv, nf = len(out.positions), len(out.faces)
    if nv == 0 or nf == 0:
        raise ValueError("GoZ file has no %s: %s" % ("vertices" if nv == 0 else "faces", path))
    quad = out.faces[:, 3] != NO_INDEX
    corners = np.where(quad[:, None], out.faces, np.where(np.arange(4) < 3, out.faces, 0))
    if corners.max() >= nv:
        raise ValueError("GoZ file has faces with bad vertex indices: %s" % path)
    if out.uvs is not None and len(out.uvs) != nf:
        out.uvs = None
    if out.colors is not None and len(out.colors) != nv:
        out.colors = None
    if out.mask is not None and len(out.mask) != nv:
        out.mask = None
    if out.groups is not None and len(out.groups) != nf:
        out.groups = None
    return out


def read_shapes(path):
    """A SculptForge blend shapes file (an object's sculpt layers, e.g. the 52 ARKit facial shapes): a list of
    (name, weight, indices, offsets) with offsets (N, 3) in Blender's axes, world space, and the vertex count.
    The object's .GoZ then holds the base shape (every layer at zero)."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20 or data[:8] != b"SFSHAPES":
        raise ValueError("Not a SculptForge shapes file: %s" % path)
    version, nv, count = struct.unpack_from("<III", data, 8)
    if version != 1:
        raise ValueError("The shapes file is from a newer SculptForge; update the add-on")
    pos = 20
    shapes = []
    for _ in range(count):
        if pos + 4 > len(data):
            raise ValueError("Truncated shapes file: %s" % path)
        (n,) = struct.unpack_from("<I", data, pos)
        name = data[pos + 4:pos + 4 + n].decode("utf-8", "replace")
        pos += 4 + n
        weight, k = struct.unpack_from("<fI", data, pos)
        pos += 8
        if k > nv or pos + 16 * k > len(data):
            raise ValueError("Truncated shapes file: %s" % path)
        idx = np.frombuffer(data, dtype="<u4", count=k, offset=pos).astype(np.int64)
        pos += 4 * k
        off = np.frombuffer(data, dtype="<f4", count=3 * k, offset=pos).reshape(-1, 3).astype(np.float64)
        pos += 12 * k
        if k and idx.max() >= nv:
            raise ValueError("The shapes file has bad vertex indices: %s" % path)
        shapes.append((name, float(weight), idx, off))
    return nv, shapes


# --------------------------------------------------------------------------------------------------
# SculptForge: running? installed where? start it
# --------------------------------------------------------------------------------------------------


def sculptforge_running(root):
    try:
        with open(os.path.join(root, "alive.json"), "rb") as f:
            beat = json.loads(f.read().decode("utf-8"))
        return abs(time.time() - float(beat.get("time", 0))) < HEARTBEAT_SECONDS
    except (OSError, ValueError, AttributeError):
        return False


def wait_for_heartbeat(root, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if sculptforge_running(root):
            return True
        time.sleep(0.2)
    return sculptforge_running(root)


def find_sculptforge(root, override=""):
    """Path of SculptForge: the add-on preference, else where SculptForge said it is (app.json), else PATH
    and the usual install folders. Empty when not found."""
    candidates = []
    if override:
        candidates.append(os.path.expanduser(override))
    try:
        with open(os.path.join(root, "app.json"), "rb") as f:
            candidates.append(json.loads(f.read().decode("utf-8")).get("path", ""))
    except (OSError, ValueError, AttributeError):
        pass
    for name in ("sculptforge", "SculptForge"):
        found = shutil.which(name)
        if found:
            candidates.append(found)
    if sys.platform == "win32":
        for base in filter(None, (os.environ.get("ProgramFiles"), os.environ.get("LOCALAPPDATA"))):
            candidates += sorted(glob.glob(os.path.join(base, "SculptForge*", "bin", "sculptforge.exe")), reverse=True)
            candidates += sorted(glob.glob(os.path.join(base, "SculptForge*", "sculptforge.exe")), reverse=True)
    elif sys.platform == "darwin":
        candidates += ["/Applications/SculptForge.app", os.path.expanduser("~/Applications/SculptForge.app")]
    else:
        for folder in ("~/Applications", "~/.local/bin", "~/Downloads", "/opt"):
            candidates += sorted(glob.glob(os.path.join(os.path.expanduser(folder), "SculptForge*.AppImage")), reverse=True)
        candidates += ["/usr/bin/sculptforge", "/usr/local/bin/sculptforge", "/opt/sculptforge/bin/sculptforge"]
        # The Flatpak's launchers, per-user and system-wide.
        candidates += [os.path.expanduser("~/.local/share/flatpak/exports/bin/org.sculptforge.SculptForge"),
                       "/var/lib/flatpak/exports/bin/org.sculptforge.SculptForge"]
    for c in candidates:
        if c and os.path.exists(c):
            return c
    return ""


def launch_sculptforge(path):
    """Starts SculptForge with --blender (it loads what was sent), detached from Blender."""
    if path.endswith(".app"):
        args = ["open", "-n", path, "--args", "--blender"]
    else:
        args = [path, "--blender"]
    kwargs = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL, "close_fds": True}
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    else:
        kwargs["start_new_session"] = True
    env = dict(os.environ)
    for key in ("PYTHONHOME", "PYTHONPATH"):  # Blender's Python, not SculptForge's
        env.pop(key, None)
    kwargs["env"] = env
    subprocess.Popen(args, **kwargs)
