# SculptForge bridge for Blender
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Send the selected meshes to SculptForge, sculpt them there, send them back: the objects update in
# place (transform, materials and modifiers kept), the way GoZ / GoB work with ZBrush. Ideas and the
# .GoZ conventions come from GoB (github.com/JoseConseco/GoB, GPL) by Daniel Grauer, JoseConseco and
# contributors. The exchange itself (folder, manifest, .GoZ reading and writing) is in exchange.py.
#
# What travels: positions (object space, with each object's matrix_world in the manifest), triangles
# and quads (n-gons as triangle fans), the active UV map, the active colour attribute (vertex paint),
# the sculpt mask (.sculpt_mask) and face sets (.sculpt_face_set, SculptForge's face groups).
#
# The preference "ZBrush-style keys in Sculpt Mode" adds ZBrush's keys to Blender's Sculpt Mode (zbrush_keys.py).

bl_info = {  # legacy add-on install (Blender 4.2+ reads blender_manifest.toml instead)
    "name": "SculptForge Bridge",
    "author": "SculptForge contributors",
    "version": (0, 7, 0),
    "blender": (4, 2, 0),
    "location": "3D View > Sidebar > SculptForge, Object menu, File > Import / Export",
    "description": "Send meshes to SculptForge and back, updating the objects in place",
    "doc_url": "https://sculptforge.app/guide/blender.html",
    "category": "Import-Export",
}

import contextlib
import os
import time
import traceback

import bpy
import numpy as np
from bpy.props import BoolProperty, EnumProperty, StringProperty

from . import exchange, zbrush_keys

APPLIED_KEY = "sculptforge_applied_modifiers"
MASK = ".sculpt_mask"
FACE_SETS = ".sculpt_face_set"


class _State:
    sent = False         # this session sent something: what SculptForge sends back loads by itself
    launched = 0.0       # when this session last started SculptForge (time.time())
    seen = -1            # revision last loaded (or present when the add-on started)
    message = ""         # last result, shown in the panel


state = _State()


def prefs(context=None):
    context = context or bpy.context
    addon = context.preferences.addons.get(__package__)
    return addon.preferences if addon else None


def exchange_root(p=None):
    p = p or prefs()
    folder = p.exchange_folder if p else ""
    return bpy.path.abspath(folder) if folder else exchange.default_root()


# --------------------------------------------------------------------------------------------------
# Blender mesh -> arrays
# --------------------------------------------------------------------------------------------------


def _get(collection, prop, count, dtype, width=1):
    out = np.empty(count * width, dtype=dtype)
    if count:
        collection.foreach_get(prop, out)
    return out.reshape(-1, width) if width > 1 else out


def mesh_topology(me, mirrored=False):
    """(loops, face_polygon, faces) of the GoZ faces for `me` (exchange.polygon_corners)."""
    starts = _get(me.polygons, "loop_start", len(me.polygons), np.int32)
    totals = _get(me.polygons, "loop_total", len(me.polygons), np.int32)
    loop_vert = _get(me.loops, "vertex_index", len(me.loops), np.int32)
    loops, face_poly = exchange.polygon_corners(starts, totals, mirrored)
    return loops, face_poly, exchange.faces_from_loops(loops, loop_vert), loop_vert


