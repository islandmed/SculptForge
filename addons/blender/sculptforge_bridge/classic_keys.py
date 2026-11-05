# SculptForge bridge for Blender: Classic sculpting keys in Sculpt Mode
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The add-on preference "Classic sculpting keys in Sculpt Mode" adds these to Blender's add-on keymap (they take
# priority over Blender's own Sculpt keys while it is on, and are removed again when it is turned off or the
# add-on is disabled). Only keys with a real Blender equivalent are mapped; MISSING lists the rest, for the
# preferences and docs/guide/blender.md.
#
# Blender 4.2 picks sculpt brushes as tools (builtin_brush.*); 4.3 and newer as brush assets from the
# Essentials library. The radial controls read the size and strength from the unified paint settings, which
# moved from the tool settings into each paint mode in Blender 5.

import bpy
from bpy.props import IntProperty, StringProperty

# (SculptForge name, the classic B-letters, Blender 4.3+ Essentials brush, Blender 4.2 sculpt_tool, Blender 4.2 tool id)
BRUSHES = [
    ("Standard", "ST", "Draw", "DRAW", "builtin_brush.Draw"),
    ("Sharp Crease", "DS", "Draw Sharp", "DRAW_SHARP", "builtin_brush.Draw Sharp"),
    ("Clay", "CL", "Clay", "CLAY", "builtin_brush.Clay"),
    ("Clay Block", "CB", "Clay Strips", "CLAY_STRIPS", "builtin_brush.Clay Strips"),
    ("Inflate", "IN", "Inflate/Deflate", "INFLATE", "builtin_brush.Inflate"),
    ("Layer", "LY", "Layer", "LAYER", "builtin_brush.Layer"),
    ("Crease", "CR", "Crease Sharp", "CREASE", "builtin_brush.Crease"),
    ("Smooth", "SM", "Smooth", "SMOOTH", "builtin_brush.Smooth"),
    ("Flatten", "FL", "Flatten/Contrast", "FLATTEN", "builtin_brush.Flatten"),
    ("Plane Trim", "TD", "Trim", "SCRAPE", "builtin_brush.Scrape"),
    ("Pinch", "PI", "Pinch/Magnify", "PINCH", "builtin_brush.Pinch"),
    ("Move", "MV", "Grab", "GRAB", "builtin_brush.Grab"),
    ("Move Elastic", "", "Elastic Grab", "ELASTIC_DEFORM", "builtin_brush.Elastic Deform"),
    ("Tendril", "SH", "Snake Hook", "SNAKE_HOOK", "builtin_brush.Snake Hook"),
    ("Nudge", "NU", "Nudge", "NUDGE", "builtin_brush.Nudge"),
    ("MaskPen", "", "Mask", "MASK", "builtin_brush.Mask"),
]

ESSENTIALS = "brushes/essentials_brushes-mesh_sculpt.blend/Brush/"


def brush_assets():
    """Blender 4.3+ picks brushes as assets (bpy.ops.brush.asset_activate); 4.2 as tools."""
    return bpy.app.version >= (4, 3, 0)

# Classic keys Blender has nothing for (or that can't be bound the same way), shown in the preferences.
MISSING = [
    ("I", "RGB Intensity: Blender's sculpt brushes don't paint"),
    ("Ctrl+drag on the model", "painting the mask with any brush held: Blender can't swap to the Mask brush "
                               "for one stroke, so Ctrl+drag draws a mask lasso instead (B M P picks the Mask brush)"),
    ("Drag on the background", "rotating: a keymap can't tell the model from the background (use the middle "
                               "button, or Blender's Emulate 3 Button Mouse)"),
    ("Ctrl+click the background", "invert or clear the mask: use Ctrl+I (invert) and Alt+M (clear)"),
    ("Ctrl+Shift+click the background", "show everything: use Alt+H"),
    ("B, then two letters", "Blender menus take one key at a time: B opens the brush list; type the letters "
                            "(or any part of the name) to filter it, Enter picks"),
    ("Alt+click a Part", "Blender's objects aren't Parts: Alt+Q switches the object being sculpted"),
    ("Strokes (DragRect, Spray...)", "Blender's stroke methods live in each brush's Stroke panel"),
]

