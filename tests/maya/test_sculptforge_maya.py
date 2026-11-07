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

    def shelfButton(self, *name, parent=None, command="", sourceType="", exists=False, edit=False, **kw):
        if exists:
            return any(name and b[2] == name[0] for b in self.option_vars.get("_shelves", {}).get("SculptForge", []))
        if edit:
            return
        self.option_vars["_shelves"][parent].append((sourceType, command, name[0] if name else kw.get("label")))


def install_fake_maya(folder):
    cmds = FakeCmds(folder)
    mel = types.ModuleType("maya.mel")
    mel.eval = lambda text: "ShelfLayout"
    utils = types.ModuleType("maya.utils")
    cmds.deferred = []
    utils.executeDeferred = cmds.deferred.append  # Maya runs these in its main thread when idle; tests by hand
    maya = types.ModuleType("maya")
    maya.cmds = cmds
    maya.mel = mel
    maya.utils = utils
    sys.modules["maya"] = maya
    sys.modules["maya.cmds"] = cmds
    sys.modules["maya.mel"] = mel
    sys.modules["maya.utils"] = utils
    return cmds


def test_shelf(sm):
    cmds = install_fake_maya("")
    sm.install_shelf()
    sm.install_shelf()  # again: replaces the shelf
    buttons = cmds.option_vars["_shelves"][sm.SHELF]
    check(len(buttons) == 6 and all(t == "python" for t, _, _ in buttons), "the shelf has six Python buttons")
    check(any(n == sm.LIVE_BUTTON for _, _, n in buttons) and any(n == sm.LINK_BUTTON for _, _, n in buttons),
          "the Live and Link buttons have their names (their labels show on / off)")
    check(any(n == sm.AUTO_BUTTON for _, _, n in buttons), "the Auto button has its name (its label shows on / off)")
    for _, command, _ in buttons:
        func = command.split("sculptforge_maya.")[1].rstrip("()")
        check(callable(getattr(sm, func, None)), "shelf button calls sculptforge_maya.%s" % func)


def faces(obj_text):
    return [line.split()[1:] for line in obj_text.splitlines() if line.startswith("f ")]


def test_format(sm, tmp):
    check(sm.safe_name("pCube1") == "pCube1" and sm.safe_name("Body Hi") == "Body_Hi", "safe names")
    check(sm.safe_name("3D") == "_3D" and sm.safe_name("") == "Part", "safe names: digits, empty")
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


def write_sculptforge_send(sm, out, revision, names):
    """What SculptForge's maya_send writes: the OBJs, then the manifest."""
    os.makedirs(out, exist_ok=True)
    objects = []
    for k, name in enumerate(names):
        file_name = "r%d_%03d.obj" % (revision, k)
        with open(os.path.join(out, file_name), "w") as f:
            f.write(CUBE_OBJ.replace("# Maya OBJ export", "# SculptForge r%d" % revision))
        objects.append((name, file_name))
    sm.write_atomically(os.path.join(out, "manifest.json"),
                        sm.write_manifest(objects, revision).replace('"maya"', '"sculptforge"'))


