# SculptForge - Blender bridge: the add-on inside a real Blender (4.2+), headless
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   blender -b --factory-startup -noaudio --python-exit-code 1 --python tests/blender/test_in_blender.py -- \
#       --cli build/sculptforge-cli
#
# Sends a small scene to SculptForge (sculptforge-cli stands in for the app: blender_receive, then
# blender_send), receives it back and checks that the objects are unchanged: positions, n-gons,
# transforms, materials, modifiers, UVs, colours, mask and face sets. Then a topology change (subdivide)
# and a new Part come back as a rebuilt mesh and a new object. The classic keys preference adds and
# removes its keymap items, each bound to a real operator with valid settings. Vector displacement maps that
# SculptForge exports read in Blender with the same orientation, and Blender's EXRs load in SculptForge.
# CMake runs it when `blender` is found.
import os
import subprocess
import sys
import tempfile

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "addons", "blender"))

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLI = argv[argv.index("--cli") + 1] if "--cli" in argv else "sculptforge-cli"
ROOT = tempfile.mkdtemp(prefix="sf_blender_test_")
os.environ["SCULPTFORGE_BLENDER_EXCHANGE"] = ROOT

import addon_utils  # noqa: E402

# Enabled as an add-on (so it has its preferences) straight from the source folder on sys.path.
addon_utils.enable("sculptforge_bridge", default_set=True)
import sculptforge_bridge as bridge  # noqa: E402
from sculptforge_bridge import exchange  # noqa: E402
from sculptforge_bridge import splats as splat_io  # noqa: E402

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def arr(collection, prop, width, dtype=np.float64):
    out = np.empty(len(collection) * width, dtype=dtype)
    collection.foreach_get(prop, out)
    return out.reshape(-1, width) if width > 1 else out


def snapshot(obj):
    me = obj.data
    s = {"co": arr(me.vertices, "co", 3), "matrix": np.array(obj.matrix_world),
         "polys": [tuple(p.vertices) for p in me.polygons], "materials": [m.name for m in me.materials],
         "modifiers": [(m.name, m.show_viewport) for m in obj.modifiers]}
    if me.uv_layers.active:
        s["uv"] = arr(me.uv_layers.active.data, "uv", 2)
    if me.color_attributes.active_color:
        s["color"] = arr(me.color_attributes.active_color.data, "color_srgb", 4)
    if me.attributes.get(".sculpt_face_set"):
        s["sets"] = arr(me.attributes[".sculpt_face_set"].data, "value", 1, np.int32)
    if me.attributes.get(".sculpt_mask"):
        s["mask"] = arr(me.attributes[".sculpt_mask"].data, "value", 1)
    return s


def sculptforge(commands):
    r = subprocess.run([CLI, "-c", "; ".join(c + ' folder="%s"' % ROOT if c.startswith("blender_") else c for c in commands)],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    check(r.returncode == 0, "sculptforge-cli: " + "; ".join(commands) + (" (%s)" % r.stderr.strip() if r.returncode else ""))


def build_scene():
    for obj in list(bpy.data.objects):  # (not read_factory_settings: it would disable the add-on)
        bpy.data.objects.remove(obj)
    mat = bpy.data.materials.new("Clay")
    # A cube (quads, UVs) with a live modifier, rotated and scaled.
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(1.0, 2.0, 0.5), rotation=(0.3, 0.2, 0.9))
    cube = bpy.context.active_object
    cube.name = "Box"
    cube.scale = (1.0, 2.0, 0.5)
    cube.data.materials.append(mat)
    cube.modifiers.new("Bevel", "BEVEL")
    me = cube.data
    colors = me.color_attributes.new("Paint", "FLOAT_COLOR", "POINT")
    rgba = np.random.default_rng(1).integers(0, 256, (len(me.vertices), 4)) / 255.0
    rgba[:, 3] = 1.0
    colors.data.foreach_set("color_srgb", rgba.astype(np.float32).ravel())
    me.color_attributes.active_color = colors
    sets = me.attributes.new(".sculpt_face_set", "INT", "FACE")
    sets.data.foreach_set("value", np.array([1, 1, 2, 2, 3, 3], dtype=np.int32))
    mask = me.attributes.new(".sculpt_mask", "FLOAT", "POINT")
    mask.data.foreach_set("value", (np.arange(len(me.vertices)) / 7.0).astype(np.float32))
    # An n-gon disc, mirrored.
    bpy.ops.mesh.primitive_circle_add(vertices=7, radius=1.0, fill_type="NGON", location=(-3.0, 0.0, 1.0))
    disc = bpy.context.active_object
    disc.name = "Disc ü"
    disc.scale = (-1.0, 1.0, 1.0)
    # Monkey: triangles and quads, with UVs.
    bpy.ops.mesh.primitive_monkey_add(location=(0.0, -3.0, 0.0))
    bpy.context.active_object.name = "Suzanne"
    bpy.ops.object.select_all(action="SELECT")
    return ["Box", "Disc ü", "Suzanne"]