_items = []  # (keymap, keymap item) added by enable()


def _unified_path():
    """Where the unified size / strength live: each paint mode in Blender 5, the tool settings before."""
    if "unified_paint_settings" in bpy.types.Paint.bl_rna.properties:
        return "tool_settings.sculpt.unified_paint_settings"
    return "tool_settings.unified_paint_settings"


def radial_properties(prop, unified_toggle=""):
    """wm.radial_control settings for the sculpt brush's `prop` (what Blender's own F / Shift+F use)."""
    brush = "tool_settings.sculpt.brush"
    props = {"data_path_primary": brush + "." + prop, "rotation_path": brush + ".texture_slot.angle",
             "color_path": brush + ".cursor_color_add", "image_id": brush}
    if unified_toggle:
        unified = _unified_path()
        props["data_path_secondary"] = unified + "." + prop
        props["use_secondary"] = unified + "." + unified_toggle
    return props


def _brush_entry(name):
    return next((b for b in BRUSHES if b[0] == name), None)


class SCULPTFORGE_OT_classic_brush(bpy.types.Operator):
    bl_idname = "sculptforge.classic_brush"
    bl_label = "Classic Brush"
    bl_description = "Sculpt with the Blender brush closest to this SculptForge brush"
    bl_options = {"REGISTER"}

    brush: StringProperty(name="Brush", description="SculptForge brush name")

    def execute(self, context):
        entry = _brush_entry(self.brush)
        if entry is None:
            self.report({"ERROR"}, "No brush called %s" % self.brush)
            return {"CANCELLED"}
        name, _, asset, sculpt_tool, tool_id = entry
        if brush_assets():
            result = bpy.ops.brush.asset_activate(asset_library_type="ESSENTIALS",
                                                  relative_asset_identifier=ESSENTIALS + asset)
            if "FINISHED" not in result:
                self.report({"ERROR"}, "Blender's %s brush is missing" % asset)
                return {"CANCELLED"}
        else:  # Blender 4.2: brushes are tools of the 3D View
            done = False
            if context.space_data is not None and context.space_data.type == "VIEW_3D":
                done = "FINISHED" in bpy.ops.wm.tool_set_by_id(name=tool_id)
            if not done:  # (no 3D View, e.g. from a script) the brush itself
                brush = next((b for b in bpy.data.brushes if b.use_paint_sculpt and b.sculpt_tool == sculpt_tool), None)
                if brush is None or context.tool_settings.sculpt is None:
                    self.report({"ERROR"}, "Blender has no %s brush here" % asset)
                    return {"CANCELLED"}
                context.tool_settings.sculpt.brush = brush
        self.report({"INFO"}, "%s: Blender's %s" % (name, asset))
        return {"FINISHED"}


class SCULPTFORGE_OT_classic_sdiv(bpy.types.Operator):
    bl_idname = "sculptforge.classic_sdiv"
    bl_label = "Classic SDiv"
    bl_description = "Sculpt one Multires level higher or lower (D / Shift+D); never adds a level"
    bl_options = {"REGISTER", "UNDO"}

    step: IntProperty(name="Step", default=1, min=-1, max=1)

    @classmethod
    def poll(cls, context):
        return context.active_object is not None and any(m.type == "MULTIRES" for m in context.active_object.modifiers)

    def execute(self, context):
        multires = next(m for m in context.active_object.modifiers if m.type == "MULTIRES")
        level = max(0, min(multires.total_levels, multires.sculpt_levels + self.step))
        multires.sculpt_levels = level
        multires.levels = level  # the viewport shows the level being sculpted
        self.report({"INFO"}, "SDiv %d of %d" % (level, multires.total_levels))
        return {"FINISHED"}


