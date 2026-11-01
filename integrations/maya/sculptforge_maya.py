# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""SculptForge bridge for Maya (SculptForge issue #42).

Sends the selected meshes from Maya to SculptForge and gets them back, through ONE exchange folder that you
pick in both apps (the way GoZ works between ZBrush and other apps). Nothing else is shared: no ports, sockets
or servers, and neither side runs anything it finds in the folder. Meshes travel as OBJ (quads and UVs, world
space, Maya's own units, one object per file); a small JSON manifest per direction lists the objects:

    <folder>/maya_to_sculptforge/manifest.json + r<rev>_<k>.obj   written here (Send to SculptForge)
    <folder>/sculptforge_to_maya/manifest.json + r<rev>_<k>.obj   written by SculptForge (File > Maya > Send to Maya)

Get from SculptForge replaces each object's mesh by name, keeping its transform, parent and shading groups, as
one undo step; objects Maya doesn't have yet are created. Plain maya.cmds (and maya.mel for the shelf); no other
modules. Install: see README.md beside this file, or run install_shelf() once.
"""

import json
import os
import re

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
        return "SubTool"
    if s[0].isdigit():
        s = ("_" + s)[:MAX_NAME]
    return s


def file_name_ok(name):
    """A file a manifest may list: a plain *.obj name inside the manifest's own folder."""
    return (isinstance(name, str) and 4 < len(name) <= MAX_FILE_NAME and not name.startswith(".")
            and ".." not in name and _FILE_CHARS.match(name) is not None and name.lower().endswith(".obj"))


def write_manifest(objects, revision, sender_version="", unit=""):
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
    for key in ("sender", "sender_version", "unit"):
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
    return {"revision": int(revision), "sender": data.get("sender", ""), "objects": out}


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
    cmds = _cmds()
    meshes = _selected_meshes()
    if not meshes:
        return _message("select one or more meshes to send", warning=True)
    if len(meshes) > MAX_OBJECTS:
        return _message("too many meshes selected (at most %d)" % MAX_OBJECTS, warning=True)
    folder = exchange_folder()
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
    try:
        for k, ((transform, shape), name) in enumerate(zip(meshes, names)):
            file_name = "%s%03d.obj" % (prefix, k)
            remove_entry(os.path.join(box, file_name))
            cmds.select(shape, replace=True)  # the shape alone: not the transform's children
            cmds.file(os.path.join(box, file_name), force=True, options=OBJ_EXPORT_OPTIONS, type="OBJexport",
                      preserveReferences=True, exportSelected=True)
            objects.append((name, file_name))
    finally:
        if selection:
            cmds.select(selection, replace=True)
        else:
            cmds.select(clear=True)
    write_atomically(os.path.join(box, "manifest.json"),
                     write_manifest(objects, revision, cmds.about(version=True), cmds.currentUnit(q=True, linear=True)))
    remove_old_sends(box, prefix)
    return _message("sent %d mesh%s to SculptForge" % (len(objects), "" if len(objects) == 1 else "es"))


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
    try:
        manifest = read_manifest(box) if os.path.isdir(box) and not os.path.islink(box) else None
    except ValueError as e:
        return _message("can't read what SculptForge sent: %s" % e, warning=True)
    if not manifest or not manifest["objects"]:
        return _message("nothing has been sent from SculptForge yet (File > Maya > Send to Maya)", warning=True)
    _ensure_obj_plugin()
    done = []
    cmds.undoInfo(openChunk=True, chunkName="Get from SculptForge")
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
        cmds.undoInfo(closeChunk=True)
    cmds.optionVar(iv=(REVISION_VAR, manifest["revision"]))
    cmds.select([d for d in done if cmds.objExists(d)] or None, replace=True)
    return _message("got %d mesh%s from SculptForge" % (len(done), "" if len(done) == 1 else "es"))


def install_shelf():
    """Creates the SculptForge shelf: Send, Get and Folder buttons. Run once, after putting this file in a
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
        ("Folder", "Choose the exchange folder (the same one as in SculptForge)", "choose_folder"),
    ]
    for label, tip, func in buttons:
        cmds.shelfButton(parent=shelf, label=label, imageOverlayLabel=label, annotation=tip,
                         image="pythonFamily.png", sourceType="python",
                         command="import sculptforge_maya\nsculptforge_maya.%s()" % func)
    try:
        mel.eval("saveAllShelves $gShelfTopLevel")
    except Exception:
        pass
    return _message("SculptForge shelf installed")