def addon_sculpt_items():
    km = bpy.context.window_manager.keyconfigs.addon.keymaps.get("Sculpt")
    return list(km.keymap_items) if km else []


def test_classic_keys():
    keys = bridge.classic_keys
    p = bridge.prefs()
    before = len(addon_sculpt_items())
    check(not p.classic_keys and not keys.enabled(), "Classic keys: off by default")
    p.classic_keys = True
    table = keys.key_table()
    items = addon_sculpt_items()
    check(len(items) == before + len(table), "Classic keys: one keymap item per key (%d)" % len(table))
    for zkey, what, idname, key, ctrl, shift, alt, value, props in table:
        kmi = next((i for i in items if i.idname == idname and i.type == key and i.ctrl == ctrl and i.shift == shift
                    and i.alt == alt), None)
        check(kmi is not None and kmi.active, "Classic keys: %s -> %s" % (zkey or key, idname))
        mod, fn = idname.split(".")
        try:  # (hasattr is always true on bpy.ops: the operator is looked up when called)
            getattr(getattr(bpy.ops, mod), fn).get_rna_type()
            exists = True
        except (AttributeError, KeyError, RuntimeError):
            exists = False
        check(exists, "Classic keys: operator %s exists" % idname)
        for name, v in props.items():
            got = getattr(kmi.properties, name) if kmi else None
            same = got == v or (isinstance(v, str) and isinstance(got, str) and got.upper() == v.upper()) or \
                (isinstance(v, float) and abs(got - v) < 1e-6)
            check(same, "Classic keys: %s %s = %r" % (idname, name, v))
    check(not keys.skipped, "Classic keys: none skipped %s" % keys.skipped)
    # Every data path the keys use resolves in Sculpt Mode (they changed between Blender 4.2 and 5).
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8)
    bpy.ops.object.mode_set(mode="SCULPT")
    for zkey, what, idname, key, ctrl, shift, alt, value, props in table:
        for name, path in props.items():
            if name in ("data_path_primary", "data_path_secondary", "use_secondary", "rotation_path", "color_path",
                        "image_id") or (idname == "wm.context_toggle" and name == "data_path" and
                                        not path.startswith("space_data")):
                try:
                    bpy.context.path_resolve(path)
                    ok = True
                except ValueError:
                    ok = False
                check(ok, "Classic keys: %s resolves (%s)" % (path, zkey or key))
    check(bpy.types.Menu.bl_rna_get_subclass_py(keys.SCULPTFORGE_MT_classic_brushes.bl_idname) is not None,
          "Classic keys: the brush menu is registered")
    # The brush menu's picks.
    for name, _, asset, sculpt_tool, _ in keys.BRUSHES:
        r = bpy.ops.sculptforge.classic_brush(brush=name)
        brush = bpy.context.tool_settings.sculpt.brush
        got = brush.name if keys.brush_assets() else brush.sculpt_tool
        check(r == {"FINISHED"} and got == (asset if keys.brush_assets() else sculpt_tool),
              "classic brush %s -> Blender %s (%s)" % (name, asset, got))
    # Ctrl+D divides (a Multires level); D / Shift+D step between levels without adding any.
    check(bpy.ops.sculptforge.classic_divide() == {"FINISHED"}, "Ctrl+D: Divide")
    bpy.ops.sculptforge.classic_divide()
    multires = next((m for m in bpy.context.active_object.modifiers if m.type == "MULTIRES"), None)
    check(multires is not None and multires.total_levels == 2, "Ctrl+D in Sculpt Mode adds Multires levels")
    if multires is not None:
        bpy.ops.sculptforge.classic_sdiv(step=-1)
        bpy.ops.sculptforge.classic_sdiv(step=-1)
        bpy.ops.sculptforge.classic_sdiv(step=-1)
        check(multires.sculpt_levels == 0 and multires.total_levels == 2, "Shift+D steps down to Level 0")
        bpy.ops.sculptforge.classic_sdiv(step=1)
        bpy.ops.sculptforge.classic_sdiv(step=1)
        bpy.ops.sculptforge.classic_sdiv(step=1)
        check(multires.sculpt_levels == 2 and multires.total_levels == 2, "D steps up to the top, adds nothing")
    # Ctrl+W (sculptforge.classic_group) runs sculpt operators that need a viewport (mask_flood_fill crashes
    # headless Blender): only its poll is checked here.
    check(bpy.ops.sculptforge.classic_group.poll(), "Ctrl+W: Group is available in Sculpt Mode")
    bpy.ops.object.mode_set(mode="OBJECT")
    p.classic_keys = False
    check(len(addon_sculpt_items()) == before and not keys.enabled(), "Classic keys: turning it off removes them")
    p.classic_keys = True  # (the add-on's unregister removes them too: checked after disabling it)