def mesh_arrays(me, mirrored):
    """Everything write_goz takes, from a Blender mesh (object space)."""
    nv = len(me.vertices)
    loops, face_poly, faces, loop_vert = mesh_topology(me, mirrored)
    out = {"positions": _get(me.vertices, "co", nv, np.float32, 3), "faces": faces}
    safe = np.maximum(loops, 0)
    uv_layer = me.uv_layers.active
    if uv_layer is not None and len(me.loops):
        uv = _get(uv_layer.data, "uv", len(me.loops), np.float32, 2)
        out["uvs"] = uv[safe]
    color = me.color_attributes.active_color
    if color is not None and color.domain in {"POINT", "CORNER"}:
        rgba = _get(color.data, "color_srgb", len(color.data), np.float32, 4)
        if color.domain == "POINT":
            out["colors"] = rgba[:, :3]
        else:  # one colour per vertex: the mean of its corners
            sums = np.zeros((nv, 3))
            np.add.at(sums, loop_vert, rgba[:, :3])
            out["colors"] = sums / np.maximum(np.bincount(loop_vert, minlength=nv), 1)[:, None]
    mask = me.attributes.get(MASK)
    if mask is not None and mask.domain == "POINT" and mask.data_type == "FLOAT":
        out["mask"] = _get(mask.data, "value", nv, np.float32)
    sets = me.attributes.get(FACE_SETS)
    if sets is not None and sets.domain == "FACE" and sets.data_type == "INT":
        # Face sets start at 1; SculptForge's face groups at 0.
        out["groups"] = np.maximum(_get(sets.data, "value", len(me.polygons), np.int32)[face_poly] - 1, 0)
    return out


# --------------------------------------------------------------------------------------------------
# Arrays -> Blender mesh
# --------------------------------------------------------------------------------------------------


def set_geometry(me, positions, faces):
    """Replaces the mesh's geometry (materials stay; attributes go). Returns the loop of each face corner."""
    quad = faces[:, 3] != exchange.NO_INDEX
    totals = np.where(quad, 4, 3).astype(np.int32)
    starts = (np.cumsum(totals) - totals).astype(np.int32)
    loop_vert = faces[np.arange(4)[None, :] < totals[:, None]].astype(np.int32)
    me.clear_geometry()
    me.vertices.add(len(positions))
    me.vertices.foreach_set("co", np.ascontiguousarray(positions, dtype=np.float32).ravel())
    me.loops.add(len(loop_vert))
    me.loops.foreach_set("vertex_index", loop_vert)
    me.polygons.add(len(faces))
    me.polygons.foreach_set("loop_start", starts)
    try:
        me.polygons.foreach_set("loop_total", totals)  # read-only (and implied) in newer versions
    except (AttributeError, TypeError, RuntimeError):
        pass
    me.update(calc_edges=True)
    return np.where(np.arange(4)[None, :] < totals[:, None], starts[:, None] + np.arange(4)[None, :], -1)


def write_attributes(me, g, loops, corner, face_poly, names):
    """Received UVs, vertex paint, mask and face groups into `me`. `loops[i, s]` is the loop of corner s of face i
    and `corner[i, s]` the received face's corner at that vertex; `names` the attribute names to (re)use."""
    nl, nv = len(me.loops), len(me.vertices)
    valid = loops >= 0
    if g.uvs is not None and nl:
        layer = me.uv_layers.get(names.get("uv", "")) or me.uv_layers.active or me.uv_layers.new(name=names.get("uv") or "UVMap")
        uv = _get(layer.data, "uv", nl, np.float32, 2)
        picked = np.take_along_axis(g.uvs, corner[:, :, None].repeat(2, axis=2), axis=1)
        uv[loops[valid]] = picked[valid]
        layer.data.foreach_set("uv", uv.ravel())
        me.uv_layers.active = layer
    if g.colors is not None:
        name = names.get("color", "")
        attr = me.color_attributes.get(name) if name else None
        if attr is None or attr.domain not in {"POINT", "CORNER"}:
            attr = me.color_attributes.new(name or "Color", "FLOAT_COLOR", "POINT")
        rgba = np.ones((nv, 4), dtype=np.float32)
        rgba[:, :3] = g.colors
        if attr.domain == "CORNER":
            rgba = rgba[_get(me.loops, "vertex_index", nl, np.int32)]
        attr.data.foreach_set("color_srgb", rgba.ravel())
        me.color_attributes.active_color = attr
        try:
            me.color_attributes.render_color_index = me.color_attributes.active_color_index
        except (AttributeError, TypeError):
            pass
    mask = me.attributes.get(MASK)
    if g.mask is not None:
        if mask is None:
            mask = me.attributes.new(MASK, "FLOAT", "POINT")
        mask.data.foreach_set("value", g.mask.astype(np.float32))
    elif mask is not None:  # SculptForge leaves the mask out when nothing is masked
        me.attributes.remove(mask)
    if g.groups is not None and len(me.polygons):
        sets = me.attributes.get(FACE_SETS)
        if sets is None:
            sets = me.attributes.new(FACE_SETS, "INT", "FACE")
        values = _get(sets.data, "value", len(me.polygons), np.int32)
        values[face_poly] = g.groups + 1
        sets.data.foreach_set("value", values)


