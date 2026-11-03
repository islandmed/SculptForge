# Coming from ZBrush

This chapter is for sculptors whose hands already know ZBrush. It lists what SculptForge calls the things you
know, what works the same, what is different, and the shortcuts side by side, so you can start sculpting instead
of relearning. SculptForge has its own keymap and offers ZBrush-style, Blender and Maya styles; turn on the
**ZBrush-style** keys first (see [ZBrush-style keys](#zbrush-style-keys)) and your hands will feel at home.

New installs start in SculptForge's own layout, with menus by task. For the alphabetical palette bar and the
two-row top shelf you know, pick **Coming from: ZBrush** (keys and layout) or the **ZBrush-style layout** card in the welcome, or pick
**Edit > Preferences > Interface > Look > Layout > ZBrush-style** (see [Looks and layouts](interface.md#looks-and-layouts)).
Paths in this guide name the SculptForge layout's menus. In the ZBrush-style layout, leave out the menu:
**Edit > Preferences > Input** is the palette named **Preferences**, **Sculpt > Stroke** the palette named
**Stroke**; **Tool > Parts** is the same in both. See [Menus and palettes](interface.md#menus-and-palettes) for
every palette's menu, and <kbd>F3</kbd> finds any of them by name.

## ZBrush terms in SculptForge

Features use SculptForge's own names. The ideas are the ones you know; here is what to look for:

| ZBrush | SculptForge | Where |
|---|---|---|
| SubTools | **Parts** | **Tool > Parts**; <kbd>Alt</kbd>+click one in the viewport. See [Parts](parts.md) |
| ZSpheres | **Forge Spheres** | **Tool > Forge Spheres**. See [Forge Spheres](forge-spheres.md) |
| Polypaint | **Vertex Paint** | The Paint brush (<kbd>P</kbd>), **Rgb**, the **Paint** menu. See [Vertex Paint and materials](vertex-paint.md) |
| Polygroups | **Face Groups** | **Tool > Face Groups**. See [Face Groups and visibility](face-groups.md) |
| LightBox | **Library** | <kbd>,</kbd> (comma). See [The interface](interface.md#the-library) |
| GoZ | **Send to ZBrush** (GoZ-compatible) | **File > Send to ZBrush**. See [Exporting](exporting.md#exchange-models-with-zbrush-send-to-zbrush) |
| MatCap | **Lit Sphere** | **Render > Materials**, <kbd>M</kbd>. See [Materials](vertex-paint.md#materials-lit-spheres) |
| Zadd / Zsub | **Add** / **Sub** | Top shelf; <kbd>Alt</kbd> flips for one stroke |
| Z Intensity | **Intensity** | Top shelf, <kbd>U</kbd> |
| Focal Shift | **Falloff** | **Sculpt > Draw & Tablet**, <kbd>O</kbd> |
| DynaMesh | **Voxel Remesh** | **Tool > Remesh**. See [Geometry](geometry.md#voxel-remesh) |
| ZRemesher, ZRemesher Guides | **Quad Remesh**, **Quad Remesh Guides** | **Tool > Geometry > Quad Remesh**. See [Quad retopology](retopology.md) |
| ProjectAll, ZProject | **Project All**, **Project** | **Tool > Parts > Project**. See [Quad retopology](retopology.md#project-all) |
| Sculptris Pro | **Dynamic Topology** | **Dynamic** on the top shelf. See [Dynamic topology](dynamic-topology.md) |
| QuickSave | **Snapshot** | **File > Save Snapshot**, the Library's **Snapshots** tab. See [Projects](projects.md#snapshots) |
| Transpose, Gizmo 3D, Transpose All | **Gizmo**, **All Parts** | <kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd>. See [Gizmo](gizmo.md) |
| LazyMouse | **Steady Stroke** (every stroke type) | **Sculpt > Stroke**, <kbd>L</kbd> |
| PolyFrame (PolyF) | **Wireframe** (**Wire** on the shelf) | Right shelf, <kbd>Shift</kbd>+<kbd>F</kbd> |
| QuickMenu | **Quick Menu** | <kbd>Space</kbd> or a right click |
| DamStandard | **Sharp Crease** brush | <kbd>B</kbd> <kbd>D</kbd> <kbd>S</kbd>. See [Sculpting](sculpting.md) |
| ClayBuildup | **Clay Block** brush | <kbd>B</kbd> <kbd>C</kbd> <kbd>B</kbd> |
| SnakeHook | **Tendril** brush | <kbd>B</kbd> <kbd>S</kbd> <kbd>H</kbd> |
| KelvinSnake | **Elastic Tendril** brush | Brush picker (KelvinGrab, KelvinTwist and KelvinScale keep their names) |
| Trim Dynamic | **Trim** brush | Brush picker |
| hPolish | **Hard Polish** | Not built in yet; a [Brush Lab](brush-lab.md) recipe gets close |
| DragRect, DragDot | **Drag Stamp**, **Slide Stamp** strokes | **Sculpt > Stroke** |
| MaskPen, MaskLasso, MaskRect | **Mask Pen**, **Mask Lasso**, **Mask Rectangle** | **Tool > Masking**. See [Masking](masking.md) |
| SelectRect, SelectLasso | **Select Rectangle**, **Select Lasso** | **Tool > Visibility** |
| FiberMesh, LightCap | Not yet | |
| ZScript, ZPlugin | Script commands and the Python module | See [Scripting](scripting.md) |
| Decimation Master, UV Master, Projection Master | **Decimate**, the **UV Editor** (and **Auto UV**), **Mesh > Generate 3D > Stamp** | See [Geometry](geometry.md#decimate), [UV Editor](uv-editor.md) and [Texture painting](texture-painting.md) |

The command palette (<kbd>F3</kbd>) finds every feature by its ZBrush name too: type `SubTool`, `ZSphere`,
`Polypaint` or `MatCap` and it shows the SculptForge command, as in "Forge Spheres (ZBrush users: ZSpheres)". The
[Glossary](glossary.md) maps many more ZBrush terms.

ZBrush, GoZ, MatCap and other ZBrush feature names are trademarks of Maxon Computer GmbH. SculptForge is not
affiliated with or endorsed by Maxon.

## ZBrush-style keys

New installs start with the **SculptForge** keys. The first time SculptForge starts it asks what you are
**Coming from**: pick **ZBrush** and it turns on the ZBrush-style keys for you. To switch at any time, open
**Edit > Preferences > Input** and choose **ZBrush-style** under **Style**, then press **Store Config**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) to keep it. If you used an earlier version, your saved choice is
kept.

What the ZBrush-style keys change:

| | **ZBrush-style** | **SculptForge** (default) |
|---|---|---|
| <kbd>1</kbd> to <kbd>9</kbd>, <kbd>0</kbd> | Free (<kbd>9</kbd> is **Save Snapshot**, ZBrush's QuickSave) | Pick the ten first brushes (Standard, Clay, Smooth, Move ...) |
| <kbd>I</kbd> | RGB Intensity at the cursor | Intensity at the cursor (as <kbd>U</kbd>) |
| <kbd>Ctrl</kbd>+drag on the background with nothing masked | Draws a mask rectangle | Zooms |
| Zoom drags (<kbd>Ctrl</kbd>+right-drag, <kbd>Alt</kbd> then release) | Up zooms out | Up zooms in |

Everything else in this chapter works in both styles. **Edit > Preferences > Input > Rotation** chooses free rotation
(as in ZBrush) or a turntable that keeps the model upright, as in Blender or Maya. Press **Store Config** to keep
either. See [Keyboard and mouse](shortcuts.md#zbrush-style-keys) for the full list.

## What's the same

- **The layout** (in the ZBrush-style layout). Shelves around the canvas, palettes in alphabetical order in the palette bar and the trays,
  sub-palettes that fold, the Library on <kbd>,</kbd>, Ctrl-hover help on every control, Ctrl+Alt+click to
  assign a hotkey, **Store Config** on <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>. See
  [The interface](interface.md).
- **Navigation.** Drag on the background to rotate freely (over the top, too), <kbd>Alt</kbd>+drag to move,
  <kbd>Alt</kbd>+click, let go of <kbd>Alt</kbd> and drag to scale, <kbd>Shift</kbd> during a rotation to snap to
  a side view, <kbd>Shift</kbd>+click, let go of <kbd>Shift</kbd> and drag to roll. Right-drag navigation works as
  with ZBrush's RightClick Navigation turned on, over the model too. <kbd>F</kbd> frames the model. See
  [Navigating the view](navigation.md).
- **Sculpting modifiers.** <kbd>Alt</kbd> swaps Add and Sub (ZBrush's Zadd and Zsub), <kbd>Shift</kbd> smooths
  (Smooth keeps its own Intensity; let go of <kbd>Shift</kbd> mid-stroke for the alternate smooth that keeps the
  volume), <kbd>Shift</kbd> pressed after a stroke starts draws a straight line.
- **Brush keys.** <kbd>B</kbd> then two letters, with ZBrush's letters: <kbd>B</kbd> <kbd>S</kbd> <kbd>T</kbd>
  Standard, <kbd>B</kbd> <kbd>C</kbd> <kbd>B</kbd> Clay Block, <kbd>B</kbd> <kbd>D</kbd> <kbd>S</kbd>
  Sharp Crease, <kbd>B</kbd> <kbd>M</kbd> <kbd>V</kbd> Move, <kbd>B</kbd> <kbd>I</kbd> <kbd>N</kbd> Inflate,
  <kbd>B</kbd> <kbd>S</kbd> <kbd>M</kbd> Smooth, <kbd>B</kbd> <kbd>S</kbd> <kbd>H</kbd> Tendril,
  <kbd>B</kbd> <kbd>P</kbd> <kbd>I</kbd> Pinch.
- **Brush settings.** <kbd>S</kbd> Draw Size, <kbd>U</kbd> Intensity, <kbd>I</kbd> RGB Intensity (ZBrush-style
  keys), <kbd>O</kbd> Falloff, each as a slider under the cursor; <kbd>[</kbd> and <kbd>]</kbd> step the size;
  <kbd>Space</kbd> opens the Quick Menu; <kbd>L</kbd> toggles Steady Stroke. The cursor's outer ring is the Draw
  Size, the inner ring the Falloff. See [Sculpting and brushes](sculpting.md).
- **Masking.** <kbd>Ctrl</kbd> paints a mask and <kbd>Ctrl</kbd>+<kbd>Alt</kbd> erases it; <kbd>Ctrl</kbd>+click
  the model blurs it and <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click sharpens it; <kbd>Ctrl</kbd>+click the background
  inverts it and <kbd>Ctrl</kbd>+drag there draws a mask rectangle (ZBrush-style keys; one that misses the model
  clears the mask). Mask Lasso and Mask Rectangle follow the symmetry: with <kbd>Ctrl</kbd> held the shelf shows
  the mask brush, and the Brush (<kbd>B</kbd>) and Stroke pickers pick it. See [Masking](masking.md).
- **Visibility and face groups.** <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag keeps the inside (press <kbd>Alt</kbd> to
  hide it instead), <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click a group to isolate it (on a partly hidden mesh: to hide
  it), <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click the background to show everything, <kbd>Ctrl</kbd>+<kbd>W</kbd> to
  group the masked part. Select Rectangle and Select Lasso, also from the Brush and Stroke pickers with
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd> held. **Group by UVs** is UV Groups. See [Face Groups and visibility](face-groups.md).
- **Parts** with <kbd>Alt</kbd>+click to select one and <kbd>Shift</kbd>+<kbd>Alt</kbd>+click to select and
  frame it; **subdivision levels** with <kbd>Ctrl</kbd>+<kbd>D</kbd>, <kbd>D</kbd> and <kbd>Shift</kbd>+<kbd>D</kbd>;
  **Wireframe** on <kbd>Shift</kbd>+<kbd>F</kbd>; **Save Snapshot** on <kbd>9</kbd> (ZBrush-style keys). See
  [Parts](parts.md) and [Subdivision levels](subdivision-levels.md).
- **Send to ZBrush** (GoZ-compatible). Send models to and from ZBrush. See
  [Exporting, Unreal and ZBrush](exporting.md#exchange-models-with-zbrush-send-to-zbrush).
- **VDM brushes** with Drag Stamp and Slide Stamp, and your own made from a VDM plane. See
  [Vector displacement brushes](sculpting.md#vector-displacement-brushes).
- **Sculpting in Blender too?** The Blender add-on can give Blender's Sculpt Mode ZBrush's keys. See
  [Working with Blender](blender.md#zbrush-style-keys-in-sculpt-mode).

## Bring your ZTools over

ZBrush's `.ZTL` files are proprietary, so SculptForge can't read them directly. The free **SculptForge ZTL
Export** plug-in runs inside ZBrush and converts them once:

1. Download [SculptForgeZToolExport.txt](https://github.com/islandmed/SculptForge/blob/main/integrations/zbrush/SculptForgeZToolExport.txt) (also attached to every
   release) and copy it into ZBrush's `ZStartup/ZPlugs64` folder, then restart ZBrush. Or load it once with
   **ZScript > Load**.
2. In ZBrush, **ZPlugin > SculptForge > Convert ZTL** asks for a `.ZTL`, loads it and writes each SubTool next to
   it as a GoZ-compatible file (`Dragon.ZTL` gives `Dragon.sf001_Body.GoZ`, `Dragon.sf002_Horns.GoZ`...).
   **Save for SculptForge** does the same for the ZTool you have open, saving the `.ZTL` where you choose.
3. In SculptForge, open the `.ZTL` (**File > Open**, or drop it on the window). Its SubTools open as Parts in the
   same order with the same names, face groups, vertex paint, mask and UVs.

Each SubTool comes over at its highest subdivision level; the lower levels, sculpt layers and ZBrush materials
don't. If you change the ZTool in ZBrush later, convert it again: SculptForge warns when the `.ZTL` is newer than
its conversion. `.ZPR` projects can't be converted; save their ZTools first.

> **Note:** the plug-in is new and hasn't been tested in every ZBrush version yet. If it stops with an error or a
> SubTool is missing, please [report it](https://github.com/islandmed/SculptForge/issues) with your ZBrush
> version; exporting each SubTool from ZBrush as a `.GoZ` or `.OBJ` file works in the meantime.

## What's different

| ZBrush | SculptForge | Why |
|---|---|---|
| Transpose action line (<kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd>) | The Gizmo on the same keys; mask what should stay, and <kbd>Alt</kbd>+click to put the pivot on the model | Simpler to learn; ZBrush has a Gizmo 3D too. See [Gizmo](gizmo.md) |
| Edit mode (<kbd>T</kbd>) and the 2.5D canvas | Always in Edit mode | SculptForge is a 3D sculpting app only |
| Perspective toggle (<kbd>P</kbd>) | Always perspective; <kbd>P</kbd> is the Paint brush (colour only: Rgb on, Add off) | |
| Stroke replay (<kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd>) | Not yet | |
| <kbd>N</kbd> lists the SubTools | The Parts sub-palette in the right tray | |
| DynaMesh, ZRemesher | **Voxel Remesh** and **Quad Remesh** in **Tool > Geometry** | See [Geometry](geometry.md) and [Retopology](retopology.md) |
| Sculptris Pro | **Dynamic** on the top shelf, with its **Detail** slider | See [Dynamic topology](dynamic-topology.md) |
| Open edges hold their shape when smoothed | Smooth pulls an open border in a little with each pass | Mask the border first when it matters |
| Values 0-100 everywhere | The same for Intensity, RGB Intensity and Falloff; Draw Size is the radius in pixels | |

## Shortcuts side by side

The SculptForge column is with the ZBrush-style keys on.

| Action | ZBrush | SculptForge |
|---|---|---|
| Rotate / move / scale the view | Drag / <kbd>Alt</kbd>+drag / <kbd>Alt</kbd>, release, drag on the background | Same |
| Snap to a side view / roll | <kbd>Shift</kbd> while rotating / <kbd>Shift</kbd>+click, release, drag | Same |
| Frame | <kbd>F</kbd> | <kbd>F</kbd> |
| Brush pop-up | <kbd>B</kbd> | <kbd>B</kbd> |
| Draw Size / Z Intensity / RGB Intensity / Focal Shift | <kbd>S</kbd> / <kbd>U</kbd> / <kbd>I</kbd> / <kbd>O</kbd> | Same keys: Draw Size / Intensity / RGB Intensity / Falloff |
| QuickMenu | <kbd>Space</kbd> or right-click | Quick Menu: <kbd>Space</kbd> or right-click (a right drag navigates) |
| LazyMouse | <kbd>L</kbd> | Steady Stroke: <kbd>L</kbd> |
| Pick colour / switch colour | <kbd>C</kbd> / <kbd>V</kbd> | Same |
| Symmetry | <kbd>X</kbd> | <kbd>X</kbd> |
| Undo / redo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Same (also <kbd>Ctrl</kbd>+<kbd>Y</kbd>) |
| Divide / higher / lower res | <kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>D</kbd> / <kbd>Shift</kbd>+<kbd>D</kbd> | Same |
| Mask all / clear / invert / view | <kbd>Ctrl</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>H</kbd> | Same |
| Group masked | <kbd>Ctrl</kbd>+<kbd>W</kbd> | <kbd>Ctrl</kbd>+<kbd>W</kbd> |
| PolyFrame | <kbd>Shift</kbd>+<kbd>F</kbd> | Wireframe: <kbd>Shift</kbd>+<kbd>F</kbd> |
| Draw / Move / Scale / Rotate | <kbd>Q</kbd> / <kbd>W</kbd> / <kbd>E</kbd> / <kbd>R</kbd> | Same keys, Gizmo |
| LightBox | <kbd>,</kbd> | Library: <kbd>,</kbd> |
| Hide the palettes | <kbd>Tab</kbd> | <kbd>Tab</kbd> |
| QuickSave | <kbd>9</kbd> | Save Snapshot: <kbd>9</kbd> |
| Store Config / Load UI / Save UI | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>L</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd> | Same |
| Perspective | <kbd>P</kbd> | Always on (<kbd>P</kbd> is Paint) |
| Search every command | | <kbd>F3</kbd> (ZBrush names work too) |

Every shortcut is in [Keyboard and mouse](shortcuts.md), generated from the app itself, and can be changed with
<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click or **Edit > Preferences > Hotkeys**.

## See also

- [Keyboard and mouse](shortcuts.md)
- [Glossary](glossary.md): more ZBrush terms and their SculptForge names
- [Exporting, Unreal and ZBrush](exporting.md): Send to ZBrush