class SCULPTFORGE_OT_classic_divide(bpy.types.Operator):
    bl_idname = "sculptforge.classic_divide"
    bl_label = "Classic Divide"
    bl_description = "Add a subdivision level to the Multires modifier (adding one if needed) and sculpt on it (Ctrl+D)"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.active_object is not None and context.active_object.type == "MESH"

    def execute(self, context):
        obj = context.active_object
        multires = next((m for m in obj.modifiers if m.type == "MULTIRES"), None)
        if multires is None:
            multires = obj.modifiers.new("Multires", "MULTIRES")
        result = bpy.ops.object.multires_subdivide(modifier=multires.name, mode="CATMULL_CLARK")
        if "FINISHED" not in result:
            return {"CANCELLED"}
        multires.sculpt_levels = multires.levels = multires.total_levels
        self.report({"INFO"}, "SDiv %d of %d" % (multires.sculpt_levels, multires.total_levels))
        return {"FINISHED"}


class SCULPTFORGE_OT_classic_group(bpy.types.Operator):
    bl_idname = "sculptforge.classic_group"
    bl_label = "Classic Group"
    bl_description = "Make the masked part a face set and clear the mask; with nothing masked, the visible part (Ctrl+W)"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        return context.mode == "SCULPT" and context.active_object is not None

    def execute(self, context):
        mask = context.active_object.data.attributes.get(".sculpt_mask")
        values = [0.0] * (len(mask.data) if mask else 0)
        if mask:
            mask.data.foreach_get("value", values)
        if any(v > 0.5 for v in values):
            bpy.ops.sculpt.face_sets_create(mode="MASKED")
            bpy.ops.paint.mask_flood_fill(mode="VALUE", value=0.0)
            self.report({"INFO"}, "Grouped the masked part")
        else:
            bpy.ops.sculpt.face_sets_create(mode="VISIBLE")
            self.report({"INFO"}, "Grouped the visible part")
        return {"FINISHED"}


class SCULPTFORGE_MT_classic_brushes(bpy.types.Menu):
    bl_idname = "SCULPTFORGE_MT_classic_brushes"
    bl_label = "Classic Brushes"
    bl_options = {"SEARCH_ON_KEY_PRESS"}  # typing filters, as the classic B pop-up does

    def draw(self, context):
        layout = self.layout
        for name, keys, asset, _, _ in BRUSHES:
            label = "%s  (B %s)" % (name, " ".join(keys)) if keys else name
            layout.operator(SCULPTFORGE_OT_classic_brush.bl_idname, text=label).brush = name


