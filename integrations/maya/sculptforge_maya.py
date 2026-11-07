# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""SculptForge bridge for Maya (SculptForge issue #42).

Sends the selected meshes from Maya to SculptForge and gets them back, through ONE exchange folder that you
pick in both apps. Nothing else is shared: no ports, sockets
or servers, and neither side runs anything it finds in the folder. Meshes travel as OBJ (quads and UVs, world
space, Maya's own units, one object per file); a small JSON manifest per direction lists the objects:

    <folder>/maya_to_sculptforge/manifest.json + r<rev>_<k>.obj   written here (Send to SculptForge)
    <folder>/sculptforge_to_maya/manifest.json + r<rev>_<k>.obj   written by SculptForge (File > Maya > Send to Maya)

Get from SculptForge replaces each object's mesh by name, keeping its transform, parent and shading groups, as
one undo step; objects Maya doesn't have yet are created. Auto (on by default) does the Get by itself when
SculptForge sends: every 250 ms a background thread looks at the modification time and size of SculptForge's
manifest (nothing else in the folder), and only when they change is the Get run, in Maya's main thread.

Live (off by default; the shelf's Live button) also sends by itself: the meshes you sent or got are watched
(Maya's node-dirty callbacks: a flag, nothing more), and once none of them changed for 300 ms the changed ones
are sent, in Maya's main thread when it is idle. What a Get loads is never sent back (the echo guard), and a
manifest this Maya session wrote is never loaded. Still files only.

Plain maya.cmds (maya.mel for the shelf, maya.utils for Auto, maya.api.OpenMaya's callbacks for Live) and the
standard library. Install: see README.md beside this file, or run install_shelf() once.
"""

import json
import os
import re
import struct
import sys
import threading
from array import array
import time
import uuid

FORMAT = "sculptforge-maya"
VERSION = 1
TO_SCULPTFORGE = "maya_to_sculptforge"
TO_MAYA = "sculptforge_to_maya"
MAX_MANIFEST_BYTES = 256 * 1024
MAX_OBJECTS = 256
MAX_NAME = 128
MAX_FILE_NAME = 64

FOLDER_VAR = "sculptforgeExchangeFolder"
REVISION_VAR = "sculptforgeLastRevision"
AUTO_VAR = "sculptforgeAutoGet"
AUTO_BUTTON = "sculptforgeAutoButton"
LIVE_VAR = "sculptforgeLive"
LIVE_BUTTON = "sculptforgeLiveButton"
POLL_SECONDS = 0.25
LIVE_IDLE_SECONDS = 0.3
WRITER = uuid.uuid4().hex[:16]  # this Maya session's id in the manifests it writes (the echo guard)
SHELF = "SculptForge"
IMPORT_NAMESPACE = "sculptforge_get"
OBJ_EXPORT_OPTIONS = "groups=0;ptgroups=0;materials=0;smoothing=0;normals=0"

_FILE_CHARS = re.compile(r"^[A-Za-z0-9_.\-]+$")
_NAME_BAD = re.compile(r"[^A-Za-z0-9_]")


# ---- The exchange format (no Maya needed: tests/maya/test_sculptforge_maya.py runs these with plain Python) ----

def safe_name(name):
    """A Maya node name: letters, digits and '_' only, not starting with a digit, at most MAX_NAME bytes."""
    s = _NAME_BAD.sub("_", (name or "").encode("utf-8", "replace").decode("ascii", "replace").replace("�", "_"))
    s = s[:MAX_NAME]
    if not s:
        return "Part"
    if s[0].isdigit():
        s = ("_" + s)[:MAX_NAME]
    return s


def file_name_ok(name):
    """A file a manifest may list: a plain *.obj name inside the manifest's own folder."""
    return (isinstance(name, str) and 4 < len(name) <= MAX_FILE_NAME and not name.startswith(".")
            and ".." not in name and _FILE_CHARS.match(name) is not None and name.lower().endswith(".obj"))


def write_manifest(objects, revision, sender_version="", unit="", writer="", live=False):
    """The manifest text. `objects` is a list of (name, file)."""
    data = {
        "format": FORMAT,
        "version": VERSION,
        "revision": int(revision),
        "sender": "maya",
        "sender_version": str(sender_version)[:64],
    }
    if unit:
        data["unit"] = str(unit)[:64]
    if writer:
        data["writer"] = str(writer)[:64]
    if live:
        data["live"] = True
    data["objects"] = [{"name": n, "file": f} for n, f in objects]
    return json.dumps(data, indent=2) + "\n"


def parse_manifest(text):
    """Checks a manifest as strictly as SculptForge does. Returns {"revision", "sender", "objects": [(name, file)]};
    raises ValueError with the reason."""
    if len(text) > MAX_MANIFEST_BYTES:
        raise ValueError("the manifest is too big")
    try:
        data = json.loads(text)
    except (ValueError, RecursionError) as e:
        raise ValueError("the manifest is not valid JSON (%s)" % e)
    if not isinstance(data, dict) or data.get("format") != FORMAT:
        raise ValueError("not a SculptForge Maya bridge manifest")
    version = data.get("version")
    if isinstance(version, bool) or not isinstance(version, (int, float)) or version < 1 or version != int(version):
        raise ValueError("the manifest has no valid version")
    if version > VERSION:
        raise ValueError("the manifest is from a newer SculptForge (version %d): update this script" % version)
    revision = data.get("revision")
    if (isinstance(revision, bool) or not isinstance(revision, (int, float)) or revision < 0
            or revision != int(revision) or revision > 2 ** 53):
        raise ValueError("the manifest has no valid revision")
    for key in ("sender", "sender_version", "unit", "writer"):
        if key in data and (not isinstance(data[key], str) or len(data[key]) > 64):
            raise ValueError("the manifest has a bad %r" % key)
    objects = data.get("objects")
    if not isinstance(objects, list):
        raise ValueError("the manifest lists no objects")
    if len(objects) > MAX_OBJECTS:
        raise ValueError("the manifest lists too many objects (%d, at most %d)" % (len(objects), MAX_OBJECTS))
    out, files = [], set()
    for o in objects:
        if not isinstance(o, dict) or not isinstance(o.get("name"), str) or not isinstance(o.get("file"), str):
            raise ValueError("the manifest has an object without a name and a file")
        if not file_name_ok(o["file"]):
            raise ValueError("the manifest names a file this script won't read: %r" % o["file"][:MAX_FILE_NAME])
        if o["file"] in files:
            raise ValueError("the manifest lists %r twice" % o["file"])
        if len(o["name"].encode("utf-8")) > 4 * MAX_NAME:
            raise ValueError("the manifest has a name that is too long")
        files.add(o["file"])
        out.append((safe_name(o["name"]), o["file"]))
    return {"revision": int(revision), "sender": data.get("sender", ""), "writer": data.get("writer", ""),
            "live": data.get("live") is True, "objects": out}


def read_manifest(box):
    """The parsed manifest in `box`, or None when nothing has been sent. Raises ValueError when it is not valid."""
    path = os.path.join(box, "manifest.json")
    if not os.path.lexists(path):
        return None
    if os.path.islink(path) or not os.path.isfile(path):
        raise ValueError("%s is not a plain file" % path)
    if os.path.getsize(path) > MAX_MANIFEST_BYTES:
        raise ValueError("the manifest is too big")
    with open(path, "r", encoding="utf-8") as f:
        return parse_manifest(f.read(MAX_MANIFEST_BYTES + 1))


def object_file(box, file_name):
    """The full path of a file a manifest lists, checked: a plain file directly inside `box`."""
    if not file_name_ok(file_name):
        raise ValueError("bad file name %r" % file_name)
    path = os.path.join(box, file_name)
    real_box = os.path.realpath(box)
    if os.path.dirname(os.path.realpath(path)) != real_box or os.path.islink(path) or not os.path.isfile(path):
        raise ValueError("%s is missing or not a plain file in the exchange folder" % file_name)
    return path


def remove_entry(path):
    """Removes a file or link (never what a link points to), so a planted link can't redirect a write."""
    if os.path.lexists(path) and not os.path.isdir(path) or os.path.islink(path):
        os.remove(path)


def write_atomically(path, text):
    tmp = path + ".tmp"
    remove_entry(tmp)
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def next_revision(box):
    try:
        m = read_manifest(box)
    except ValueError:
        m = None
    return (m["revision"] if m else 0) + 1


def remove_old_sends(box, keep_prefix):
    """Removes r<n>_* files of earlier sends (and nothing else)."""
    for n in os.listdir(box):
        if re.match(r"^r[0-9]+_", n) and not n.startswith(keep_prefix):
            try:
                os.remove(os.path.join(box, n))
            except OSError:
                pass  # open elsewhere: the next send tries again


def manifest_stamp(box):
    """The modification time and size of <box>/manifest.json, or None when there is none: what Auto checks once a
    second (cheap, and safe from any thread); the manifest itself is read only when this changes."""
    try:
        st = os.lstat(os.path.join(box, "manifest.json"))
    except OSError:
        return None
    return (st.st_mtime_ns, st.st_size)


class LiveState(object):
    """Live's bookkeeping, apart from Maya so it can be tested: which watched objects changed and when, the wait
    for 300 ms of stillness, and the echo guard (changes while a Get applies SculptForge's meshes don't count)."""

    def __init__(self, idle=LIVE_IDLE_SECONDS, clock=time.monotonic):
        self.idle = idle
        self.clock = clock
        self.lock = threading.Lock()
        self.dirty = {}  # transform (long name) -> when it last changed
        self.applying = 0  # > 0 while a Get (or a send) works on the scene
        self.queued = False  # a send is handed to Maya's main thread already

    def mark(self, transform):
        """A watched mesh changed (called from Maya's dirty callbacks: keep it cheap)."""
        if self.applying:
            return
        with self.lock:
            self.dirty[transform] = self.clock()

    def forget(self, transforms):
        with self.lock:
            for t in transforms:
                self.dirty.pop(t, None)

    def due(self):
        """True once something changed and nothing changed for `idle` seconds (and no send is queued)."""
        with self.lock:
            if not self.dirty or self.queued or self.applying:
                return False
            return self.clock() - max(self.dirty.values()) >= self.idle

    def take(self):
        """The changed objects, in the order they first changed; clears them."""
        with self.lock:
            out = list(self.dirty)
            self.dirty.clear()
            self.queued = False
            return out


class Watcher(object):
    """Auto: watches one box's manifest from a daemon thread and hands `on_change` to `defer` (Maya's
    executeDeferred: the main thread, when Maya is idle) when it changed. What is there when it starts is old
    news: Get loads it. With `live`, it also hands `on_live` over once Live's wait is over."""

    def __init__(self, box, defer, on_change, live=None, on_live=None):
        self.box = box
        self.defer = defer
        self.on_change = on_change
        self.live = live
        self.on_live = on_live
        self.seen = manifest_stamp(box)
        self.stopped = threading.Event()
        self.thread = None

    def poll(self):
        """One check: True when the manifest is there and changed since the last check."""
        stamp = manifest_stamp(self.box)
        if stamp is None or stamp == self.seen:
            return False
        self.seen = stamp
        return True

    def live_due(self):
        """One Live check: True when a send is due (it is then marked queued until it runs)."""
        if self.live is None or not self.live.due():
            return False
        with self.live.lock:
            self.live.queued = True
        return True

    def _run(self):
        while not self.stopped.wait(POLL_SECONDS):
            if self.poll():
                self.defer(self.on_change)
            if self.live_due():
                self.defer(self.on_live)

    def start(self):
        self.thread = threading.Thread(target=self._run, name="SculptForge Auto Get", daemon=True)
        self.thread.start()

    def stop(self):
        self.stopped.set()


def unique_names(names):
    out = []
    for name in names:
        n, k = name, 1
        while n in out:
            n = "%s%d" % (name, k)
            k += 1
        out.append(n)
    return out


# ---- Maya ----

def _cmds():
    import maya.cmds as cmds  # only inside Maya
    return cmds


def _message(text, warning=False):
    cmds = _cmds()
    if warning:
        cmds.warning("SculptForge: " + text)
    else:
        print("SculptForge: " + text)
    try:
        cmds.inViewMessage(amg=text, pos="topCenter", fade=True)
    except Exception:
        pass
    return text


def choose_folder():
    """Asks for the exchange folder (pick the same one in SculptForge's File > Maya > Exchange folder...)."""
    cmds = _cmds()
    start = cmds.optionVar(q=FOLDER_VAR) if cmds.optionVar(exists=FOLDER_VAR) else ""
    picked = cmds.fileDialog2(fileMode=3, caption="SculptForge exchange folder", okCaption="Use this folder",
                              startingDirectory=start or None)
    if not picked:
        return ""
    folder = picked[0]
    cmds.optionVar(sv=(FOLDER_VAR, folder))
    _message("exchange folder: " + folder)
    start_auto_get()
    return folder


def exchange_folder(ask=True):
    cmds = _cmds()
    folder = cmds.optionVar(q=FOLDER_VAR) if cmds.optionVar(exists=FOLDER_VAR) else ""
    if folder and os.path.isdir(folder):
        return folder
    return choose_folder() if ask else ""


def _ensure_obj_plugin():
    cmds = _cmds()
    if not cmds.pluginInfo("objExport", q=True, loaded=True):
        cmds.loadPlugin("objExport", quiet=True)


def _mesh_shape(transform):
    shapes = _cmds().listRelatives(transform, shapes=True, type="mesh", noIntermediate=True, fullPath=True) or []
    return shapes[0] if shapes else None


def _selected_meshes():
    """(transform, shape) for each selected mesh, in selection order (a selected shape counts as its transform)."""
    cmds = _cmds()
    out, seen = [], set()
    for node in cmds.ls(selection=True, long=True, objectsOnly=True) or []:
        transform = node
        if cmds.nodeType(node) == "mesh":
            transform = (cmds.listRelatives(node, parent=True, fullPath=True) or [node])[0]
        shape = _mesh_shape(transform)
        if shape and transform not in seen:
            seen.add(transform)
            out.append((transform, shape))
    return out


def _short(path):
    return path.split("|")[-1].split(":")[-1]


def send_to_sculptforge():
    """Writes the selected meshes to the exchange folder; SculptForge loads them with File > Maya > Get from Maya
    (or by itself with Get automatically on)."""
    meshes = _selected_meshes()
    if not meshes:
        return _message("select one or more meshes to send", warning=True)
    return _send(meshes, live=False)


def _send(meshes, live):
    """Writes `meshes` ((transform, shape) pairs) to SculptForge's box: each OBJ, then the manifest (written to a
    temporary name and renamed)."""
    cmds = _cmds()
    if len(meshes) > MAX_OBJECTS:
        return _message("too many meshes selected (at most %d)" % MAX_OBJECTS, warning=True)
    folder = exchange_folder(ask=not live)
    if not folder:
        return _message("no exchange folder chosen", warning=True)
    box = os.path.join(folder, TO_SCULPTFORGE)
    if os.path.islink(box):
        return _message("%s is a link, not a folder" % box, warning=True)
    if not os.path.isdir(box):
        os.makedirs(box)
    _ensure_obj_plugin()
    revision = next_revision(box)
    prefix = "r%d_" % revision
    names = unique_names([safe_name(_short(t)) for t, _ in meshes])
    objects = []
    selection = cmds.ls(selection=True, long=True) or []
    _live.applying += 1  # (selecting and exporting changes no mesh, but nothing counts while we work)
    try:
        for k, ((transform, shape), name) in enumerate(zip(meshes, names)):
            file_name = "%s%03d.obj" % (prefix, k)
            remove_entry(os.path.join(box, file_name))
            cmds.select(shape, replace=True)  # the shape alone: not the transform's children
            cmds.file(os.path.join(box, file_name), force=True, options=OBJ_EXPORT_OPTIONS, type="OBJexport",
                      preserveReferences=True, exportSelected=True)
            objects.append((name, file_name))
    finally:
        _live.applying -= 1
        if selection:
            cmds.select(selection, replace=True)
        else:
            cmds.select(clear=True)
    write_atomically(os.path.join(box, "manifest.json"),
                     write_manifest(objects, revision, cmds.about(version=True), cmds.currentUnit(q=True, linear=True),
                                    WRITER, live))
    remove_old_sends(box, prefix)
    _watch([t for t, _ in meshes])
    start_auto_get()
    text = "sent %d mesh%s to SculptForge" % (len(objects), "" if len(objects) == 1 else "es")
    if live:  # (no view message for every Live send: the status line only)
        print("SculptForge: Live: " + text)
        return text
    return _message(text)


def _find_transform(name):
    cmds = _cmds()
    for t in cmds.ls(name, long=True, type="transform") or []:
        if _short(t) == name and _mesh_shape(t):
            return t
    return None


def _import_obj(path):
    """Imports one OBJ; returns (transform, shape) of the new mesh."""
    cmds = _cmds()
    nodes = cmds.file(path, i=True, type="OBJ", options="mo=0", returnNewNodes=True, ignoreVersion=True,
                      namespace=IMPORT_NAMESPACE, mergeNamespacesOnClash=True, preserveReferences=True) or []
    for n in nodes:
        if cmds.nodeType(n) == "transform":
            shape = _mesh_shape(n)
            if shape:
                return cmds.ls(n, long=True)[0], shape
    raise ValueError("%s holds no mesh" % os.path.basename(path))


def _replace_mesh(existing, new_transform, new_shape):
    """Puts the imported (world-space) mesh under `existing`, in its object space, in place of its mesh shape;
    the transform, parent and shading groups stay."""
    cmds = _cmds()
    old_shape = _mesh_shape(existing)
    groups = []
    for sg in cmds.listConnections(old_shape, type="shadingEngine") or []:
        if sg not in groups:
            groups.append(sg)
    old_name = _short(old_shape)
    # World space -> the object's space: place the import at the inverse of the object's world matrix and bake it.
    cmds.xform(new_transform, matrix=cmds.getAttr(existing + ".worldInverseMatrix[0]"), worldSpace=True)
    cmds.makeIdentity(new_transform, apply=True, translate=True, rotate=True, scale=True, normal=0,
                      preserveNormals=True)
    moved = cmds.parent(new_shape, existing, shape=True, relative=True)[0]
    moved = cmds.ls(moved, long=True)[0] if cmds.ls(moved, long=True) else moved
    for sg in groups or ["initialShadingGroup"]:
        cmds.sets(moved, edit=True, forceElement=sg)
    cmds.delete(old_shape)
    cmds.delete(new_transform)
    return cmds.rename(moved, old_name)


def get_from_sculptforge():
    """Loads what SculptForge last sent: each object replaces the mesh of the Maya object with its name (one undo
    step), or becomes a new object."""
    cmds = _cmds()
    folder = exchange_folder()
    if not folder:
        return _message("no exchange folder chosen", warning=True)
    box = os.path.join(folder, TO_MAYA)
    start_auto_get()
    try:
        manifest = read_manifest(box) if os.path.isdir(box) and not os.path.islink(box) else None
    except ValueError as e:
        return _message("can't read what SculptForge sent: %s" % e, warning=True)
    if not manifest or not manifest["objects"]:
        return _message("nothing has been sent from SculptForge yet (File > Maya > Send to Maya)", warning=True)
    _ensure_obj_plugin()
    done = []
    cmds.undoInfo(openChunk=True, chunkName="Get from SculptForge")
    _live.applying += 1  # the echo guard: what SculptForge sent is not a Maya edit to send back
    try:
        for name, file_name in manifest["objects"]:
            path = object_file(box, file_name)
            new_transform, new_shape = _import_obj(path)
            existing = _find_transform(name)
            if existing:
                _replace_mesh(existing, new_transform, new_shape)
                done.append(existing)
            else:
                t = cmds.rename(new_transform, ":" + name)
                cmds.rename(_mesh_shape(t), name + "Shape")
                done.append(t)
        if cmds.namespace(exists=IMPORT_NAMESPACE):
            cmds.namespace(removeNamespace=IMPORT_NAMESPACE, mergeNamespaceWithRoot=True)
    except ValueError as e:
        return _message("can't load what SculptForge sent: %s" % e, warning=True)
    finally:
        _live.applying -= 1
        cmds.undoInfo(closeChunk=True)
    _live.forget(done)
    _watch(done)  # (their new shapes)
    cmds.optionVar(iv=(REVISION_VAR, manifest["revision"]))
    cmds.select([d for d in done if cmds.objExists(d)] or None, replace=True)
    return _message("got %d mesh%s from SculptForge" % (len(done), "" if len(done) == 1 else "es"))


# ---- Auto: Get by itself when SculptForge sends ----

_watcher = None


def auto_get_on():
    """Auto is on unless turned off with the shelf's Auto button."""
    cmds = _cmds()
    return not cmds.optionVar(exists=AUTO_VAR) or bool(cmds.optionVar(q=AUTO_VAR))


def _auto_get_new():
    """Main thread, after the manifest changed: Get, unless that revision is already loaded (or Auto went off)."""
    cmds = _cmds()
    folder = exchange_folder(ask=False)
    if not auto_get_on() or not folder:
        return
    try:
        manifest = read_manifest(os.path.join(folder, TO_MAYA))
    except ValueError:
        manifest = None  # half written, or not valid: Get says why; the next change is checked again
    if not manifest or not manifest["objects"] or manifest["writer"] == WRITER:  # (never what this Maya wrote)
        return
    if cmds.optionVar(exists=REVISION_VAR) and cmds.optionVar(q=REVISION_VAR) == manifest["revision"]:
        return
    get_from_sculptforge()


def start_auto_get():
    """Starts watching what SculptForge sends (Send, Get and Folder call this: Auto runs from the first use of the
    shelf in a Maya session). Stops when Auto is off or there is no folder."""
    global _watcher
    folder = exchange_folder(ask=False) if auto_get_on() else ""
    box = os.path.join(folder, TO_MAYA) if folder else ""
    if _watcher and _watcher.box == box:
        return _watcher
    if _watcher:
        _watcher.stop()
        _watcher = None
    if box:
        import maya.utils
        _watcher = Watcher(box, maya.utils.executeDeferred, _auto_get_new, _live, _live_send)
        _watcher.start()
    return _watcher


# ---- Live: send by itself what changes here ----

_live = LiveState()
_watched = {}  # transform (long name) -> callback id (or None without maya.api)


def live_on():
    cmds = _cmds()
    return cmds.optionVar(exists=LIVE_VAR) and bool(cmds.optionVar(q=LIVE_VAR))


def _on_dirty(node, plug, *_):
    """Maya's node-dirty callback on a watched mesh shape: its mesh (or where it is) changed. Only a flag."""
    try:
        name = plug.partialName(useLongNames=True)
        if not name.startswith(("inMesh", "outMesh", "pnts", "worldMesh", "cachedInMesh")):
            return
        import maya.api.OpenMaya as om
        dag = om.MFnDagNode(node)
        _live.mark(om.MFnDagNode(dag.parent(0)).fullPathName())
    except Exception:
        pass


def _watch(transforms):
    """Watches these meshes for Live (the ones sent or got: Send adds more). Again for a transform whose shape
    was replaced by a Get."""
    for t in transforms:
        old = _watched.pop(t, None)
        callback = None
        try:
            import maya.api.OpenMaya as om
            if old is not None:
                om.MMessage.removeCallback(old)
            shape = _mesh_shape(t)
            if shape:
                sel = om.MSelectionList()
                sel.add(shape)
                callback = om.MNodeMessage.addNodeDirtyPlugCallback(sel.getDependNode(0), _on_dirty)
        except Exception:
            callback = None  # (no maya.api: the tests mark changes by hand)
        _watched[t] = callback


def _live_send():
    """Main thread, once Live's wait is over: sends the watched meshes that changed (over the Live link when it is
    connected, else through the folder)."""
    changed = _live.take()
    linked = _link is not None and _link.connected
    if not (live_on() or linked) or not changed:
        return None
    cmds = _cmds()
    meshes = []
    for t in changed:
        if cmds.objExists(t):
            shape = _mesh_shape(t)
            if shape:
                meshes.append((cmds.ls(t, long=True)[0], shape))
    if not meshes:
        return None
    if linked:
        try:
            for transform, shape in meshes:
                _link.send(encode_mesh(_extract_mesh(transform, shape)))
            return "sent %d over the link" % len(meshes)
        except Exception as e:
            return _message("Live link: can't send to SculptForge: %s" % e, warning=True)
    try:
        return _send(meshes, live=True)
    except Exception as e:  # (a folder gone, a disk full: say it, keep Maya working)
        return _message("Live: can't send to SculptForge: %s" % e, warning=True)


def _live_label():
    return "Live" if live_on() else "Live off"


def toggle_live():
    """The shelf's Live button: send the watched meshes by themselves after each edit (and get by itself)."""
    cmds = _cmds()
    on = not live_on()
    cmds.optionVar(iv=(LIVE_VAR, 1 if on else 0))
    if on and not auto_get_on():
        cmds.optionVar(iv=(AUTO_VAR, 1))
    if on:
        _watch([t for t, _ in _selected_meshes()])  # (and the ones sent or got this session)
    _live.forget(list(_live.dirty))
    start_auto_get()
    for button, label in ((LIVE_BUTTON, _live_label()), (AUTO_BUTTON, _auto_label())):
        try:
            if cmds.shelfButton(button, exists=True):
                cmds.shelfButton(button, edit=True, label=label, imageOverlayLabel=label)
        except Exception:
            pass
    if on:
        return _message("Live on: meshes you edit go to SculptForge by themselves (sent or got ones, and the selection)")
    return _message("Live off: click Send to send")


def _auto_label():
    return "Auto" if auto_get_on() else "Manual"


def toggle_auto_get():
    """The shelf's Auto button: Get by itself when SculptForge sends (on), or only with Get (off)."""
    cmds = _cmds()
    cmds.optionVar(iv=(AUTO_VAR, 0 if auto_get_on() else 1))
    start_auto_get()
    try:
        if cmds.shelfButton(AUTO_BUTTON, exists=True):
            cmds.shelfButton(AUTO_BUTTON, edit=True, label=_auto_label(), imageOverlayLabel=_auto_label())
    except Exception:
        pass
    if auto_get_on():
        return _message("Auto on: what SculptForge sends loads by itself")
    return _message("Auto off: click Get to load what SculptForge sends")


# ---- The Live link: a connection to the open SculptForge (File > Maya > Live link) ----
#
# SculptForge offers it only while the user has it on (it warns every time): a Unix domain socket only this user
# can open (Linux, macOS) or a named pipe only this user can open (Windows), never a network port. The pairing
# code from SculptForge (File > Maya > Copy pairing code) opens it. Mesh data only, both ways: SculptForge streams
# the vertices a stroke moves (Points) and whole meshes when faces change (Mesh); this side sends whole meshes
# when you edit. Everything received is checked as strictly as SculptForge checks it. Plain socket / ctypes.

LINK_MAGIC = b"SFML"
LINK_VERSION = 1
LINK_HELLO, LINK_WELCOME, LINK_MESH, LINK_POINTS, LINK_BYE = 1, 2, 3, 4, 5
LINK_MAX_PAYLOAD = 1 << 30
LINK_MAX_VERTICES = 64 << 20
LINK_CODE_CHARS = 32
LINK_BUTTON = "sculptforgeLinkButton"
_HEADER = struct.Struct("<4sHHQ")
_LITTLE = sys.byteorder == "little"


class LinkError(ValueError):
    pass


def _f32(values):
    a = array("f", values)
    if not _LITTLE:
        a.byteswap()
    return a.tobytes()


def _u32(values):
    a = array("I", values)
    if a.itemsize != 4:
        a = array("L", values)
    if not _LITTLE:
        a.byteswap()
    return a.tobytes()


def _read_array(code, data, at, count):
    a = array(code)
    if code in ("I", "L") and a.itemsize != 4:
        a = array("L" if code == "I" else "I")
    end = at + a.itemsize * count
    if count < 0 or end > len(data):
        raise LinkError("a message whose size doesn't add up")
    a.frombytes(data[at:end])
    if not _LITTLE:
        a.byteswap()
    return a, end


def frame(kind, payload=b""):
    return _HEADER.pack(LINK_MAGIC, LINK_VERSION, kind, len(payload)) + payload


def parse_header(header):
    """(kind, payload length) of a 16-byte frame header; raises LinkError."""
    if len(header) != _HEADER.size:
        raise LinkError("a short header")
    magic, version, kind, length = _HEADER.unpack(header)
    if magic != LINK_MAGIC:
        raise LinkError("not the SculptForge link")
    if version != LINK_VERSION:
        raise LinkError("link version %d (this script speaks %d): update the older one" % (version, LINK_VERSION))
    if not LINK_HELLO <= kind <= LINK_BYE:
        raise LinkError("an unknown message")
    if length > LINK_MAX_PAYLOAD:
        raise LinkError("a message over the size cap")
    return kind, length


def encode_hello(code, client="Maya"):
    code = normalise_code(code).encode("ascii")
    client = client.encode("ascii", "replace")[:64]
    return frame(LINK_HELLO, code + struct.pack("<H", len(client)) + client)


def _name(name):
    raw = name.encode("ascii")
    if not raw or len(raw) > MAX_NAME or safe_name(name) != name:
        raise LinkError("a bad object name")
    return struct.pack("<H", len(raw)) + raw


def encode_mesh(mesh):
    """A Mesh frame from {"name", "positions" (3 per vertex), "face_sizes", "indices", "uvs" (2 per corner or empty)}."""
    v = len(mesh["positions"]) // 3
    f, c = len(mesh["face_sizes"]), len(mesh["indices"])
    uvs = mesh.get("uvs") or []
    head = _name(mesh["name"]) + struct.pack("<IIIB", v, f, c, 1 if uvs else 0)
    return frame(LINK_MESH, head + _f32(mesh["positions"]) + bytes(bytearray(mesh["face_sizes"])) + _u32(mesh["indices"])
                 + (_f32(uvs) if uvs else b""))


def _read_name(data, at):
    if at + 2 > len(data):
        raise LinkError("a bad object name")
    (n,) = struct.unpack_from("<H", data, at)
    raw = data[at + 2:at + 2 + n]
    if len(raw) != n:
        raise LinkError("a bad object name")
    try:
        name = raw.decode("ascii")
    except UnicodeDecodeError:
        raise LinkError("a bad object name")
    if not name or n > MAX_NAME or safe_name(name) != name:
        raise LinkError("a bad object name")
    return name, at + 2 + n


def _finite(values):
    for x in values:
        if x != x or x in (float("inf"), float("-inf")):
            return False
    return True


def parse_mesh(data):
    """Checks a Mesh payload as strictly as SculptForge does; returns the dict encode_mesh takes."""
    name, at = _read_name(data, 0)
    if at + 13 > len(data):
        raise LinkError("a mesh with bad counts")
    v, f, c, flags = struct.unpack_from("<IIIB", data, at)
    at += 13
    if v == 0 or v > LINK_MAX_VERTICES or f == 0 or c < 3 * f or c > 255 * f or flags > 1:
        raise LinkError("a mesh with bad counts")
    positions, at = _read_array("f", data, at, 3 * v)
    if at + f > len(data):
        raise LinkError("a mesh whose size doesn't add up")
    sizes = bytearray(data[at:at + f])
    at += f
    indices, at = _read_array("I", data, at, c)
    uvs = array("f")
    if flags:
        uvs, at = _read_array("f", data, at, 2 * c)
    if at != len(data):
        raise LinkError("a mesh whose size doesn't add up")
    if min(sizes) < 3 or sum(sizes) != c:
        raise LinkError("a mesh whose faces don't add up")
    if max(indices) >= v:
        raise LinkError("a mesh with a vertex index out of range")
    if not _finite(positions) or not _finite(uvs):
        raise LinkError("a mesh with a number that isn't finite")
    return {"name": name, "positions": positions, "face_sizes": sizes, "indices": indices, "uvs": uvs}


def encode_points(name, vertex_count, indices, positions):
    return frame(LINK_POINTS, _name(name) + struct.pack("<II", vertex_count, len(indices)) + _u32(indices)
                 + _f32(positions))


def parse_points(data):
    """(name, vertex count, indices, positions) of a Points payload, checked."""
    name, at = _read_name(data, 0)
    if at + 8 > len(data):
        raise LinkError("points whose size doesn't add up")
    vertex_count, n = struct.unpack_from("<II", data, at)
    at += 8
    if vertex_count == 0 or vertex_count > LINK_MAX_VERTICES or n > vertex_count:
        raise LinkError("points whose size doesn't add up")
    indices, at = _read_array("I", data, at, n)
    positions, at = _read_array("f", data, at, 3 * n)
    if at != len(data):
        raise LinkError("points whose size doesn't add up")
    if n and max(indices) >= vertex_count or not _finite(positions):
        raise LinkError("points out of range")
    return name, vertex_count, indices, positions


def normalise_code(code):
    c = (code or "").replace("-", "").strip().lower()
    if len(c) != LINK_CODE_CHARS or any(ch not in "0123456789abcdef" for ch in c):
        raise LinkError("that isn't a pairing code (32 letters and digits, from SculptForge's File > Maya > Copy "
                        "pairing code)")
    return c


_PIPE_RE = re.compile(r"^\\\\\.\\pipe\\sculptforge-maya-link-[A-Za-z0-9_\-]+$")


def default_endpoint():
    """Where SculptForge offers the link, by its rules (link/AiLink.cpp localEndpoint)."""
    if os.name == "nt":
        import getpass
        user = re.sub(r"[^A-Za-z0-9_\-]", "_", getpass.getuser() or "user") or "user"
        return "\\\\.\\pipe\\sculptforge-maya-link-" + user
    run = os.environ.get("XDG_RUNTIME_DIR")
    if run:
        return os.path.join(run, "sculptforge", "maya-link.sock")
    tmp = (os.environ.get("TMPDIR") or "/tmp").rstrip("/") or "/tmp"
    return os.path.join(tmp, "sculptforge-%d" % os.getuid(), "maya-link.sock")


def parse_pairing(text):
    """(code, endpoint) from what Copy pairing code put on the clipboard ("sflink1 CODE ENDPOINT"), or a bare code.
    The endpoint must be SculptForge's: a maya-link.sock path or its named pipe, nothing else."""
    parts = (text or "").strip().split(None, 2)
    if len(parts) == 3 and parts[0] == "sflink1":
        code, endpoint = normalise_code(parts[1]), parts[2].strip()
    elif len(parts) == 1:
        code, endpoint = normalise_code(parts[0]), default_endpoint()
    else:
        raise LinkError("paste what SculptForge's File > Maya > Copy pairing code copied")
    if os.name == "nt":
        if not _PIPE_RE.match(endpoint):
            raise LinkError("that isn't SculptForge's link")
    elif not (os.path.isabs(endpoint) and os.path.basename(endpoint) == "maya-link.sock" and ".." not in endpoint):
        raise LinkError("that isn't SculptForge's link")
    return code, endpoint


class _SocketConn(object):
    """Linux / macOS: the Unix domain socket, which must be this user's (and in a folder only this user owns)."""

    def __init__(self, endpoint):
        import socket
        st = os.lstat(endpoint)
        folder = os.lstat(os.path.dirname(endpoint))
        import stat
        if (not stat.S_ISSOCK(st.st_mode) or st.st_uid != os.getuid() or folder.st_uid != os.getuid()
                or folder.st_mode & 0o077):
            raise LinkError("the link isn't this user's: not connecting")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5.0)
        self.sock.connect(endpoint)

    def recv(self, n, timeout):
        """Up to n bytes; b"" on timeout; raises on close."""
        import socket
        self.sock.settimeout(timeout)
        try:
            data = self.sock.recv(n)
        except socket.timeout:
            return b""
        if not data:
            raise LinkError("SculptForge closed the link")
        return data

    def send(self, data):
        self.sock.settimeout(30.0)
        self.sock.sendall(data)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class _PipeConn(object):
    """Windows: SculptForge's named pipe, opened as a file; reads only what PeekNamedPipe says is there, so a
    write never waits behind a read."""

    def __init__(self, endpoint):
        import ctypes
        import msvcrt
        self.file = open(endpoint, "r+b", buffering=0)
        self.handle = msvcrt.get_osfhandle(self.file.fileno())
        self.k32 = ctypes.windll.kernel32
        self.ctypes = ctypes

    def recv(self, n, timeout):
        avail = self.ctypes.c_ulong(0)
        deadline = time.monotonic() + timeout
        while True:
            if not self.k32.PeekNamedPipe(self.ctypes.c_void_p(self.handle), None, 0, None, self.ctypes.byref(avail), None):
                raise LinkError("SculptForge closed the link")
            if avail.value:
                return self.file.read(min(n, avail.value))
            if time.monotonic() >= deadline:
                return b""
            time.sleep(0.002)

    def send(self, data):
        view = memoryview(data)
        while view:
            n = self.file.write(view[:1 << 20])
            view = view[n:]

    def close(self):
        try:
            self.file.close()
        except OSError:
            pass


class LinkClient(object):
    """One connection to SculptForge. `handle(kind, payload)` runs on the reader thread for each Mesh or Points (it
    should only queue them); `tick()` runs there too, about every 50 ms."""

    def __init__(self, conn, code, handle, tick=None, client="Maya"):
        self.conn = conn
        self.handle = handle
        self.tick = tick
        self.lock = threading.Lock()
        self.connected = False
        self.stopped = threading.Event()
        self.error = ""
        self.conn.send(encode_hello(code, client))
        kind, payload = self._read_frame(5.0)
        if kind != LINK_WELCOME:
            raise LinkError("SculptForge refused the pairing code (copy it again: it changes each time the link "
                            "is turned on)")
        self.server = payload[2:].decode("ascii", "replace")
        self.connected = True
        self.thread = threading.Thread(target=self._run, name="SculptForge Live link", daemon=True)
        self.thread.start()

    def _read_exact(self, n, first_timeout):
        out = bytearray()
        timeout = first_timeout
        while len(out) < n:
            data = self.conn.recv(n - len(out), timeout)
            if not data:
                if not out and first_timeout < 1.0:
                    return None  # nothing yet
                raise LinkError("the link timed out")
            out += data
            timeout = 30.0
        return bytes(out)

    def _read_frame(self, first_timeout):
        header = self._read_exact(_HEADER.size, first_timeout)
        if header is None:
            return None, None
        kind, length = parse_header(header)
        payload = self._read_exact(length, 30.0) if length else b""
        return kind, payload

    def _run(self):
        try:
            while not self.stopped.is_set():
                kind, payload = self._read_frame(0.05)
                if kind is None:
                    if self.tick:
                        self.tick()
                    continue
                if kind == LINK_BYE:
                    self.error = "SculptForge turned the link off"
                    break
                if kind not in (LINK_MESH, LINK_POINTS):
                    raise LinkError("an unexpected message")
                self.handle(kind, payload)
                if self.tick:
                    self.tick()
        except Exception as e:  # (closed, timed out, or a message that didn't check out: the link ends)
            self.error = str(e)
        self.connected = False
        self.conn.close()

    def send(self, data):
        with self.lock:
            self.conn.send(data)

    def close(self):
        self.stopped.set()
        if self.connected:
            try:
                self.send(frame(LINK_BYE))
            except Exception:
                pass
        self.connected = False


class LinkInbox(object):
    """What came over the link, folded for Maya's main thread: the newest whole mesh per object, and per object the
    latest position of every vertex streamed since (absolute positions, so many Points become one update)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.meshes = {}
        self.points = {}  # name -> (vertex count, {index: (x, y, z)})
        self.order = []

    def add(self, kind, payload):
        """Reader thread: checks and folds one message. True when Maya's main thread should be woken."""
        if kind == LINK_MESH:
            mesh = parse_mesh(payload)
            with self.lock:
                self.meshes[mesh["name"]] = mesh
                self.points.pop(mesh["name"], None)
                self._touch(mesh["name"])
        else:
            name, count, indices, positions = parse_points(payload)
            with self.lock:
                old = self.points.get(name)
                moved = old[1] if old and old[0] == count else {}
                for k, i in enumerate(indices):
                    moved[i] = (positions[3 * k], positions[3 * k + 1], positions[3 * k + 2])
                self.points[name] = (count, moved)
                self._touch(name)
        return True

    def _touch(self, name):
        if name in self.order:
            self.order.remove(name)
        self.order.append(name)

    def take(self):
        """[(name, mesh or None, (count, moved) or None)] in arrival order; clears."""
        with self.lock:
            out = [(n, self.meshes.pop(n, None), self.points.pop(n, None)) for n in self.order]
            self.order = []
            return out


_link = None
_link_inbox = LinkInbox()
_link_apply_queued = threading.Event()


def _link_handle(kind, payload):
    _link_inbox.add(kind, payload)
    if not _link_apply_queued.is_set():
        _link_apply_queued.set()
        import maya.utils
        maya.utils.executeDeferred(_link_apply)


def _link_tick():
    if _live.due():
        with _live.lock:
            _live.queued = True
        import maya.utils
        maya.utils.executeDeferred(_live_send)


def _om():
    import maya.api.OpenMaya as om  # only inside Maya
    return om


def _dag(node):
    om = _om()
    sel = om.MSelectionList()
    sel.add(node)
    return sel.getDagPath(0)


def _extract_mesh(transform, shape):
    """The mesh of `shape` in world space, as the link sends it."""
    om = _om()
    fn = om.MFnMesh(_dag(shape))
    positions = array("f")
    for p in fn.getFloatPoints(om.MSpace.kWorld):
        positions.extend((p.x, p.y, p.z))
    counts, connects = fn.getVertices()
    if max(counts) > 255:
        raise LinkError("%s has a face with more than 255 corners" % _short(transform))
    uvs = array("f")
    if fn.numUVs() > 0:
        uv_counts, uv_ids = fn.getAssignedUVs()
        if list(uv_counts) == list(counts):  # every face mapped
            us, vs = fn.getUVs()
            for i in uv_ids:
                uvs.extend((us[i], vs[i]))
    return {"name": safe_name(_short(transform)), "positions": positions, "face_sizes": bytearray(counts),
            "indices": list(connects), "uvs": uvs}


def _apply_points(name, count, moved):
    """SculptForge's streamed vertices, in place (world space). Not an undo step: undo in SculptForge."""
    om = _om()
    t = _find_transform(name)
    if not t:
        return
    dag = _dag(_mesh_shape(t))
    fn = om.MFnMesh(dag)
    if fn.numVertices != count:
        return  # (out of step: the stroke's end sends a whole mesh when faces changed)
    to_object = dag.inclusiveMatrixInverse()
    for i, (x, y, z) in moved.items():
        fn.setPoint(i, om.MPoint(x, y, z) * to_object)
    fn.updateSurface()


def _apply_mesh(mesh):
    """A whole mesh from SculptForge: replaces the shape of the object with its name (keeping the transform, parent
    and shading groups), or makes a new object."""
    om = _om()
    cmds = _cmds()
    name = mesh["name"]
    existing = _find_transform(name)
    p = mesh["positions"]
    points = om.MPointArray()
    to_object = _dag(existing).inclusiveMatrixInverse() if existing else om.MMatrix()
    for k in range(0, len(p), 3):
        points.append(om.MPoint(p[k], p[k + 1], p[k + 2]) * to_object)
    fn = om.MFnMesh()
    parent = _dag(existing).node() if existing else om.MObject.kNullObj
    node = fn.create(points, list(mesh["face_sizes"]), list(mesh["indices"]), parent=parent)
    if len(mesh["uvs"]):
        uv = mesh["uvs"]
        fn.setUVs(list(uv[0::2]), list(uv[1::2]))
        fn.assignUVs(list(mesh["face_sizes"]), list(range(len(mesh["indices"]))))
    new_shape = om.MFnDagNode(fn.object()).fullPathName()
    if existing:
        old_shape = _mesh_shape(existing)
        groups = []
        for sg in cmds.listConnections(old_shape, type="shadingEngine") or []:
            if sg not in groups:
                groups.append(sg)
        old_name = _short(old_shape)
        for sg in groups or ["initialShadingGroup"]:
            cmds.sets(new_shape, edit=True, forceElement=sg)
        cmds.delete(old_shape)
        cmds.rename(new_shape, old_name)
        return existing
    t = om.MFnDagNode(node).fullPathName()
    cmds.sets(new_shape, edit=True, forceElement="initialShadingGroup")
    t = cmds.rename(t, ":" + name)
    cmds.rename(_mesh_shape(t), name + "Shape")
    return cmds.ls(t, long=True)[0]


def _link_apply():
    """Main thread: what came over the link. The echo guard holds throughout; none of it is an undo step."""
    _link_apply_queued.clear()
    cmds = _cmds()
    touched = []
    state = cmds.undoInfo(q=True, stateWithoutFlush=True)
    cmds.undoInfo(stateWithoutFlush=False)
    _live.applying += 1
    try:
        for name, mesh, points in _link_inbox.take():
            try:
                if mesh is not None:
                    touched.append(_apply_mesh(mesh))
                if points is not None:
                    _apply_points(name, points[0], points[1])
            except Exception as e:
                _message("Live link: can't apply %s: %s" % (name, e), warning=True)
    finally:
        _live.applying -= 1
        cmds.undoInfo(stateWithoutFlush=state)
    touched = [t for t in touched if t]
    _live.forget(touched)
    _watch(touched)


def link_to_sculptforge(pairing=None):
    """The shelf's Link button: connects to the open SculptForge with its pairing code (asked for, unless given),
    or disconnects."""
    global _link
    if _link is not None and _link.connected:
        _link.close()
        _link = None
        _link_label_update()
        return _message("Live link off")
    cmds = _cmds()
    if pairing is None:
        answer = cmds.promptDialog(title="SculptForge Live link",
                                   message="In SculptForge: File > Maya > Live link, then Copy pairing code. Paste it here:",
                                   button=["Link", "Cancel"], defaultButton="Link", cancelButton="Cancel",
                                   dismissString="Cancel")
        if answer != "Link":
            return None
        pairing = cmds.promptDialog(query=True, text=True)
    try:
        code, endpoint = parse_pairing(pairing)
        conn = _PipeConn(endpoint) if os.name == "nt" else _SocketConn(endpoint)
        _link = LinkClient(conn, code, _link_handle, _link_tick, "Maya " + str(cmds.about(version=True))[:32])
    except Exception as e:
        _link = None
        _link_label_update()
        return _message("Live link: %s" % (e if isinstance(e, LinkError) else "SculptForge isn't offering it (%s)" % e),
                        warning=True)
    _watch([t for t, _ in _selected_meshes()])
    _link_label_update()
    return _message("Live link on: connected to SculptForge %s" % _link.server)


def _link_label_update():
    cmds = _cmds()
    label = "Linked" if _link is not None and _link.connected else "Link"
    try:
        if cmds.shelfButton(LINK_BUTTON, exists=True):
            cmds.shelfButton(LINK_BUTTON, edit=True, label=label, imageOverlayLabel=label)
    except Exception:
        pass


def install_shelf():
    """Creates the SculptForge shelf: Send, Get, Auto, Live, Link and Folder buttons. Run once, after putting this file in a
    folder on Maya's script path (README.md)."""
    cmds = _cmds()
    import maya.mel as mel
    top = mel.eval("$sculptforgeTmp = $gShelfTopLevel")
    if cmds.shelfLayout(SHELF, exists=True):
        cmds.deleteUI(SHELF, layout=True)
    shelf = cmds.shelfLayout(SHELF, parent=top)
    buttons = [
        ("Send", "Send the selected meshes to SculptForge", "send_to_sculptforge"),
        ("Get", "Get what SculptForge sent (replaces meshes by name)", "get_from_sculptforge"),
        (_auto_label(), "Auto: get what SculptForge sends by itself (click to turn on or off)", "toggle_auto_get"),
        (_live_label(), "Live: send the meshes you edit by themselves, and get by itself (click to turn on or off)",
         "toggle_live"),
        ("Link", "Live link: connect to the open SculptForge with its pairing code, so Maya follows your strokes "
                 "(click again to disconnect)", "link_to_sculptforge"),
        ("Folder", "Choose the exchange folder (the same one as in SculptForge)", "choose_folder"),
    ]
    for label, tip, func in buttons:
        name = {"toggle_auto_get": [AUTO_BUTTON], "toggle_live": [LIVE_BUTTON],
                "link_to_sculptforge": [LINK_BUTTON]}.get(func, [])
        cmds.shelfButton(*name, parent=shelf, label=label, imageOverlayLabel=label, annotation=tip,
                         image="pythonFamily.png", sourceType="python",
                         command="import sculptforge_maya\nsculptforge_maya.%s()" % func)
    try:
        mel.eval("saveAllShelves $gShelfTopLevel")
    except Exception:
        pass
    start_auto_get()
    return _message("SculptForge shelf installed")