def update_object(obj, g):
    """Puts a received mesh (world space) into an existing object, keeping its transform, materials and
    modifiers. Returns "updated" (same topology: positions and attributes only) or "rebuilt"."""
    m = np.array(obj.matrix_world, dtype=np.float64)
    positions = exchange.transform_points(np.linalg.inv(m), g.positions)
    # A mirrored object (negative scale) was sent with every polygon reversed (polygon_corners), so its
    # normals point out once SculptForge placed it: compare with that, and reverse only when rebuilding.
    mirrored = np.linalg.det(m[:3, :3]) < 0
    me = obj.data
    loops, face_poly, sent, _ = mesh_topology(me, mirrored)
    names = {"uv": me.uv_layers.active.name if me.uv_layers.active else "",
             "color": me.color_attributes.active_color.name if me.color_attributes.active_color else ""}
    if len(me.vertices) == len(positions) and exchange.same_topology(sent, g.faces):
        flat = np.ascontiguousarray(positions, dtype=np.float32).ravel()
        me.vertices.foreach_set("co", flat)
        if me.shape_keys is not None:  # the basis is what shows; other keys keep their shapes
            me.shape_keys.reference_key.data.foreach_set("co", flat)
        write_attributes(me, g, loops, exchange.corner_map(sent, g.faces), face_poly, names)
        result = "updated"
    else:
        if mirrored:
            g.faces, g.uvs = exchange.reverse_faces(g.faces, g.uvs)
        if me.shape_keys is not None:
            obj.shape_key_clear()
        loops = set_geometry(me, positions, g.faces)
        corner = np.tile(np.arange(4), (len(g.faces), 1))
        write_attributes(me, g, loops, corner, np.arange(len(g.faces)), names)
        result = "rebuilt"
    me.update()
    return result


def apply_shapes(obj, path):
    """Shape keys from a SculptForge shapes file (exchange.read_shapes) on a received object: a Basis, then one
    key per sculpt layer, named as in SculptForge (the facial shapes keep their ARKit names), at its weight."""
    try:
        nv, shapes = exchange.read_shapes(path)
    except (OSError, ValueError):
        return 0  # the mesh still arrives, without its shape keys
    me = obj.data
    if nv != len(me.vertices) or not shapes:
        return 0
    if me.shape_keys is None:
        obj.shape_key_add(name="Basis", from_mix=False)
    basis = np.empty(nv * 3, dtype=np.float64)
    me.shape_keys.reference_key.data.foreach_get("co", basis)
    basis = basis.reshape(-1, 3)
    to_local = np.linalg.inv(np.array(obj.matrix_world, dtype=np.float64))[:3, :3]
    for name, weight, idx, off in shapes:
        key = me.shape_keys.key_blocks.get(name) or obj.shape_key_add(name=name, from_mix=False)
        co = basis.copy()
        co[idx] += off @ to_local.T  # offsets are world-space vectors
        key.data.foreach_set("co", np.ascontiguousarray(co, dtype=np.float32).ravel())
        key.value = max(key.slider_min, min(key.slider_max, weight))
    me.update()
    return len(shapes)


def new_object(context, name, g):
    me = bpy.data.meshes.new(name)
    loops = set_geometry(me, g.positions, g.faces)
    write_attributes(me, g, loops, np.tile(np.arange(4), (len(g.faces), 1)), np.arange(len(g.faces)), {})
    me.update()
    obj = bpy.data.objects.new(name, me)
    collection = getattr(context, "collection", None) or context.scene.collection
    collection.objects.link(obj)
    return obj