def test_auto(sm, tmp):
    """Auto: what SculptForge sends after the shelf is used loads by itself (in Maya's main thread); what was
    there before waits for Get; Auto off leaves it to Get. Only the manifest's time and size are checked."""
    folder = os.path.join(tmp, "auto")
    os.makedirs(folder)
    cmds = install_fake_maya(folder)
    sm.POLL_SECONDS = 3600  # the thread never ticks here: the test polls by hand
    cmds.add_mesh("|pCube1", CUBE_OBJ)
    out = os.path.join(folder, sm.TO_MAYA)
    write_sculptforge_send(sm, out, 1, ["pCube1"])  # sent before Maya's shelf was used: old news
    cmds.selection = ["|pCube1"]
    sm.send_to_sculptforge()
    w = sm._watcher
    check(w is not None and w.thread.daemon and w.box == out, "auto: on by default, watching SculptForge's box only")
    check(not w.poll() and not cmds.deferred, "auto: what was there before isn't loaded")

    write_sculptforge_send(sm, out, 2, ["pCube1"])
    os.utime(os.path.join(out, "manifest.json"), ns=(1, 2_000_000_000))  # (coarse clocks: a time of its own)
    check(w.poll() and not w.poll(), "auto: a new send is seen once")
    w.defer(w.on_change)
    check(len(cmds.deferred) == 1, "auto: the Get is handed to Maya's main thread")
    cmds.deferred.pop()()
    shape = cmds.nodes["|pCube1|pCube1Shape"]
    check("# SculptForge r2" in shape["obj"], "auto: the new send replaced pCube1's mesh")
    check(cmds.option_vars.get(sm.REVISION_VAR) == 2, "auto: the revision is remembered")
    shape["obj"] = CUBE_OBJ
    sm._auto_get_new()  # the same revision again (a touch of the manifest): not loaded twice
    check(cmds.nodes["|pCube1|pCube1Shape"]["obj"] == CUBE_OBJ, "auto: a revision already loaded isn't loaded again")

    with open(os.path.join(out, "manifest.json"), "w") as f:
        f.write('{"format": "sculptforge-maya", "ver')  # half written / not valid: nothing happens, no warning
    cmds.warnings = []
    sm._auto_get_new()
    check(not cmds.warnings and cmds.nodes["|pCube1|pCube1Shape"]["obj"] == CUBE_OBJ, "auto: a bad manifest is skipped")

    sm.toggle_auto_get()
    check(not sm.auto_get_on() and sm._watcher is None and w.stopped.is_set(), "auto off: the thread stops")
    write_sculptforge_send(sm, out, 3, ["pCube1"])
    sm._auto_get_new()
    check(cmds.nodes["|pCube1|pCube1Shape"]["obj"] == CUBE_OBJ, "auto off: nothing loads by itself")
    sm.toggle_auto_get()
    check(sm.auto_get_on() and sm._watcher is not None and not sm._watcher.poll(), "auto on again: from now on")
    sm._watcher.stop()


class Clock(object):
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def test_live(sm, tmp):
    """Live: the wait for stillness, the echo guard, sends of the changed meshes only, and never loading what this
    Maya session wrote."""
    clock = Clock()
    live = sm.LiveState(clock=clock)
    check(not live.due(), "live: nothing changed, nothing due")
    live.mark("|a")
    clock.t += 0.1
    check(not live.due(), "live: 100 ms after a change: still waiting")
    live.mark("|b")
    clock.t += 0.25
    check(not live.due(), "live: another change restarts the wait")
    clock.t += 0.1
    check(live.due() and live.take() == ["|a", "|b"] and not live.due(), "live: 300 ms of stillness: due, once")
    live.applying += 1
    live.mark("|a")
    live.applying -= 1
    clock.t += 1
    check(not live.due(), "live: changes while a Get applies don't count (the echo guard)")

    folder = os.path.join(tmp, "live")
    os.makedirs(folder)
    cmds = install_fake_maya(folder)
    sm.POLL_SECONDS = 3600
    sm._live = sm.LiveState(clock=clock)
    cmds.add_mesh("|pCube1", CUBE_OBJ)
    cmds.add_mesh("|pCube2", CUBE_OBJ)
    cmds.selection = ["|pCube1", "|pCube2"]
    sm.send_to_sculptforge()  # (watched from now on)
    box = os.path.join(folder, sm.TO_SCULPTFORGE)
    m = sm.read_manifest(box)
    check(m["writer"] == sm.WRITER and not m["live"], "send: the manifest says which Maya session wrote it")
    check({"|pCube1", "|pCube2"} <= set(sm._watched), "live: what was sent is watched")

    sm.toggle_live()
    check(sm.live_on() and sm.auto_get_on(), "live on (and Auto with it)")
    cmds.nodes["|pCube2|pCube2Shape"]["obj"] = CUBE_OBJ.replace("v 1 1 1", "v 1 1.5 1")
    sm._live.mark("|pCube2")  # (Maya's dirty callback)
    clock.t += 0.5
    check(sm._live.due(), "live: due after the edit")
    sm._live_send()
    m2 = sm.read_manifest(box)
    check(m2["revision"] == m["revision"] + 1 and [n for n, _ in m2["objects"]] == ["pCube2"] and m2["live"],
          "live: only the edited mesh is sent")
    check(cmds.selection == ["|pCube1", "|pCube2"], "live: the selection is left as it was")

    # A Get of what SculptForge sends doesn't send it back.
    out = os.path.join(folder, sm.TO_MAYA)
    write_sculptforge_send(sm, out, 1, ["pCube1"])
    sm.get_from_sculptforge()
    clock.t += 1
    check(not sm._live.due(), "live: a Get is not an edit to send back")

    # A manifest this Maya wrote (the user picked one folder as both): never loaded.
    sm.write_atomically(os.path.join(out, "manifest.json"),
                        sm.write_manifest([("pCube1", "r1_000.obj")], 7, writer=sm.WRITER))
    before = cmds.nodes["|pCube1|pCube1Shape"]["obj"]
    cmds.nodes["|pCube1|pCube1Shape"]["obj"] = "mine"
    sm._auto_get_new()
    check(cmds.nodes["|pCube1|pCube1Shape"]["obj"] == "mine", "auto: what this Maya session wrote isn't loaded")
    cmds.nodes["|pCube1|pCube1Shape"]["obj"] = before
    sm.toggle_live()
    check(not sm.live_on(), "live off")
    sm._live.mark("|pCube1")
    clock.t += 1
    check(sm._live_send() is None, "live off: nothing is sent by itself")
    if sm._watcher:
        sm._watcher.stop()