def read_sfvdm(path):
    with open(path, "rb") as f:
        b = f.read()
    w, h, n = np.frombuffer(b[8:20], dtype="<u4")
    return np.frombuffer(b[20 + n:], dtype="<f4").reshape(h, w, 3)


def test_vdm_exr():
    # SculptForge's Horn as an EXR and as .sfvdm (row 0 = the top of the map, +Y).
    exr = os.path.join(ROOT, "horn.exr")
    raw = os.path.join(ROOT, "horn.sfvdm")
    sculptforge(['vdm_export name=horn path="%s"' % exr, 'vdm_export name=horn path="%s"' % raw])
    ours = read_sfvdm(raw)
    img = bpy.data.images.load(exr)
    img.colorspace_settings.is_data = True
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, img.channels)[::-1, :, :3]  # Blender: bottom row first
    check((w, h) == (256, 256), "VDM EXR: Blender reads 256 x 256")
    check(np.abs(px - ours).max() < 1e-6, "VDM EXR: Blender sees the same vectors, the same way up (max diff %g)"
          % np.abs(px - ours).max())
    # The horn leans toward the top of the image: +Y in Blender's texture space too.
    check(px[128, 128, 1] > 0.3 and px[128, 128, 2] > 0.6, "VDM EXR: the horn's tip points up the map and out")
    # Blender's EXRs (32-bit ZIP, as its VDM Brush Baker writes, and 16-bit) load in SculptForge unchanged.
    rng = np.random.default_rng(3)
    vec = rng.uniform(-0.5, 0.5, (32, 48, 3)).astype(np.float32)  # rows from the top
    for depth, codec, tol in (("32", "ZIP", 1e-6), ("16", "ZIP", 2e-3), ("32", "PIZ", 1e-6)):
        made = bpy.data.images.new("vdm%s%s" % (depth, codec), 48, 32, alpha=True, float_buffer=True, is_data=True)
        rgba = np.ones((32, 48, 4), dtype=np.float32)
        rgba[:, :, :3] = vec
        made.pixels.foreach_set(rgba[::-1].ravel())
        path = os.path.join(ROOT, "blender_%s_%s.exr" % (depth, codec))
        scene = bpy.context.scene
        settings = scene.render.image_settings
        settings.file_format = "OPEN_EXR"
        settings.color_depth = depth
        settings.exr_codec = codec
        made.save_render(path, scene=scene)
        back = os.path.join(ROOT, "blender_%s_%s.sfvdm" % (depth, codec))
        sculptforge(['vdm_set path="%s" save=false' % path, 'vdm_export path="%s"' % back])
        got = read_sfvdm(back) if os.path.exists(back) else np.zeros_like(vec)
        check(got.shape == vec.shape and np.abs(got - vec).max() < tol,
              "VDM EXR: Blender's %s-bit %s EXR loads in SculptForge (max diff %g)" % (depth, codec, np.abs(got - vec).max()))


def sample_splats(n=400, degree=3, seed=5):
    rng = np.random.default_rng(seed)
    q = rng.normal(size=(n, 4))
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    return splat_io.SplatArrays(rng.normal(size=(n, 3)), q, np.exp(rng.uniform(-6, -1, (n, 3))), rng.uniform(0.02, 0.98, n),
                                rng.normal(scale=0.4, size=(n, (degree + 1) ** 2, 3)))


