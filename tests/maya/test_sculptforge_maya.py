# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 tests/maya/test_sculptforge_maya.py [--cli path/to/sculptforge-cli]
#
# The Maya shelf script (integrations/maya/sculptforge_maya.py, #42) without Maya: its manifest code with plain
# Python, and Send / Get against a small mocked maya.cmds (a scene of transforms and mesh shapes whose OBJ
# "export" and "import" read and write real files). With --cli, what the script sends goes through SculptForge
# (maya_receive, then maya_send) and comes back into the mocked scene, replacing the meshes by name.
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "integrations", "maya"))

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAILED: " + what)


# A cube of six quads with UVs, as Maya's OBJ exporter writes one (world space).
CUBE_OBJ = """# Maya OBJ export
v -1 -1 1
v 1 -1 1
v -1 1 1
v 1 1 1
v -1 1 -1
v 1 1 -1
v -1 -1 -1
v 1 -1 -1
vt 0 0
vt 1 0
vt 0 1
vt 1 1
f 1/1 2/2 4/4 3/3
f 3/1 4/2 6/4 5/3
f 5/1 6/2 8/4 7/3
f 7/1 8/2 2/4 1/3
f 2/1 8/2 6/4 4/3
f 7/1 1/2 3/4 5/3
"""


class FakeCmds(types.ModuleType):
    """Just enough of maya.cmds for the shelf script: nodes are long paths ("|grp|pCube1")."""

    def __init__(self, dialog_folder):
        super().__init__("maya.cmds")
        self.nodes = {}  # long path -> {"type", "obj" (mesh text), "sgs"}
        self.selection = []
        self.option_vars = {}
        self.dialog_folder = dialog_folder
        self.plugins = set()
        self.undo = []
        self.calls = []
        self.namespaces = set()
        self.warnings = []

    # scene helpers
    def add_mesh(self, transform, obj_text, sgs=("blinn1SG",)):
        self.nodes[transform] = {"type": "transform"}
        shape = transform + "|" + transform.split("|")[-1] + "Shape"
        self.nodes[shape] = {"type": "mesh", "obj": obj_text, "sgs": list(sgs)}
        return shape

    def _long(self, name):
        if name in self.nodes:
            return name
        hits = [n for n in self.nodes if n.split("|")[-1] == name]
        return hits[0] if hits else None

    # maya.cmds
    def optionVar(self, q=None, exists=None, sv=None, iv=None):
        if exists is not None:
            return exists in self.option_vars
        if q is not None:
            return self.option_vars.get(q, "")
        for pair in (sv, iv):
            if pair:
                self.option_vars[pair[0]] = pair[1]

    def fileDialog2(self, **kw):
        return [self.dialog_folder] if self.dialog_folder else None

    def warning(self, text):
        self.warnings.append(text)

    def inViewMessage(self, **kw):
        pass

    def pluginInfo(self, name, q=False, loaded=False):
        return name in self.plugins

    def loadPlugin(self, name, quiet=False):
        self.plugins.add(name)

    def about(self, version=False):
        return "2026"

    def currentUnit(self, q=False, linear=False):
        return "cm"

    def ls(self, *names, selection=False, long=False, objectsOnly=False, type=None):
        if selection:
            return list(self.selection)
        out = []
        for n in names:
            if isinstance(n, (list, tuple)):
                out += self.ls(*n, long=long, type=type)
                continue
            for node, data in self.nodes.items():
                if (node == n or node.split("|")[-1] == n) and (type is None or data["type"] == type):
                    out.append(node)
        return out

    def objExists(self, n):
        return self._long(n) is not None

    def nodeType(self, n):
        return self.nodes[self._long(n)]["type"]

    def listRelatives(self, n, shapes=False, parent=False, type=None, noIntermediate=False, fullPath=False):
        node = self._long(n)
        if parent:
            return [node.rsplit("|", 1)[0]] if node.count("|") > 1 else None
        kids = [k for k in self.nodes if k.rsplit("|", 1)[0] == node and self.nodes[k]["type"] == "mesh"]
        return kids or None

    def select(self, *items, replace=False, clear=False):
        if clear or not items or items[0] is None:
            self.selection = []
            return
        sel = items[0] if isinstance(items[0], (list, tuple)) else list(items)
        self.selection = [self._long(s) for s in sel]

    def file(self, path, i=False, force=False, options="", type="", exportSelected=False, returnNewNodes=False,
             **kw):
        self.calls.append(("file", type, options))
        if type == "OBJexport":
            assert "objExport" in self.plugins and exportSelected and len(self.selection) == 1
            shape = self.selection[0]
            assert self.nodes[shape]["type"] == "mesh", "the script selects the shape alone"
            with open(path, "w") as f:
                f.write(self.nodes[shape]["obj"])
            return path
        assert i and type == "OBJ" and returnNewNodes
        with open(path) as f:
            text = f.read()
        ns = kw.get("namespace", "")
        self.namespaces.add(ns)
        t = "|%s:polySurface1" % ns
        self.nodes[t] = {"type": "transform"}
        self.nodes[t + "|%s:polySurfaceShape1" % ns] = {"type": "mesh", "obj": text, "sgs": ["initialShadingGroup"]}
        return [t, t + "|%s:polySurfaceShape1" % ns]

    def listConnections(self, n, type=None):
        return list(self.nodes[self._long(n)].get("sgs", []))

    def getAttr(self, attr):
        return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]

    def xform(self, n, matrix=None, worldSpace=False):
        self.calls.append(("xform", self._long(n)))

    def makeIdentity(self, n, **kw):
        self.calls.append(("makeIdentity", self._long(n)))

    def parent(self, child, new_parent, shape=False, relative=False):
        assert shape and relative
        old, p = self._long(child), self._long(new_parent)
        moved = p + "|" + old.split("|")[-1]
        self.nodes[moved] = self.nodes.pop(old)
        return [moved.split("|")[-1]]

    def sets(self, n, edit=False, forceElement=None):
        node = self.nodes[self._long(n)]
        node["sgs"] = [forceElement] if node.get("sgs") == ["initialShadingGroup"] else node["sgs"] + [forceElement]

    def delete(self, n):
        node = self._long(n)
        for k in [k for k in self.nodes if k == node or k.startswith(node + "|")]:
            del self.nodes[k]

    def rename(self, n, new):
        old = self._long(n)
        new = new.lstrip(":")
        moved = old.rsplit("|", 1)[0] + "|" + new
        for k in [k for k in self.nodes if k == old or k.startswith(old + "|")]:
            self.nodes[moved + k[len(old):]] = self.nodes.pop(k)
        return new

    def namespace(self, exists=None, removeNamespace=None, mergeNamespaceWithRoot=False):
        if exists is not None:
            return exists in self.namespaces
        self.namespaces.discard(removeNamespace)

    def undoInfo(self, openChunk=False, closeChunk=False, chunkName=""):
        self.undo.append("open" if openChunk else "close")

    def shelfLayout(self, name, exists=False, parent=None):
        if exists:
            return name in self.option_vars.get("_shelves", {})
        self.option_vars.setdefault("_shelves", {})[name] = []
        return name

    def deleteUI(self, name, layout=False):
        self.option_vars["_shelves"].pop(name)

    def shelfButton(self, parent=None, command="", sourceType="", **kw):
        self.option_vars["_shelves"][parent].append((sourceType, command))


