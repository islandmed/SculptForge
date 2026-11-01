# Coming from ZBrush

This chapter is for sculptors whose hands already know ZBrush. It lists what works the same, what is different,
and the shortcuts side by side, so you can start sculpting instead of relearning. SculptForge follows ZBrush's
keys and gestures by default; where it has habits of its own, they are kept as a preference.

## What's the same

- **The layout.** Shelves around the canvas, palettes in alphabetical order in the palette bar and the trays,
  sub-palettes that fold, the LightBox on <kbd>,</kbd>, Ctrl-hover help on every control, Ctrl+Alt+click to
  assign a hotkey, **Store Config** on <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>. See
  [The interface](interface.md).
- **Navigation.** Drag on the background to rotate freely (over the top, too), <kbd>Alt</kbd>+drag to move,
  <kbd>Alt</kbd>+click, let go of <kbd>Alt</kbd> and drag to scale, <kbd>Shift</kbd> during a rotation to snap to
  a side view, <kbd>Shift</kbd>+click, let go of <kbd>Shift</kbd> and drag to roll. Right-drag navigation works as
  with ZBrush's RightClick Navigation turned on, over the model too. <kbd>F</kbd> frames the model. See
  [Navigating the view](navigation.md).
- **Sculpting modifiers.** <kbd>Alt</kbd> swaps Zadd and Zsub, <kbd>Shift</kbd> smooths (Smooth keeps its own Z
  Intensity; let go of <kbd>Shift</kbd> mid-stroke for the alternate smooth that keeps the volume),
  <kbd>Shift</kbd> pressed after a stroke starts draws a straight line.
- **Brush keys.** <kbd>B</kbd> then two letters, with ZBrush's letters: <kbd>B</kbd> <kbd>S</kbd> <kbd>T</kbd>
  Standard, <kbd>B</kbd> <kbd>C</kbd> <kbd>B</kbd> ClayBuildup, <kbd>B</kbd> <kbd>D</kbd> <kbd>S</kbd>
  DamStandard, <kbd>B</kbd> <kbd>M</kbd> <kbd>V</kbd> Move, <kbd>B</kbd> <kbd>I</kbd> <kbd>N</kbd> Inflate,
  <kbd>B</kbd> <kbd>S</kbd> <kbd>M</kbd> Smooth, <kbd>B</kbd> <kbd>S</kbd> <kbd>H</kbd> SnakeHook,
  <kbd>B</kbd> <kbd>P</kbd> <kbd>I</kbd> Pinch.
- **Brush settings.** <kbd>S</kbd> Draw Size, <kbd>U</kbd> Z Intensity, <kbd>I</kbd> RGB Intensity,
  <kbd>O</kbd> Focal Shift, each as a slider under the cursor; <kbd>[</kbd> and <kbd>]</kbd> step the size;
  <kbd>Space</kbd> opens the QuickMenu; <kbd>L</kbd> toggles LazyMouse. The cursor's outer ring is the Draw
  Size, the inner ring the Focal Shift. See [Sculpting and brushes](sculpting.md).
- **Masking.** <kbd>Ctrl</kbd> paints a mask and <kbd>Ctrl</kbd>+<kbd>Alt</kbd> erases it; <kbd>Ctrl</kbd>+click
  the model blurs it and <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click sharpens it; <kbd>Ctrl</kbd>+click the background
  inverts it and <kbd>Ctrl</kbd>+drag there draws a mask rectangle (one that misses the model clears the mask).
  MaskLasso and MaskRect follow the symmetry: with <kbd>Ctrl</kbd> held the shelf shows the mask brush, and the Brush (<kbd>B</kbd>) and Stroke pickers pick it. See [Masking](masking.md).