def same_splats(a, b, what, atol=2e-5):
    ok = len(a) == len(b) and a.sh.shape == b.sh.shape
    err = {}
    if ok:
        sign = np.sign(np.sum(a.rotations * b.rotations, axis=1, keepdims=True))
        err = {"positions": np.abs(a.positions - b.positions).max(), "rotations": np.abs(a.rotations - sign * b.rotations).max(),
               "scales": np.abs(a.scales / b.scales - 1).max(), "opacities": np.abs(a.opacities - b.opacities).max(),
               "sh": np.abs(a.sh - b.sh).max()}
        ok = all(v < atol for v in err.values())
    check(ok, "%s (%s)" % (what, ", ".join("%s %.2g" % kv for kv in err.items()) if err else "sizes %s vs %s" % (a.sh.shape, b.sh.shape)))


def test_splats():
    import mathutils
    path = os.path.join(ROOT, "capture.ply")
    a = sample_splats()
    splat_io.write_ply(path, a)
    dot = os.path.join(ROOT, "capture.splat")
    with open(dot, "wb") as f:
        for i in range(5):
            f.write(np.array([i, 0, 1, 0.1, 0.1, 0.1], dtype="<f4").tobytes() + bytes([200, 100, 50, 255, 255, 128, 128, 128]))
    for o in bpy.data.objects:
        o.select_set(False)

    if not bridge.splats_supported():
        print("Blender %s has no Gaussian splats: they must refuse cleanly" % bpy.app.version_string)
        # (An operator that reports an error raises RuntimeError for a script; Blender's UI shows the report.)
        try:
            bpy.ops.sculptforge.import_splat(filepath=dot)
            said = ""
        except RuntimeError as e:
            said = str(e)
        check("5.3" in said, "older Blender: importing a .splat says it needs 5.3 and does nothing (%s)" % said)
        exchange.write_manifest(exchange.inbox(ROOT), [], "test", [{"name": "Capture", "file": "r99_splats.ply"}])
        splat_io.write_ply(os.path.join(exchange.inbox(ROOT), "r99_splats.ply"), a)
        try:
            bpy.ops.sculptforge.receive()
            said = ""
        except RuntimeError as e:
            said = str(e)
        check("5.3" in said and "5.3" in bridge.state.message, "older Blender: receiving splats says it needs 5.3 (%s)" % bridge.state.message)
        bpy.context.view_layer.objects.active = None  # (send also takes the active mesh)
        check(not bpy.ops.sculptforge.send.poll(), "older Blender: nothing selected, nothing to send, no crash")
        return

    # Blender's own import reads our PLY with the values we wrote: opacity and scale decoded, SH in its attributes.
    m = mathutils.Matrix.Translation((1.0, -2.0, 0.5)) @ mathutils.Matrix.Rotation(0.6, 4, (1.0, 2.0, 0.5)) @ mathutils.Matrix.Scale(1.5, 4)
    obj, replaced = bridge.import_splat_ply(bpy.context, path, "Capture", m)
    pc = obj.data
    print("splat attributes:", sorted((x.name, x.data_type, x.domain) for x in pc.attributes if not x.name.startswith(".")))
    check(bridge.is_splat_object(obj) and not replaced and obj.name == "Capture", "a splat PLY imports as a Gaussian splat object")
    check(np.allclose(np.array(obj.matrix_world), np.array(m), atol=1e-6), "the object is placed by the matrix")
    got = bridge.splat_arrays(pc)
    same_splats(a, got, "Blender's import and the add-on's reading of its attributes agree with the PLY")
    for name in ("radiance:base", "rotation", "scale"):
        attr = pc.attributes[name]
        print("splat attribute %s: %s, element properties %s" % (name, attr.data_type, [x.identifier for x in attr.data[0].bl_rna.properties]))
    check(not bridge.slow_reads, "the splat attributes are read in bulk, not element by element (%s)" % bridge.slow_reads)

    # To SculptForge and back through the folder: nothing baked, SH degree 3 intact, the placement kept.
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    check(bpy.ops.sculptforge.send() == {"FINISHED"}, "send a splat object: " + bridge.state.message)
    manifest = exchange.read_manifest(exchange.outbox(ROOT))
    check([e["name"] for e in manifest.get("splats", [])] == ["Capture"] and not manifest["objects"], "manifest lists the splats")
    obj.matrix_world = mathutils.Matrix.Identity(4)  # SculptForge's placement must come back to the object
    sculptforge(["blender_receive", "blender_send"])
    count = len(bpy.data.objects)
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "receive the splats: " + bridge.state.message)
    check("Gaussian splats updated" in bridge.state.message, "the splat object is updated in place (%s)" % bridge.state.message)
    obj = bpy.data.objects["Capture"]
    check(bridge.is_splat_object(obj) and len([o for o in bpy.data.objects if bridge.is_splat_object(o)]) == 1, "still one splat object")
    check(np.allclose(np.array(obj.matrix_world), np.array(m), atol=1e-5), "its placement came back (max error %.2g)" %
          np.abs(np.array(obj.matrix_world) - np.array(m)).max())
    same_splats(a, bridge.splat_arrays(obj.data), "splats through SculptForge: positions, scales, rotations, opacity, SH")
    for extra in [o for o in bpy.data.objects if o.name != "Capture" and o.name not in ("Box", "Disc ü", "Suzanne", "Extra", "Head", "Eyes")]:
        bpy.data.objects.remove(extra)

    # Export (Blender cannot): as stored, and placed by the object.
    out = os.path.join(ROOT, "out.ply")
    obj.select_set(True)
    check(bpy.ops.sculptforge.export_splats(filepath=out, world_space=False) == {"FINISHED"}, "export Gaussian Splats (.ply)")
    same_splats(a, splat_io.read_ply(out), "exported PLY holds the splats as they are")
    check(bpy.ops.sculptforge.export_splats(filepath=out, world_space=True) == {"FINISHED"}, "export applying the object transform")
    placed = splat_io.read_ply(out)
    world = np.array(m)[:3, :3] @ a.positions.astype(np.float64).T + np.array(m)[:3, 3:4]
    check(np.abs(placed.positions - world.T).max() < 1e-4 and placed.sh.shape[1] == 1, "exported world-space positions; turned splats keep plain colour")

    # Dropping a .splat file: its handler's operator.
    check(bpy.ops.sculptforge.import_splat(filepath=dot) == {"FINISHED"}, "import a .splat file")
    dropped = bpy.data.objects.get("capture")
    check(dropped is not None and bridge.is_splat_object(dropped) and len(dropped.data.attributes["position"].data) == 5, ".splat becomes a splat object")
    check(dropped is not None and np.allclose(bridge.splat_arrays(dropped.data).positions[3], (3, 0, 1), atol=1e-6), ".splat positions")