def install_fake_maya(folder):
    cmds = FakeCmds(folder)
    mel = types.ModuleType("maya.mel")
    mel.eval = lambda text: "ShelfLayout"
    maya = types.ModuleType("maya")
    maya.cmds = cmds
    maya.mel = mel
    sys.modules["maya"] = maya
    sys.modules["maya.cmds"] = cmds
    sys.modules["maya.mel"] = mel
    return cmds


def test_shelf(sm):
    cmds = install_fake_maya("")
    sm.install_shelf()
    sm.install_shelf()  # again: replaces the shelf
    buttons = cmds.option_vars["_shelves"][sm.SHELF]
    check(len(buttons) == 3 and all(t == "python" for t, _ in buttons), "the shelf has three Python buttons")
    for _, command in buttons:
        func = command.split("sculptforge_maya.")[1].rstrip("()")
        check(callable(getattr(sm, func, None)), "shelf button calls sculptforge_maya.%s" % func)


def faces(obj_text):
    return [line.split()[1:] for line in obj_text.splitlines() if line.startswith("f ")]


def test_format(sm, tmp):
    check(sm.safe_name("pCube1") == "pCube1" and sm.safe_name("Body Hi") == "Body_Hi", "safe names")
    check(sm.safe_name("3D") == "_3D" and sm.safe_name("") == "SubTool", "safe names: digits, empty")
    check(sm.safe_name("Body Hi \u00fc") == "Body_Hi___", "safe names match SculptForge's (one '_' per byte)")
    check(len(sm.safe_name("x" * 1000)) == sm.MAX_NAME, "names are capped")
    for bad in ["", ".obj", "../x.obj", "a..b.obj", "/etc/x.obj", "C:\\x.obj", "c:x.obj", "a/b.obj", "x.mel",
                "x.py", "x obj.obj", ".hidden.obj", "x" * 70 + ".obj"]:
        check(not sm.file_name_ok(bad), "file name refused: %r" % bad)
    check(sm.file_name_ok("r1_000.obj") and sm.file_name_ok("A-b.c.OBJ"), "plain file names accepted")

    text = sm.write_manifest([("pCube1", "r3_000.obj"), ("Body_Hi", "r3_001.obj")], 3, "2026", "cm")
    m = sm.parse_manifest(text)
    check(m["revision"] == 3 and m["objects"] == [("pCube1", "r3_000.obj"), ("Body_Hi", "r3_001.obj")],
          "manifest round trip")

    def refused(text, what):
        try:
            sm.parse_manifest(text)
            check(False, "refused: " + what)
        except ValueError:
            pass

    base = '{"format": "sculptforge-maya", "version": 1, "revision": 1, "objects": [%s]}'
    for bad in ["../x.obj", "/tmp/x.obj", "C:\\\\x.obj", "sub/x.obj", "x.mel", ".."]:
        refused(base % ('{"name": "a", "file": "%s"}' % bad), "traversal " + bad)
    refused("not json", "not JSON")
    refused("[" * 5000 + "]" * 5000, "deep nesting")
    refused('{"format": "sculptforge-maya", "version": 2, "revision": 1, "objects": []}', "newer version")
    refused('{"format": "sculptforge-maya", "version": 1, "revision": -1, "objects": []}', "negative revision")
    refused('{"format": "sculptforge-maya", "version": 1, "revision": true, "objects": []}', "bool revision")
    refused('{"format": "sculptforge-maya", "version": 1, "revision": 1}', "no objects")
    refused(base % '{"name": 1, "file": "a.obj"}', "name not a string")
    refused(base % '{"name": "a", "file": "a.obj"}, {"name": "b", "file": "a.obj"}', "a file twice")
    refused(base % ", ".join('{"name": "a", "file": "f%d.obj"}' % i for i in range(sm.MAX_OBJECTS + 1)), "too many")
    refused(base % "" + " " * sm.MAX_MANIFEST_BYTES, "too big")

    # object_file: only plain files directly inside the box.
    box = os.path.join(tmp, "box")
    os.makedirs(box)
    with open(os.path.join(tmp, "outside.obj"), "w") as f:
        f.write(CUBE_OBJ)
    if hasattr(os, "symlink"):
        try:
            os.symlink(os.path.join(tmp, "outside.obj"), os.path.join(box, "link.obj"))
            try:
                sm.object_file(box, "link.obj")
                check(False, "a link out of the folder is refused")
            except ValueError:
                pass
        except OSError:
            pass  # no symlinks here (Windows without the privilege)
    try:
        sm.object_file(box, "missing.obj")
        check(False, "a missing file is refused")
    except ValueError:
        pass