def test_link_codec(sm):
    """The Live link's frames: the same bytes as SculptForge's (tests/maya/data, read by test_maya_live.cpp), strict
    parsing, and random input refused without anything but LinkError."""
    data = os.path.join(HERE, "data")
    mesh = {"name": "pPlane1", "positions": [0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 0, 0], "face_sizes": [4, 3],
            "indices": [0, 1, 2, 3, 1, 4, 2], "uvs": [0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1]}
    with open(os.path.join(data, "link_mesh.bin"), "rb") as f:
        check(f.read() == sm.encode_mesh(mesh), "link: the shelf writes SculptForge's Mesh bytes")
    with open(os.path.join(data, "link_points.bin"), "rb") as f:
        golden = f.read()
    check(golden == sm.encode_points("Body", 5, [1, 4], [0.5, 0.25, -1, 2, 3, 4]), "link: and its Points bytes")
    kind, length = sm.parse_header(golden[:16])
    name, count, indices, positions = sm.parse_points(golden[16:])
    check(kind == sm.LINK_POINTS and name == "Body" and count == 5 and list(indices) == [1, 4], "link: points read")
    back = sm.parse_mesh(sm.encode_mesh(mesh)[16:])
    check(list(back["indices"]) == mesh["indices"] and list(back["face_sizes"]) == [4, 3] and len(back["uvs"]) == 14,
          "link: mesh round trip")

    def refused(fn, payload, what):
        try:
            fn(payload)
            check(False, "link refuses " + what)
        except sm.LinkError:
            pass

    for bad, what in [(dict(mesh, indices=[0, 1, 2, 3, 1, 5, 2]), "an index out of range"),
                      (dict(mesh, face_sizes=[2, 5]), "a face of 2"),
                      (dict(mesh, face_sizes=[4, 4]), "faces that don't add up"),
                      (dict(mesh, positions=[float("nan")] + mesh["positions"][1:]), "NaN")]:
        refused(sm.parse_mesh, sm.encode_mesh(bad)[16:], what)
    for name in ["", "../x", "a b", "1abc"]:
        raw = name.encode()
        refused(sm.parse_mesh, struct_name(raw) + sm.encode_mesh(mesh)[16 + 2 + 7:], "the name %r" % name)
    good = sm.encode_mesh(mesh)[16:]
    refused(sm.parse_mesh, good + b"x", "a byte too many")
    for cut in range(len(good)):
        refused(sm.parse_mesh, good[:cut], "a cut at %d" % cut)
    for header, what in [(b"GET / HTTP/1.1\r\n\r\n"[:16], "HTTP"), (b"SFML" + bytes([2, 0, 3, 0]) + bytes(8), "version 2"),
                         (b"SFML" + bytes([1, 0, 9, 0]) + bytes(8), "type 9"),
                         (b"SFML" + bytes([1, 0, 3, 0]) + bytes([0, 0, 0, 0x40, 1, 0, 0, 0]), "over the cap")]:
        refused(sm.parse_header, header, what)
    import random
    rng = random.Random(42)
    for i in range(3000):
        blob = bytearray(good) if i % 2 else bytearray(rng.getrandbits(8) for _ in range(rng.randrange(80)))
        if i % 2:
            for _ in range(rng.randrange(1, 5)):
                blob[rng.randrange(len(blob))] = rng.getrandbits(8)
        for fn in (sm.parse_mesh, sm.parse_points):
            try:
                r = fn(bytes(blob))
                if fn is sm.parse_mesh:
                    check(max(r["indices"]) < len(r["positions"]) // 3, "fuzz: what parses is in range")
            except sm.LinkError:
                pass
            except Exception as e:  # anything else would be a parser bug
                check(False, "fuzz: %r raised %r" % (bytes(blob[:16]), e))

    # Pairing: SculptForge's Copy text or a bare code; only SculptForge's endpoint names.
    code = "0123456789abcdef0123456789ABCDEF"
    shown = "-".join(code[k:k + 8].upper() for k in range(0, 32, 8))
    if os.name != "nt":
        c, e = sm.parse_pairing("sflink1 %s /run/user/1000/sculptforge/maya-link.sock" % shown)
        check(c == code.lower() and e.endswith("maya-link.sock"), "pairing: Copy's text")
        for bad in ["sflink1 %s /etc/passwd" % shown, "sflink1 %s /tmp/../x/maya-link.sock" % shown,
                    "sflink1 %s relative/maya-link.sock" % shown]:
            refused(sm.parse_pairing, bad, "the endpoint in %r" % bad)
    check(sm.parse_pairing(shown)[0] == code.lower(), "pairing: a bare code")
    for bad in ["", "1234", "zz" * 16, "sflink2 %s x" % shown]:
        refused(sm.parse_pairing, bad, "the code %r" % bad)


def struct_name(raw):
    import struct
    return struct.pack("<H", len(raw)) + raw


class PairConn(object):
    """One end of a socket pair, as LinkClient's connection."""

    def __init__(self, sock):
        self.sock = sock

    def recv(self, n, timeout):
        import socket
        self.sock.settimeout(timeout)
        try:
            data = self.sock.recv(n)
        except socket.timeout:
            return b""
        if not data:
            raise ValueError("closed")
        return data

    def send(self, data):
        self.sock.sendall(data)

    def close(self):
        self.sock.close()


def test_link_client(sm):
    """LinkClient against a stand-in SculptForge: the code first, Welcome, then streamed Points fold into one update
    per object, a Mesh replaces them, and Bye ends it."""
    import socket
    import time as _time
    try:
        a, b = socket.socketpair()
    except (AttributeError, OSError):
        return
    code = "0123456789abcdef0123456789abcdef"
    got = []
    inbox = sm.LinkInbox()

    def app():
        f = b.recv(16)
        kind, length = sm.parse_header(f)
        payload = b""
        while len(payload) < length:
            payload += b.recv(length - len(payload))
        got.append((kind, payload[:32]))
        b.sendall(sm.frame(sm.LINK_WELCOME, b"\x06\x000.11.11"))
        b.sendall(sm.encode_points("Body", 10, [1, 2], [1, 1, 1, 2, 2, 2]))
        b.sendall(sm.encode_points("Body", 10, [2, 3], [5, 5, 5, 3, 3, 3]))
        _time.sleep(0.2)
        b.sendall(sm.frame(sm.LINK_BYE))

    import threading
    t = threading.Thread(target=app)
    t.start()
    client = sm.LinkClient(PairConn(a), code, inbox.add)
    check(got and got[0] == (sm.LINK_HELLO, code.encode()), "link client: the pairing code goes first")
    check(client.server == "0.11.11" and client.connected, "link client: Welcome, connected")
    t.join()
    for _ in range(100):
        if not client.connected:
            break
        _time.sleep(0.01)
    check(not client.connected and "off" in client.error, "link client: Bye ends the link")
    taken = inbox.take()
    check(len(taken) == 1 and taken[0][0] == "Body" and taken[0][1] is None, "link inbox: one update for Body")
    count, moved = taken[0][2]
    check(count == 10 and sorted(moved) == [1, 2, 3] and moved[2] == (5, 5, 5), "link inbox: Points fold, later wins")
    inbox.add(sm.LINK_POINTS, sm.encode_points("Body", 10, [1], [0, 0, 0])[16:])
    inbox.add(sm.LINK_MESH, sm.encode_mesh({"name": "Body", "positions": [0, 0, 0, 1, 0, 0, 0, 1, 0], "face_sizes": [3],
                                            "indices": [0, 1, 2], "uvs": []})[16:])
    taken = inbox.take()
    check(taken[0][1] is not None and taken[0][2] is None, "link inbox: a whole mesh drops the Points before it")
    a.close()
    b.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", help="sculptforge-cli, for the round trip through SculptForge")
    args = ap.parse_args()
    import sculptforge_maya as sm
    tmp = tempfile.mkdtemp(prefix="sf_maya_")
    try:
        test_format(sm, tmp)
        test_send_and_get(sm, tmp, args.cli)
        test_auto(sm, tmp)
        test_live(sm, tmp)
        test_link_codec(sm)
        test_link_client(sm)
        test_shelf(sm)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("%s: %d failure(s)" % (os.path.basename(__file__), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