def main():
    check(bridge.prefs() is not None, "add-on enabled with its preferences")
    # No real SculptForge here: never start one.
    exchange.find_sculptforge = lambda root, override="": ""
    names = build_scene()
    before = {n: snapshot(bpy.data.objects[n]) for n in names}

    check(bpy.ops.sculptforge.send() == {"FINISHED"}, "Send to SculptForge")
    manifest = exchange.read_manifest(exchange.outbox(ROOT))
    check([o["name"] for o in manifest["objects"]] == names, "manifest lists the selected objects by name")

    # SculptForge loads them and sends them straight back: everything must be as it was.
    sculptforge(["blender_receive", "blender_send"])
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "Receive from SculptForge: " + bridge.state.message)
    check("3 updated" in bridge.state.message, "all three updated in place")
    check(sorted(o.name for o in bpy.data.objects) == sorted(names), "no objects added")
    for n in names:
        a, b = before[n], snapshot(bpy.data.objects[n])
        check(np.abs(a["co"] - b["co"]).max() < 1e-5, n + ": positions")
        check(np.allclose(a["matrix"], b["matrix"]), n + ": transform kept")
        check(a["polys"] == b["polys"], n + ": polygons (n-gons included) kept")
        check(a["materials"] == b["materials"] and a["modifiers"] == b["modifiers"], n + ": materials and modifiers kept")
        for key in ("uv", "color", "sets"):
            if key in a:
                check(key in b and np.allclose(a[key], b[key], atol=1e-6), n + ": " + key)
        if "mask" in a:
            check("mask" in b and np.allclose(a["mask"], b["mask"], atol=1e-4), n + ": mask")

    # Subdivide the box in SculptForge and add a Part: the box is rebuilt (transform, material and modifier
    # kept), the new Part becomes a new object.
    sculptforge(["blender_receive", "part_select name=Box", "subdivide", "part_append type=cube name=Extra",
                 "blender_send"])
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "Receive the changed scene: " + bridge.state.message)
    box = bpy.data.objects["Box"]
    check(len(box.data.polygons) > len(before["Box"]["polys"]), "Box: new topology")
    check(np.allclose(np.array(box.matrix_world), before["Box"]["matrix"]), "Box: transform kept")
    check([m.name for m in box.data.materials] == ["Clay"] and box.modifiers.get("Bevel") is not None,
          "Box: material and modifier kept")
    check("Extra" in bpy.data.objects, "the new Part is a new object")

    # Sending and receiving work from Sculpt, Edit and Vertex Paint Mode (Edit Mode keeps UVs in its edit mesh):
    # the add-on switches to Object Mode for the transfer and back.
    for mode in ("SCULPT", "EDIT", "VERTEX_PAINT"):
        bpy.ops.object.select_all(action="DESELECT")
        monkey = bpy.data.objects["Suzanne"]
        monkey.select_set(True)
        bpy.context.view_layer.objects.active = monkey
        start = arr(monkey.data.vertices, "co", 3)
        bpy.ops.object.mode_set(mode=mode)
        check(bpy.ops.sculptforge.send() == {"FINISHED"}, mode + ": send")
        sculptforge(["blender_receive", 'stroke points="0,0,-3 0.3,0.3,-3" radius=0.5 strength=1', "blender_send"])
        check(bpy.ops.sculptforge.receive() == {"FINISHED"}, mode + ": receive: " + bridge.state.message)
        check(monkey.mode == mode, mode + ": back in that mode (now %s)" % monkey.mode)
        bpy.ops.object.mode_set(mode="OBJECT")
        moved = np.abs(arr(monkey.data.vertices, "co", 3) - start).max()
        check(moved > 1e-3, mode + ": the sculpt arrived (moved %.3f)" % moved)

    # Apply modifiers: the result is sent, and the modifier is turned off when it comes back.
    bridge.prefs().modifiers = "APPLY"
    bpy.ops.object.select_all(action="DESELECT")
    box.select_set(True)
    bpy.context.view_layer.objects.active = box
    check(bpy.ops.sculptforge.send() == {"FINISHED"}, "send with modifiers applied")
    sculptforge(["blender_receive", "blender_send"])
    bpy.ops.sculptforge.receive()
    check(not box.modifiers["Bevel"].show_viewport, "the applied modifier is turned off")

    # Facial shapes: SculptForge's sculpt layers arrive as shape keys with their names (the 52 ARKit ones).
    sculptforge(["new_head", "face_shapes_generate", "face_weights json={\"jawOpen\":0.25}", "blender_send"])
    check(bpy.ops.sculptforge.receive() == {"FINISHED"}, "receive the head: " + bridge.state.message)
    head = bpy.data.objects.get("Head")
    keys = head.data.shape_keys.key_blocks if head and head.data.shape_keys else []
    check(len(keys) == 53 and keys[0].name == "Basis" and keys[1].name == "browDownLeft" and keys[-1].name == "tongueOut",
          "Head: Basis and the 52 ARKit shape keys, in order")
    if len(keys) == 53:
        jaw = keys["jawOpen"]
        check(abs(jaw.value - 0.25) < 1e-6, "jawOpen keeps its weight")
        basis = arr(keys[0].data, "co", 3)
        check((arr(jaw.data, "co", 3) - basis)[:, 2].min() < -0.01, "jawOpen drops the chin")
    eyes = bpy.data.objects.get("Eyes")
    check(eyes is not None and eyes.data.shape_keys is not None and "eyeLookUpLeft" in eyes.data.shape_keys.key_blocks,
          "Eyes: eyeLook shape keys")

    test_splats()
    test_classic_keys()
    test_vdm_exr()

    addon_utils.disable("sculptforge_bridge")
    check(not bridge.classic_keys.enabled(), "disabling the add-on removes the Classic keys")
    print("%d failure(s)" % len(failures))
    sys.exit(1 if failures else 0)


main()