# --------------------------------------------------------------------------------------------------
# Modes (meshes are updated in Object Mode; the previous mode comes back afterwards)
# --------------------------------------------------------------------------------------------------

_MODE_SET = {"EDIT_MESH": "EDIT", "SCULPT": "SCULPT", "PAINT_VERTEX": "VERTEX_PAINT",
             "PAINT_WEIGHT": "WEIGHT_PAINT", "PAINT_TEXTURE": "TEXTURE_PAINT"}


def _override(context):
    window = context.window or next(iter(context.window_manager.windows), None)
    if window is None:
        return {}
    for area in window.screen.areas:
        if area.type == "VIEW_3D":
            region = next((r for r in area.regions if r.type == "WINDOW"), None)
            return {"window": window, "area": area, "region": region}
    return {"window": window}


def _set_mode(context, mode):
    with context.temp_override(**_override(context)):
        bpy.ops.object.mode_set(mode=mode)


@contextlib.contextmanager
def _object_mode(context, objects):
    """Object Mode while the block runs when any of `objects` is in another mode (Edit Mode keeps the mesh's data
    in its edit mesh; Sculpt Mode in its sculpt session), then the previous mode again. Raises RuntimeError when
    Blender won't switch."""
    active = context.view_layer.objects.active
    previous = context.mode
    switched = any(o is not None and o.mode != "OBJECT" for o in objects)
    if switched:
        _set_mode(context, "OBJECT")
    try:
        yield
    finally:
        if switched and previous in _MODE_SET and context.mode != previous and active is not None and \
                active.name in bpy.data.objects:
            try:
                _set_mode(context, _MODE_SET[previous])
            except RuntimeError:
                pass


# --------------------------------------------------------------------------------------------------
# Send and receive
# --------------------------------------------------------------------------------------------------


def _write_objects(context, objects, folder, prefix, apply):
    """The objects' .GoZ files in `folder`; returns their manifest entries."""
    depsgraph = context.evaluated_depsgraph_get() if apply else None
    entries = []
    for k, obj in enumerate(objects):
        mirrored = obj.matrix_world.determinant() < 0
        if apply:
            evaluated = obj.evaluated_get(depsgraph)
            me = evaluated.to_mesh()
            try:
                arrays = mesh_arrays(me, mirrored)
            finally:
                evaluated.to_mesh_clear()
            obj[APPLIED_KEY] = [m.name for m in obj.modifiers if m.show_viewport]
        else:
            arrays = mesh_arrays(obj.data, mirrored)
            if APPLIED_KEY in obj:
                del obj[APPLIED_KEY]
        if len(arrays["faces"]) == 0:
            continue
        file = "%s%03d.GoZ" % (prefix, k)
        exchange.write_goz(os.path.join(folder, file), obj.name, **arrays)
        entries.append({"name": obj.name, "file": file, "matrix_world": [list(row) for row in obj.matrix_world]})
    return entries