- **Visibility and polygroups.** <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag keeps the inside (press <kbd>Alt</kbd> to
  hide it instead), <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click a group to isolate it (on a partly hidden mesh: to hide
  it), <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click the background to show everything, <kbd>Ctrl</kbd>+<kbd>W</kbd> to
  group the masked part. SelectRect and SelectLasso, also from the Brush and Stroke pickers with
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd> held. **Group by UVs** is UV Groups. See [Polygroups and visibility](polygroups.md).
- **SubTools** with <kbd>Alt</kbd>+click to select one and <kbd>Shift</kbd>+<kbd>Alt</kbd>+click to select and
  frame it; **subdivision levels** with <kbd>Ctrl</kbd>+<kbd>D</kbd>, <kbd>D</kbd> and <kbd>Shift</kbd>+<kbd>D</kbd>;
  **PolyFrame** on <kbd>Shift</kbd>+<kbd>F</kbd>; **QuickSave** on <kbd>9</kbd>. See [SubTools](subtools.md) and
  [Subdivision levels](subdivision-levels.md).
- **GoZ.** Send models to and from ZBrush. See [Exporting, Unreal and ZBrush](exporting.md#exchange-models-with-zbrush-goz).
- **VDM brushes** with DragRect and DragDot, and your own made from a VDM plane. See
  [Vector displacement brushes](sculpting.md#vector-displacement-brushes).
- **Sculpting in Blender too?** The Blender add-on can give Blender's Sculpt Mode ZBrush's keys. See
  [Working with Blender](blender.md#zbrush-style-keys-in-sculpt-mode).

## Bring your ZTools over

ZBrush's `.ZTL` files are proprietary, so SculptForge can't read them directly. The free **SculptForge ZTool
Export** plug-in runs inside ZBrush and converts them once:

1. Download [SculptForgeZToolExport.txt](https://github.com/islandmed/SculptForge/blob/main/integrations/zbrush/SculptForgeZToolExport.txt) (also attached to every
   release) and copy it into ZBrush's `ZStartup/ZPlugs64` folder, then restart ZBrush. Or load it once with
   **ZScript > Load**.
2. In ZBrush, **ZPlugin > SculptForge > Convert ZTL** asks for a `.ZTL`, loads it and writes each SubTool next to
   it as a GoZ file (`Dragon.ZTL` gives `Dragon.sf001_Body.GoZ`, `Dragon.sf002_Horns.GoZ`...).
   **Save for SculptForge** does the same for the ZTool you have open, saving the `.ZTL` where you choose.
3. In SculptForge, open the `.ZTL` (**File > Open**, or drop it on the window). Its SubTools open in the same
   order with the same names, polygroups, polypaint, mask and UVs.

Each SubTool comes over at its highest subdivision level; the lower levels, sculpt layers and ZBrush materials
don't. If you change the ZTool in ZBrush later, convert it again: SculptForge warns when the `.ZTL` is newer than
its conversion. `.ZPR` projects can't be converted; save their ZTools first.

> **Note:** the plug-in is new and hasn't been tested in every ZBrush version yet. If it stops with an error or a
> SubTool is missing, please [report it](https://github.com/islandmed/SculptForge/issues) with your ZBrush
> version; exporting each SubTool with **Tool > Export** as `.GoZ` or `.OBJ` works in the meantime.

## What's different

| ZBrush | SculptForge | Why |
|---|---|---|
| Transpose action line (<kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd>) | The Gizmo 3D on the same keys; mask what should stay, and <kbd>Alt</kbd>+click to put the pivot on the model | Simpler to learn; ZBrush has a Gizmo 3D too. See [Gizmo 3D](gizmo.md) |
| Edit mode (<kbd>T</kbd>) and the 2.5D canvas | Always in Edit mode | SculptForge is a 3D sculpting app only |
| Perspective toggle (<kbd>P</kbd>) | Always perspective; <kbd>P</kbd> is the Paint brush (colour only, ZBrush's Rgb on, Zadd off) | |
| Stroke replay (<kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd>) | Not yet | |
| <kbd>N</kbd> lists the SubTools | The SubTool sub-palette in the right tray | |
| DynaMesh, ZRemesher | **Voxel Remesh** and **QRemesh** in **Tool > Geometry** | See [Geometry](geometry.md) and [Retopology](retopology.md) |
| Sculptris Pro | **Dynamic** on the top shelf, with its **Detail** slider | See [Dynamic topology](dynamic-topology.md) |
| Open edges hold their shape when smoothed | Smooth pulls an open border in a little with each pass | Mask the border first when it matters |
| Values 0-100 everywhere | The same for Z Intensity, RGB Intensity and Focal Shift; Draw Size is the radius in pixels | |

The [Glossary](glossary.md) maps every ZBrush term to its SculptForge name.

## The SculptForge input style

Earlier versions of SculptForge had a few keys of their own, and some people like them. **Preferences > Input >
Style** switches between:

| | **ZBrush** (default) | **SculptForge** |
|---|---|---|
| <kbd>1</kbd> to <kbd>9</kbd>, <kbd>0</kbd> | Free (<kbd>9</kbd> is QuickSave) | Pick the ten first brushes (Standard, Clay, Smooth, Move ...) |
| <kbd>I</kbd> | RGB Intensity at the cursor | Z Intensity at the cursor (as <kbd>U</kbd>) |
| <kbd>Ctrl</kbd>+drag on the background with nothing masked | Draws a mask rectangle | Zooms |
| Zoom drags (<kbd>Ctrl</kbd>+right-drag, <kbd>Alt</kbd> then release) | Up zooms out | Up zooms in |

**Preferences > Input > Rotation** chooses free rotation (ZBrush) or a turntable that keeps the model upright, as
in Blender or Maya. Press **Store Config** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) to keep either.

## Shortcuts side by side

| Action | ZBrush | SculptForge |
|---|---|---|
| Rotate / move / scale the view | Drag / <kbd>Alt</kbd>+drag / <kbd>Alt</kbd>, release, drag on the background | Same |
| Snap to a side view / roll | <kbd>Shift</kbd> while rotating / <kbd>Shift</kbd>+click, release, drag | Same |
| Frame | <kbd>F</kbd> | <kbd>F</kbd> |
| Brush pop-up | <kbd>B</kbd> | <kbd>B</kbd> |
| Draw Size / Z Intensity / RGB Intensity / Focal Shift | <kbd>S</kbd> / <kbd>U</kbd> / <kbd>I</kbd> / <kbd>O</kbd> | Same |
| QuickMenu | <kbd>Space</kbd> or right-click | <kbd>Space</kbd> or right-click (a right drag navigates) |
| LazyMouse | <kbd>L</kbd> | <kbd>L</kbd> |
| Pick colour / switch colour | <kbd>C</kbd> / <kbd>V</kbd> | Same |
| Symmetry | <kbd>X</kbd> | <kbd>X</kbd> |
| Undo / redo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Same (also <kbd>Ctrl</kbd>+<kbd>Y</kbd>) |
| Divide / higher / lower res | <kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>D</kbd> / <kbd>Shift</kbd>+<kbd>D</kbd> | Same |
| Mask all / clear / invert / view | <kbd>Ctrl</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>H</kbd> | Same |
| Group masked | <kbd>Ctrl</kbd>+<kbd>W</kbd> | <kbd>Ctrl</kbd>+<kbd>W</kbd> |
| PolyFrame | <kbd>Shift</kbd>+<kbd>F</kbd> | <kbd>Shift</kbd>+<kbd>F</kbd> |
| Draw / Move / Scale / Rotate | <kbd>Q</kbd> / <kbd>W</kbd> / <kbd>E</kbd> / <kbd>R</kbd> | Same keys, Gizmo 3D |
| LightBox | <kbd>,</kbd> | <kbd>,</kbd> |
| Hide the palettes | <kbd>Tab</kbd> | <kbd>Tab</kbd> |
| QuickSave | <kbd>9</kbd> | <kbd>9</kbd> |
| Store Config / Load UI / Save UI | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>L</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd> | Same |
| Perspective | <kbd>P</kbd> | Always on (<kbd>P</kbd> is Paint) |
| Search every command | | <kbd>F3</kbd> |

Every shortcut is in [Keyboard and mouse](shortcuts.md), generated from the app itself, and can be changed with
<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click or **Preferences > Hotkeys**.

## See also

- [Keyboard and mouse](shortcuts.md)
- [Glossary](glossary.md): ZBrush terms and their SculptForge names
- [Exporting, Unreal and ZBrush](exporting.md): GoZ