def key_table():
    """(Classic key, what it does, Blender operator, key, ctrl, shift, alt, value, properties)."""
    toggle = "wm.context_toggle"
    lasso = "paint.mask_lasso_gesture"
    return [
        ("S", "Draw Size", "wm.radial_control", "S", False, False, False, "PRESS", radial_properties("size", "use_unified_size")),
        ("U", "Z Intensity", "wm.radial_control", "U", False, False, False, "PRESS",
         radial_properties("strength", "use_unified_strength")),
        ("O", "Focal Shift (Blender: Hardness)", "wm.radial_control", "O", False, False, False, "PRESS",
         radial_properties("hardness")),
        ("Space", "the quick menu at the cursor (Blender's Sculpt context menu)", "wm.call_panel", "SPACE", False,
         False, False, "PRESS", {"name": "VIEW3D_PT_sculpt_context_menu"}),
        ("B", "the brush pop-up: type to narrow, Enter picks", "wm.call_menu", "B", False, False, False, "PRESS",
         {"name": SCULPTFORGE_MT_classic_brushes.bl_idname}),
        ("Alt+drag", "Sub (the brush digs in)", "sculpt.brush_stroke", "LEFTMOUSE", False, False, True, "PRESS",
         {"mode": "INVERT"}),
        ("Ctrl+drag", "mask (a lasso)", lasso, "LEFTMOUSE", True, False, False, "PRESS", {"mode": "VALUE", "value": 1.0}),
        ("Ctrl+Alt+drag", "unmask (a lasso)", lasso, "LEFTMOUSE", True, False, True, "PRESS", {"mode": "VALUE", "value": 0.0}),
        ("Ctrl+Shift+drag", "show only what's inside", "paint.hide_show", "LEFTMOUSE", True, True, False, "PRESS",
         {"action": "HIDE", "area": "OUTSIDE"}),
        ("Ctrl+Shift+Alt+drag", "hide what's inside", "paint.hide_show", "LEFTMOUSE", True, True, True, "PRESS",
         {"action": "HIDE", "area": "INSIDE"}),  # (Blender spells this enum item "Inside": see _set)
        ("X", "Symmetry X", toggle, "X", False, False, False, "PRESS", {"data_path": "object.data.use_mirror_x"}),
        ("Shift+F", "Wireframe overlay", toggle, "F", False, True, False, "PRESS",
         {"data_path": "space_data.overlay.show_wireframes"}),
        ("F", "Frame the model", "view3d.view_all", "F", False, False, False, "PRESS", {}),
        ("P", "Perspective on / off", "view3d.view_persportho", "P", False, False, False, "PRESS", {}),
        ("L", "LazyMouse (Blender: Stabilize Stroke)", toggle, "L", False, False, False, "PRESS",
         {"data_path": "tool_settings.sculpt.brush.use_smooth_stroke"}),
        ("Ctrl+D", "Divide (adds a Multires level)", SCULPTFORGE_OT_classic_divide.bl_idname, "D", True, False, False,
         "PRESS", {}),
        ("D", "SDiv up", SCULPTFORGE_OT_classic_sdiv.bl_idname, "D", False, False, False, "PRESS", {"step": 1}),
        ("Shift+D", "SDiv down", SCULPTFORGE_OT_classic_sdiv.bl_idname, "D", False, True, False, "PRESS", {"step": -1}),
        ("Ctrl+W", "Group Masked and clear the mask, or Group Visible (face sets)", SCULPTFORGE_OT_classic_group.bl_idname,
         "W", True, False, False, "PRESS", {}),
        ("W / E / R", "Move / Scale / Rotate", "wm.tool_set_by_id", "W", False, False, False, "PRESS",
         {"name": "builtin.move"}),
        ("", "", "wm.tool_set_by_id", "E", False, False, False, "PRESS", {"name": "builtin.scale"}),
        ("", "", "wm.tool_set_by_id", "R", False, False, False, "PRESS", {"name": "builtin.rotate"}),
    ]


def _set(properties, name, value):
    """Sets an operator setting; enum items match regardless of case (paint.hide_show's area items are 'OUTSIDE'
    and 'Inside')."""
    prop = properties.bl_rna.properties.get(name)
    if prop is not None and prop.type == "ENUM" and isinstance(value, str):
        value = next((e.identifier for e in prop.enum_items if e.identifier.upper() == value.upper()), value)
    setattr(properties, name, value)


def enabled():
    return bool(_items)


skipped = []  # keys that could not be bound in this Blender (operator or setting missing), with why


def enable():
    """Adds the keys (once). Returns how many were added; 0 without an add-on keyconfig (Blender's -b mode has
    one; some embedded builds don't)."""
    if _items:
        return len(_items)
    kc = bpy.context.window_manager.keyconfigs.addon
    if kc is None:
        return 0
    km = kc.keymaps.new(name="Sculpt", space_type="EMPTY")
    skipped.clear()
    for zkey, _, idname, key, ctrl, shift, alt, value, props in key_table():
        kmi = km.keymap_items.new(idname, key, value, ctrl=ctrl, shift=shift, alt=alt)
        try:
            for name, v in props.items():
                _set(kmi.properties, name, v)
        except (TypeError, AttributeError, ValueError) as e:  # one key never stops the others
            km.keymap_items.remove(kmi)
            skipped.append((zkey or key, str(e)))
            continue
        _items.append((km, kmi))
    return len(_items)


def disable():
    while _items:
        km, kmi = _items.pop()
        try:
            km.keymap_items.remove(kmi)
        except (ReferenceError, RuntimeError):  # the keyconfig went first (Blender quitting)
            pass


CLASSES = (SCULPTFORGE_OT_classic_brush, SCULPTFORGE_OT_classic_sdiv, SCULPTFORGE_OT_classic_divide,
           SCULPTFORGE_OT_classic_group, SCULPTFORGE_MT_classic_brushes)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)


def unregister():
    disable()
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