def send(context):
    """Writes the selected meshes for SculptForge and starts it if needed. Returns (level, message)."""
    p = prefs(context)
    root = exchange_root(p)
    objects = [o for o in context.selected_objects if o.type == "MESH"]
    if not objects and context.active_object is not None and context.active_object.type == "MESH":
        objects = [context.active_object]
    if not objects:
        return "ERROR", "Select one or more mesh objects to send"
    apply = p is not None and p.modifiers == "APPLY"
    folder = exchange.outbox(root)
    first = not os.path.isdir(folder)
    os.makedirs(folder, exist_ok=True)
    prefix = exchange.next_file_prefix(folder)
    try:
        with _object_mode(context, objects):
            entries = _write_objects(context, objects, folder, prefix, apply)
    except RuntimeError as e:
        return "ERROR", "Leave %s to send to SculptForge (%s)" % (context.mode.replace("_", " ").title(), e)
    if not entries:
        return "ERROR", "The selected meshes have no faces"
    exchange.write_manifest(folder, entries, bpy.app.version_string)
    state.sent = True
    count = "%d object%s" % (len(entries), "" if len(entries) == 1 else "s")
    # A running SculptForge loads it within a second. It beats only once the folder exists, and one this session
    # started may still be starting: wait for its heartbeat rather than start a second one.
    starting = time.time() - state.launched < 60.0
    if exchange.sculptforge_running(root) or ((first or starting) and exchange.wait_for_heartbeat(root, 5.0 if starting else 2.5)):
        return "INFO", "Sent %s to SculptForge" % count
    path = exchange.find_sculptforge(root, p.sculptforge_path if p else "")
    if not path:
        return "WARNING", ("Sent %s, but SculptForge was not found: set its path in the add-on's preferences, or "
                           "start it and use File > Blender > Receive from Blender" % count)
    try:
        exchange.launch_sculptforge(path)
        state.launched = time.time()
    except OSError as e:
        return "WARNING", "Sent %s, but SculptForge could not be started (%s): %s" % (count, path, e)
    return "INFO", "Sent %s; starting SculptForge" % count


def receive(context):
    """Loads what SculptForge sent into the scene. Returns (level, message)."""
    root = exchange_root()
    folder = exchange.inbox(root)
    try:
        manifest = exchange.read_manifest(folder)
    except ValueError as e:
        return "ERROR", str(e)
    if manifest is None:
        return "ERROR", "Nothing has been sent from SculptForge yet (File > Blender > Send to Blender)"
    state.seen = int(manifest.get("revision", 0))
    entries = manifest.get("objects", [])
    meshes = []
    for entry in entries:
        try:
            meshes.append((entry.get("name") or "Object", exchange.read_goz(os.path.join(folder, entry["file"])),
                           os.path.join(folder, entry["shapes"]) if entry.get("shapes") else None))
        except (OSError, ValueError) as e:
            return "ERROR", "Cannot read %s from SculptForge: %s" % (entry.get("name", "?"), e)

    def target(name):
        obj = bpy.data.objects.get(name)
        return obj if obj is not None and obj.type == "MESH" and obj.library is None else None

    counts = {"updated": 0, "rebuilt": 0, "new": 0}
    disabled = 0
    keys = 0
    previous = context.mode
    try:
        with _object_mode(context, [target(n) for n, _, _ in meshes]):  # back to the previous mode afterwards
            for name, g, shapes in meshes:
                obj = target(name)
                if obj is None:
                    obj = new_object(context, name, g)
                    counts["new"] += 1
                    if shapes:
                        keys += apply_shapes(obj, shapes)
                    continue
                counts[update_object(obj, g)] += 1
                if shapes:
                    keys += apply_shapes(obj, shapes)
                applied = obj.get(APPLIED_KEY)
                if applied is not None:  # those modifiers are in the mesh now
                    for m in obj.modifiers:
                        if m.name in applied and m.show_viewport:
                            m.show_viewport = m.show_render = False
                            disabled += 1
                    del obj[APPLIED_KEY]
    except RuntimeError as e:
        return "ERROR", "Leave %s to receive from SculptForge (%s)" % (previous.replace("_", " ").title(), e)
    parts = []
    if counts["updated"]:
        parts.append("%d updated" % counts["updated"])
    if counts["rebuilt"]:
        parts.append("%d with new topology" % counts["rebuilt"])
    if counts["new"]:
        parts.append("%d new" % counts["new"])
    message = "Received %d object%s from SculptForge (%s)" % (len(meshes), "" if len(meshes) == 1 else "s", ", ".join(parts))
    if disabled:
        message += "; turned off %d modifier%s applied when sending" % (disabled, "" if disabled == 1 else "s")
    if keys:
        message += "; %d shape key%s" % (keys, "" if keys == 1 else "s")
    return "INFO", message