def test_send_and_get(sm, tmp, cli):
    folder = os.path.join(tmp, "exchange")
    os.makedirs(folder)
    cmds = install_fake_maya(folder)
    cube_shape = cmds.add_mesh("|pCube1", CUBE_OBJ)
    cmds.nodes["|grp"] = {"type": "transform"}
    cmds.add_mesh("|grp|Body", CUBE_OBJ.replace("v -1 -1 1", "v -1.5 -1 1"), sgs=("skinSG",))
    cmds.selection = ["|pCube1", "|grp|Body|BodyShape"]  # a transform and a shape

    sm.send_to_sculptforge()
    check(not cmds.warnings, "send: no warnings (%s)" % cmds.warnings)
    check(cmds.option_vars.get(sm.FOLDER_VAR) == folder, "the folder picked in the dialog is remembered")
    box = os.path.join(folder, sm.TO_SCULPTFORGE)
    m = sm.read_manifest(box)
    check(m is not None and m["revision"] == 1 and [n for n, _ in m["objects"]] == ["pCube1", "Body"],
          "send: manifest lists the selection by short name")
    check(all(os.path.isfile(os.path.join(box, f)) for _, f in m["objects"]), "send: one OBJ per mesh")
    check(cmds.selection == ["|pCube1", "|grp|Body|BodyShape"], "send: the selection is restored")
    check(("file", "OBJexport", sm.OBJ_EXPORT_OPTIONS) in cmds.calls, "send: Maya's OBJ exporter, no groups")
    victim = os.path.join(tmp, "victim.txt")
    with open(victim, "w") as f:
        f.write("keep me")
    try:  # a link planted where the next send writes is replaced, not written through
        os.symlink(victim, os.path.join(box, "r2_000.obj"))
        os.symlink(victim, os.path.join(box, "manifest.json.tmp"))
    except (OSError, AttributeError):
        pass
    sm.send_to_sculptforge()
    with open(victim) as f:
        check(f.read() == "keep me", "send: a planted link is not written through")
    m2 = sm.read_manifest(box)
    check(m2["revision"] == 2 and not os.path.exists(os.path.join(box, m["objects"][0][1])),
          "a second send: next revision, old files removed")

    out = os.path.join(folder, sm.TO_MAYA)
    if cli:
        r = subprocess.run([cli, "-c", 'maya_receive folder="%s"; maya_send folder="%s"' % (folder, folder)],
                           capture_output=True, text=True)
        check(r.returncode == 0, "sculptforge-cli maya_receive; maya_send (%s %s)" % (r.stdout, r.stderr))
    else:  # what SculptForge's maya_send writes
        os.makedirs(out)
        for name in ("r1_000.obj", "r1_001.obj"):
            with open(os.path.join(out, name), "w") as f:
                f.write(CUBE_OBJ.replace("# Maya OBJ export", "# SculptForge"))
        sm.write_atomically(os.path.join(out, "manifest.json"),
                            sm.write_manifest([("pCube1", "r1_000.obj"), ("Body", "r1_001.obj")], 1)
                            .replace('"maya"', '"sculptforge"'))
    sent = sm.read_manifest(out)
    check(sent is not None and [n for n, _ in sent["objects"]] == ["pCube1", "Body"],
          "SculptForge sends the objects back under their Maya names")

    # A new object in SculptForge too: it becomes a new Maya object.
    shutil.copy(os.path.join(out, sent["objects"][0][1]), os.path.join(out, "r9_extra.obj"))
    with open(os.path.join(out, "manifest.json")) as f:
        data = json.load(f)
    data["objects"].append({"name": "Extra", "file": "r9_extra.obj"})
    sm.write_atomically(os.path.join(out, "manifest.json"), json.dumps(data))

    cmds.undo = []
    sm.get_from_sculptforge()
    check(not cmds.warnings, "get: no warnings (%s)" % cmds.warnings)
    check(cmds.undo == ["open", "close"], "get: one undo chunk")
    shape = cmds.nodes.get("|pCube1|pCube1Shape")
    check(shape is not None and shape["obj"] != CUBE_OBJ, "get: pCube1 keeps its transform and shape name, new mesh")
    check(shape is not None and shape["sgs"] == ["blinn1SG"], "get: pCube1 keeps its shading group")
    check(shape is not None and len(faces(shape["obj"])) == 6 and all(len(f) == 4 for f in faces(shape["obj"])),
          "get: six quads come back as six quads")
    check(shape is not None and all("/" in v for f in faces(shape["obj"]) for v in f), "get: UVs come back")
    body = cmds.nodes.get("|grp|Body|BodyShape")
    check(body is not None and body["sgs"] == ["skinSG"], "get: Body stays under its parent with its shading group")
    check(("xform", "|sculptforge_get:polySurface1") in cmds.calls and any(c[0] == "makeIdentity" for c in cmds.calls),
          "get: the world-space import is moved into the object's space")
    check("|Extra" in cmds.nodes and "|Extra|ExtraShape" in cmds.nodes, "get: a new object is created")
    check(not any("sculptforge_get" in n for n in cmds.nodes), "get: nothing left in the import namespace")
    check(cmds.option_vars.get(sm.REVISION_VAR) == sent["revision"], "get: the revision is remembered")

    # A manifest that points outside the folder: nothing is imported.
    before = dict(cmds.nodes)
    sm.write_atomically(os.path.join(out, "manifest.json"),
                        '{"format": "sculptforge-maya", "version": 1, "revision": 99, '
                        '"objects": [{"name": "pCube1", "file": "../outside.obj"}]}')
    cmds.warnings = []
    sm.get_from_sculptforge()
    check(cmds.warnings and cmds.nodes == before, "get: a traversal attempt is refused and nothing changes")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", help="sculptforge-cli, for the round trip through SculptForge")
    args = ap.parse_args()
    import sculptforge_maya as sm
    tmp = tempfile.mkdtemp(prefix="sf_maya_")
    try:
        test_format(sm, tmp)
        test_send_and_get(sm, tmp, args.cli)
        test_shelf(sm)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("%s: %d failure(s)" % (os.path.basename(__file__), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