def _redraw(context):
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == "VIEW_3D":
                area.tag_redraw()


def _poll_inbox():
    """Timer: load what SculptForge sends back, once this session has sent something."""
    try:
        p = prefs()
        if p is None or not p.auto_receive or not state.sent:
            return 1.0
        folder = exchange.inbox(exchange_root(p))
        # The revision, not the file time (copies and coarse file systems can keep the time); the file is tiny.
        rev = exchange.revision(folder)
        if rev < 0 or rev == state.seen:
            return 1.0
        level, state.message = receive(bpy.context)
        print("SculptForge:", state.message)
        if level == "INFO":
            try:
                bpy.ops.ed.undo_push(message="Receive from SculptForge")
            except RuntimeError:
                pass
        _redraw(bpy.context)
    except Exception:  # never let the timer die
        traceback.print_exc()
    return 1.0


# --------------------------------------------------------------------------------------------------
# UI
# --------------------------------------------------------------------------------------------------


class SCULPTFORGE_OT_send(bpy.types.Operator):
    bl_idname = "sculptforge.send"
    bl_label = "Send to SculptForge"
    bl_description = ("Send the selected meshes to SculptForge (starting it if needed); each becomes a Part. "
                      "Send them back from SculptForge with File > Blender > Send to Blender")
    bl_options = {"REGISTER"}

    @classmethod
    def poll(cls, context):
        return any(o.type == "MESH" for o in context.selected_objects) or (
            context.active_object is not None and context.active_object.type == "MESH")

    def execute(self, context):
        level, state.message = send(context)
        self.report({level}, state.message)
        return {"CANCELLED"} if level == "ERROR" else {"FINISHED"}


class SCULPTFORGE_OT_receive(bpy.types.Operator):
    bl_idname = "sculptforge.receive"
    bl_label = "Receive from SculptForge"
    bl_description = ("Load what SculptForge last sent: objects that came from here update in place (transform, "
                      "materials and modifiers kept), new Parts become new objects")
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        level, state.message = receive(context)
        self.report({level}, state.message)
        return {"CANCELLED"} if level == "ERROR" else {"FINISHED"}


class SCULPTFORGE_PT_panel(bpy.types.Panel):
    bl_label = "SculptForge"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "SculptForge"

    def draw(self, context):
        layout = self.layout
        p = prefs(context)
        col = layout.column(align=True)
        col.scale_y = 1.3
        col.operator(SCULPTFORGE_OT_send.bl_idname, icon="EXPORT")
        col.operator(SCULPTFORGE_OT_receive.bl_idname, icon="IMPORT")
        if p is not None:
            layout.prop(p, "modifiers", text="Modifiers")
            layout.prop(p, "auto_receive")
        running = exchange.sculptforge_running(exchange_root(p))
        layout.label(text="SculptForge is running" if running else "SculptForge is not running",
                     icon="CHECKMARK" if running else "INFO")
        if state.message:
            box = layout.box()
            for line in _wrap(state.message, 36):
                box.label(text=line)


def _wrap(text, width):
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            lines.append(line)
            line = word
        else:
            line = (line + " " + word).strip()
    return lines + ([line] if line else [])


class SculptForgePreferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    sculptforge_path: StringProperty(
        name="SculptForge",
        description="The SculptForge program (sculptforge, sculptforge.exe, the .AppImage or SculptForge.app). "
                    "Empty: found automatically (SculptForge records where it is each time it starts)",
        subtype="FILE_PATH")
    exchange_folder: StringProperty(
        name="Exchange folder",
        description="Folder both programs use. Empty: SculptForge's per-user folder (set the same folder with "
                    "SCULPTFORGE_BLENDER_EXCHANGE for SculptForge if you change it)",
        subtype="DIR_PATH")
    modifiers: EnumProperty(
        name="Modifiers",
        description="What to send of objects with modifiers",
        items=[("PRESERVE", "Keep live", "Send the mesh under the modifiers; they keep working on the sculpt in Blender"),
               ("APPLY", "Apply", "Send the result of the modifiers; when it comes back they are turned off (the mesh has them)")],
        default="PRESERVE")
    auto_receive: BoolProperty(
        name="Receive automatically",
        description="After a send, load what SculptForge sends back as soon as it arrives (checked once a second)",
        default=True)
    zbrush_keys: BoolProperty(
        name="ZBrush-style keys in Sculpt Mode",
        description="S / U / O for Draw Size / Z Intensity / Focal Shift, B for the brush list, Space for the quick "
                    "menu, Alt to dig in, Ctrl to mask, Ctrl+Shift to hide, D / Shift+D for SDiv... Turning it off "
                    "removes them again and Blender's own keys are back",
        default=False,
        update=lambda self, context: _apply_zbrush_keys(self))

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "sculptforge_path")
        found = exchange.find_sculptforge(exchange_root(self), self.sculptforge_path)
        layout.label(text="Found: " + found if found else "SculptForge not found: set its path above", icon="FILE_TICK" if found else "ERROR")
        layout.prop(self, "exchange_folder")
        layout.label(text="Exchange folder: " + exchange_root(self))
        layout.prop(self, "modifiers")
        layout.prop(self, "auto_receive")
        layout.prop(self, "zbrush_keys")
        if self.zbrush_keys:
            box = layout.box()
            col = box.column(align=True)
            for zkey, what, *_ in zbrush_keys.key_table():
                if zkey:
                    col.label(text="%s: %s" % (zkey, what))
            col.separator()
            col.label(text="Not in Blender:", icon="INFO")
            for zkey, why in zbrush_keys.MISSING:
                col.label(text="%s: %s" % (zkey, why))


def _object_menu(self, context):
    self.layout.separator()
    self.layout.operator(SCULPTFORGE_OT_send.bl_idname, icon="EXPORT")
    self.layout.operator(SCULPTFORGE_OT_receive.bl_idname, icon="IMPORT")


def _export_menu(self, context):
    self.layout.operator(SCULPTFORGE_OT_send.bl_idname, text="SculptForge (send)")


def _import_menu(self, context):
    self.layout.operator(SCULPTFORGE_OT_receive.bl_idname, text="SculptForge (receive)")


CLASSES = (SculptForgePreferences, SCULPTFORGE_OT_send, SCULPTFORGE_OT_receive, SCULPTFORGE_PT_panel)


def _apply_zbrush_keys(p=None):
    p = p or prefs()
    if p is not None and p.zbrush_keys:
        zbrush_keys.enable()
    else:
        zbrush_keys.disable()


def _apply_zbrush_keys_later():
    _apply_zbrush_keys()  # (the preferences are loaded by the time Blender runs timers)
    return None


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    zbrush_keys.register()
    _apply_zbrush_keys()
    bpy.app.timers.register(_apply_zbrush_keys_later, first_interval=0.1)
    bpy.types.VIEW3D_MT_object.append(_object_menu)
    bpy.types.TOPBAR_MT_file_export.append(_export_menu)
    bpy.types.TOPBAR_MT_file_import.append(_import_menu)
    # What SculptForge sent before Blender started is not loaded by itself (Receive loads it).
    folder = exchange.inbox(exchange_root())
    state.seen = exchange.revision(folder)
    if not bpy.app.timers.is_registered(_poll_inbox):
        bpy.app.timers.register(_poll_inbox, first_interval=1.0, persistent=True)


def unregister():
    if bpy.app.timers.is_registered(_poll_inbox):
        bpy.app.timers.unregister(_poll_inbox)
    bpy.types.TOPBAR_MT_file_import.remove(_import_menu)
    bpy.types.TOPBAR_MT_file_export.remove(_export_menu)
    bpy.types.VIEW3D_MT_object.remove(_object_menu)
    zbrush_keys.unregister()  # (removes the ZBrush keys)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
