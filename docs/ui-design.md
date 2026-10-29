# SculptForge UI spec: a ZBrush-shaped shell

Status: design spec, ready to implement. Target: Dear ImGui **1.91.9** (pinned in `cmake/Dependencies.cmake`, non-docking), SDL3, OpenGL.
Reference picture: `~/Downloads/Core-interface.jpg` (ZBrush Core 1920x1080).

## 0. Goals and non-goals

ZBrush users should feel at home within a minute:

- a **bounded central canvas**, surrounded by **shelves** that hold the most-used controls (Tab hides them);
- an **alphabetical palette bar** under the title strip that holds *every* function; palettes open as drop-downs and can be pinned into the **left or right tray**;
- **LightBox** on `,` (comma): a horizontal thumbnail strip along the top of the canvas;
- a **ZScript-style console** at the bottom;
- **Ctrl+hover** on any control shows its description and shortcut;
- **Customize**: Preferences > Config > Enable Customize, Ctrl+Alt+drag to move or remove items, **Store Config (Ctrl+Shift+I)**, **Restore Standard UI**.

Not goals: free-floating or OS-level detachable panels, tabbed docking, a full keymap editor, ZBrush 2.5D canvas or pixols. These need ImGui's docking branch or aren't worth the cost (see §12).

The current UI (screenshot, 1600x960) is a menu bar (File/Edit/Mesh/View/Help) plus floating windows over a full-window viewport: Tools (250 px, left), SubTools, Layers / Blend Shapes, Game Ready, Brush Lab (open by default, covers the model), and optionally Polypaint & Materials, Generate 3D, Gaussian Splats. All of the features stay. Only where they live changes.

---

## 1. What ZBrush (and others) actually do: reference notes

| Element | Concrete behaviour (source) |
|---|---|
| Title bar | Left: version, memory, PolyCount / ActivePoints. Right: QuickSave, See-through slider, "Menus" toggle (show or hide the palette bar), DefaultZScript, layout preset cycle buttons, help [R1][picture] |
| Palette bar | Palettes listed **alphabetically** under the title bar (Alpha, Brush, Color, Document, Draw, Edit, File, Layer, Light, ... Tool, Transform, Zoom, Zplugin, Zscript). Clicking a name drops the palette down. Sub-palettes inside are context-sensitive [R1][R2] |
| Sub-palettes | One open at a time by default; **Shift+click** opens more (Preferences > Interface > One Subpalette) [R2][R3] |
| Trays | Left and right trays hold whole palettes. Right is open by default, left closed. **Double-click or click the divider** to toggle. A palette goes into a tray when you click its icon (top-left of the palette) or drag that icon to a tray. Only palettes go in trays [R1][R3][R4] |
| Shelves | Surround the canvas. Each grows to fit its largest item. **Tab** hides or shows them. Menus can't go on shelves [R1][R2][R4] |
| Top shelf | LightBox button; Draw / Move / Scale / Rotate (Q W E R); Mrgb / Rgb / M; Rgb Intensity; Zadd / Zsub; Z Intensity; Focal Shift; Draw Size; ActivePoints / TotalPoints readout [picture][R1] |
| Left shelf | Big thumbnail buttons: Brush, Stroke, Alpha, Texture, Material, then a colour picker, two swatches, "Alternate", "FillObject" [picture][R1] |
| Right shelf | Canvas: BPR, Scroll, Zoom, Actual, AAHalf. Object: Persp, Floor, Local, L.Sym, Xyz, **Frame, Move, Scale, Rotate** (drag on the button to navigate), PolyF, Transp, Ghost, Solo [picture][R1] |
| Bottom shelf (Core) | One row of the most-used brushes: ClayBuildup, Standard, DamStandard, Move, TrimDynamic, hPolish, Pinch, Inflate, IMM Primitives, Paint, Transpose, SelectLasso, SelectRect [picture] |
| Pop-up pickers | Any preview thumbnail with changeable contents opens a pop-up grid when clicked (Tool, Brush, Stroke, Alpha, Texture, Material) [R1] |
| Brush pop-up | **B** opens it. Typing a letter narrows the grid to brushes starting with that letter and shows a second key on each. Pressing it selects (B, S, T = Standard). Holding Ctrl while you pick chooses a masking brush [R5][R6] |
| LightBox | **`,`** toggles it. Category tabs along its top: Project, Tool, Brush, Material, Alpha, Texture, LightCaps, QuickSave (+ Recent). Back and forward arrows; an up-folder tile; **double-click** to enter folders or load items; drag in the strip to scrub sideways; 1-4 rows of icons; placement top or bottom (preference); thumbnail glow and label brightness preferences. Assets split into built-in (Maxon) and User [R7] |
| Ctrl-hover help | **Ctrl + cursor over item** shows the item's description when "Popup Info" is on. **Ctrl+Alt + hover** shows an alternative description. Opacity is set by Preferences > Icolors > Quick Info Opacity [R6][R8] |
| Quick Info | Large thumbnail viewer when you hover items in the Alpha, Material, Texture and Tool palettes. Has a delay slider, a "Large Quick Info Text" option, and Preview Material On Mesh [R9] |
| Customize | Preferences > Config > **Enable Customize**, then **Ctrl+Alt+drag** an item to move it. Dragging it to an invalid place (e.g. the canvas) **removes** it. Custom menus are made with Custom UI > Create New Menu. Items fill left to right. Element sizes are 1, 3/4, 1/2 or 1/4 of the width, 1/8 high [R4] |
| Config | **Store Config = Ctrl+Shift+I** (ZBrush never saves preferences automatically). Alt+Ctrl+Shift+I stores an alternate layout, Ctrl+L loads one. **Restore Standard UI** returns to the default; Restore Custom UI goes back to the stored one [R2][R4][R10] |
| Shortcuts | Tab shelves, `,` LightBox, B brush pop-up, Space QuickMenu, S / I / O size / intensity / focal, X symmetry, F frame, Shift+F polyframe, Ctrl+D divide, D / Shift+D sdiv up / down, Ctrl+Z / Ctrl+Shift+Z, Shift+Ctrl+I store config, H hide ZScript [R6] |
| Blender 4.3+ | Brushes are assets in an **asset shelf** at the bottom of the viewport (Shift+Space toggles it; drag its border to resize). Right-click a brush > Assign Shortcut [R11][R12] |
| Nomad Sculpt | Tools on the right; Radius / Intensity sliders and a "Sub" (invert) button on the left; a bottom bar of shortcuts that users can bind (Undo, Redo, Solo by default); top-left stats; nav cube; a mirrored left-handed layout [R13] |
| 3DCoat | "Rooms" show only the tools for one task. The Brush Options panel starts with an alpha preview. An "Activity bar" of pop-up libraries (Brushes, Strips, Stencils, Smart Materials...). Space shows the tool pop-up [R14] |

What we take from the others: Blender's **brush asset shelf**, which is our bottom shelf, plus "Assign Shortcut" on right-click. Nomad's **left-handed mirror** costs almost nothing in a data-driven layout (swap left and right). The command palette and console are our own additions. They exist because every SculptForge action is already a named, described command.

---

## 2. Target layout

All sizes are logical px at `uiScale_ = 1` and get multiplied by `uiScale_`. Shells are computed every frame from the main viewport; nothing is placed by hand.

### 2.1 Size table

| Region | Size | Spans |
|---|---|---|
| Title strip | h 24 | full width |
| Palette bar | h 22 (hidden by "Menus" toggle -> 0) | full width |
| Top shelf | h 64 (two rows of 26 + padding) | from left-tray edge to right-tray edge |
| Left shelf | w 88 (tiles 76x76 + 12 label) | between top and bottom shelf |
| Right shelf | w 52 (buttons 44x40, icon + 10 px label) | between top and bottom shelf |
| Bottom shelf | h 60 (tiles 40 icon + 12 label) | same span as top shelf |
| Console (closed by default) | h 160, drag 100-400 | above bottom shelf, same span |
| Tray divider | w 8 each side, always visible | top of palette bar to bottom |
| Right tray | w 300 (1600 wide) / 320 (>=1920), drag 240-480 | palette bar bottom to window bottom |
| Left tray | 0 (closed) by default, 300 when open | same |
| LightBox | h = 28 (tab row) + rows x 118 (96 thumb + 22 label), 1 row default | overlays the top of the canvas, full canvas width |
| Canvas | the rest | 3D view renders **only** here |

### 2.2 1600 x 960 (default, right tray open, left tray closed)

```
x:0 8      96                                                              1240   1292 1300                1600
  +-------------------------------------------------------------------------------------------------------------+ y0
  |SF SculptForge  head.sfproj*  | Verts 5.1k  Tris 10.2k  SubTool Sphere  Layer Base   status msg... [QS][1][2][3][Menus][? Search F3]|
  +-------------------------------------------------------------------------------------------------------------+ y24
  |Alpha Brush Color Draw Edit File Generate Help Material Preferences Render Script Splats Stroke Tool Transform |
  +--+-------------------------------------------------------------------------------------+-+-------------------+ y46
  |  |[LightBox] [Sculpt|Paint] [Zadd|Zsub] [Rgb] [Draw Size 60 ====] [Strength 0.50 ====] | | Tool          [x] |
  |  |[AI Brush] [Front] [ X | Y | Z sym ]  [Pressure>Str] [Pressure>Size]   5.1k / 10.2k   | | Sphere 5.1k  (o)  |
  +--+------+--------------------------------------------------------------------+---------+ | [Open][Save][Imp] |
  |d | +--+ |                                                                    | [Frame] |d| > SubTool  (open) |
  |i | |Br| |                                                                    | [Move ] |i|   Sphere      eye |
  |v | +--+ |                                                                    | [Zoom ] |v|   Helmet      eye |
  |  |Clay  |                                                                    | [Rotat] | |   [Append][Dup]...|
  |  | +--+ |                                                                    | [Persp] | | > Layers          |
  |  | |St| |                          CANVAS                                    | [Floor] | | > Geometry        |
  |  | +--+ |                     (3D view, gradient bg)                         | [PolyF] | | > Remesh          |
  |  |Dots  |                        1144 x 790                                  | [Grps ] | | > Masking         |
  |  | +--+ |                                                                    | [Mask ] | | > Polygroups      |
  |  | |Al| |                                                                    | [Paint] | | > Visibility      |
  |  | +--+ |                                                                    | [Solo ] | | > Export          |
  |  |Round |                                                                    | [Splat] | | > Display         |
  |  | +--+ |                                                                    |         | |                   |
  |  | |Mt| |                                                                    |         | |                   |
  |  | +--+ |                                                                    |         | |                   |
  |  |Studio|                                                                    |         | |                   |
  |  |[colr]|                                                                    |         | |                   |
  |  |[sw|sw|                                                                    |         | |                   |
  +--+------+--------------------------------------------------------------------+---------+ |                   | y900
  |  | (o)   (o)   (o)   (o)   (o)   (o)   (o)   (o)   (o)   (o)  (o)  (o)  (o)  (o) [+]  | |                   |
  |  |Std   Clay  ClayB DamSt Move  Smth  Infl  Pnch  Trim  Snake Layer Nudge Paint  ...  | |                   |
  +--+-------------------------------------------------------------------------------------+-+-------------------+ y960
```

Canvas = x 96..1240, y 110..900 (1144 x 790). With LightBox open (1 row), it covers y 110..256 of the canvas. With the console open, the canvas bottom moves to y 740.

### 2.3 1920 x 1080

Same structure. The right tray is 320 wide (x 1600..1920), the divider is at 1592, and the right shelf is at 1540..1592. Canvas = x 96..1540, y 110..1020 (**1444 x 910**). The extra width goes to the canvas only. Shelves keep their fixed sizes, as in ZBrush.

### 2.4 Narrow windows (< 1280 logical px wide)

The right tray auto-collapses to its divider. Palettes are then used as drop-downs only. Left-shelf tiles shrink to 60. The bottom shelf scrolls sideways (mouse wheel).

### 2.5 Canvas ownership (important engineering change)

Today `renderer_.draw(camera_, fbW, fbH, ...)` fills the window, and picking and the brush cursor use window coordinates. The shell introduces:

```cpp
// App.h (shell section)
struct CanvasRect { float x, y, w, h; };          // logical px, window coordinates
CanvasRect canvasRect() const;                     // from Layout, recomputed every frame
```

- `Renderer::draw(...)` gets a viewport rect (`glViewport` / `glScissor` to the canvas in framebuffer pixels). `camera_.aspect = w/h` of the canvas.
- `pick`, `onMouseDown/Move`, the brush cursor, `onScreen`, `hide_rect` and polygroup overlays all convert window -> canvas coordinates in one helper.
- Mouse events outside the canvas never start strokes or orbits, even when ImGui doesn't capture them (e.g. on gaps between shelf items).
- Recording, `--render`, demos and `hideUi_` keep using the full window (`canvasRect()` returns the whole viewport when `hideUi_` is set).
- The area outside the canvas is cleared to `shellBg`. The canvas itself keeps the current gradient background and gets a 1 px `#0e0e10` border.

---

## 3. Contents of every region, mapped to SculptForge

Notation: `cmd:name` = CommandRegistry command, `act:id` = UI action (§8), `widget:x` = custom widget.

### 3.1 Title strip (h 24)
Left to right:
1. App glyph + "SculptForge".
2. Document name + `*` when `doc.dirty`. Click: Save.
3. Info readout (dim text, like ZBrush's memory and PolyCount line): `Verts 5.1k  Tris 10.2k  SubTool <name> (2/5)  Layer <name|Base>  60 fps`. Splat count when splats are loaded.
4. Status message (`status_`), elided. Errors in orange-red for 6 s.
5. Right-aligned:
   - `[QuickSave]`: `act:file.quicksave` writes a timestamped `.sfproj` to `<user lib>/QuickSave/`, keeping the last 10. It shows up in the LightBox QuickSave tab.
   - Layout preset buttons `[1][2][3]` (§9.3).
   - `[Menus]` toggles the palette bar.
   - A search field showing "Search  F3" that opens the command palette (§7).

### 3.2 Palette bar (h 22): alphabetical, every function
Each name is a button. A click opens the palette as a drop-down window under its name (an ImGui popup, width 300, max height = window - 60, scrolls). The palette header row has the **tray icon** (Lucide `panel-right`; Shift+click it to go left), the palette title and a close button. Clicking the icon moves the palette into the right tray, and you can also drag the icon onto a tray. A palette that is already in a tray shows its name in accent colour, and clicking the name scrolls the tray to it and flashes it.

Every palette's body is one function `void drawPalette_<Id>()`, and the same function draws it in a drop-down or in a tray.

| Palette | Contents (existing code it comes from) |
|---|---|
| **Alpha** | Large preview; built-in grid (round, square, noise); Load image...; Invert loaded image; recent alphas. (`PanelBrushes.cpp` alpha section, `cmd:brush_set_alpha`) |
| **Brush** | Brush grid (the same widget as the picker, §4.1); Custom brushes list; Brush Lab (AI)... button; recipe editor for the active custom brush (`drawRecipeEditor`); Save brush... / Load brush... (`cmd:brush_save`, `cmd:brush_load`); Front faces only; Invert (Alt) |
| **Color** | Polypaint colour picker and swatches; Fill object (`cmd:paint_fill`); Clear (`cmd:paint_clear`); From cavity (`cmd:paint_from_cavity`); Pick under cursor (C). (`PanelPolypaint.cpp` minus the material part) |
| **Draw** | Draw Size, Strength (Z Intensity), Zadd/Zsub, Rgb (`paintRgb_`), Paint mode, Tablet: Pressure > strength, Pressure > size, pen readout |
| **Edit** | Undo <label>, Redo <label> (`cmd:undo`, `cmd:redo`) |
| **File** | New Sphere, New Plane, Open..., Open Recent >, Save, Save As..., Autosave interval; Import/Export: Export OBJ, GLB (blend shapes), LOD chain, USD/USDZ, FBX for Unreal, Send to Unreal, Save Splats...; GoZ >; Quit. (Today's File menu, `drawFileMenuProjectItems`, `drawGozMenu`) |
| **Generate** | Generate 3D (image/text -> 3D, ComfyUI) opens the existing Generate 3D window; Image Relief / Inflate / Project (`cmd:image_relief`, `image_inflate`, `image_project`); AI provider settings (`drawAiSettings`) |
| **Help** | Controls (shortcut sheet generated from the Action registry), User Guide (Shift+F1), What's New (also opened once after each update), Command palette (F3), Website & documentation, Report a problem, About SculptForge |
| **Material** | Studio + MatCap thumbnail grid (`cmd:set_material`); base colour (today's "Material" ColorEdit in Tools) |
| **Preferences** | Sub-palettes: **Config** (Enable Customize, Store Config Ctrl+Shift+I, Restore Custom UI, Restore Standard UI, Save UI..., Load UI... Ctrl+L); **Interface** (UI scale, Popup Info, Plain tooltips delay, One sub-palette open, Left-handed layout); **LightBox** (library folder, Show at startup, Startup rows, Placement top/bottom); **Hotkeys** (list with reset; Store); **Tablet** (pen, pressure meter, test area, pressure curve, eraser and side-button mappings; see docs/tablet.md); **Autosave** |
| **Render** | Wireframe (Shift+F), Show Mesh, Show Polypaint, Show Mask, Show Polygroups, Show Splats, Frame Model (F) (today's View menu render toggles) |
| **Script** | Show console (`` ` ``), Run script file..., Copy session as script, Clear console |
| **Splats** | `drawSplatSection` body (info, clean, transform, to mesh, save, clear) |
| **Stroke** | Stroke type (Dots, DragRect, Spray, LazyMouse) and its settings (spray scatter, lazy radius) |
| **Tool** | The ZBrush "Tool" palette. Header: active SubTool name + tri count, `[Open] [Save] [Import]`. Sub-palettes (collapsing headers, one open at a time, Shift+click for more): **SubTool** (SubTools panel body), **Layers** (Layers / Blend Shapes body), **Geometry** (Lower Res / Higher Res, SDiv slider, Del Lower / Del Higher, Reconstruct Subdiv, Divide Ctrl+D + Smt, Subdivide, Decimate, `cmd:decimate`), **Remesh** (Voxel remesh + resolution, the "DynaMesh" analogue), **Masking** (`drawMaskingSection`), **Polygroups** (`drawPolygroupsSection`), **Visibility** (Show all, Invert visibility, Delete hidden), **Export** (Game Ready panel body: LODs, Unreal, GLB/LOD chain), **Display** (wireframe, show mask/groups, material colour) |
| **Transform** | Draw / Move / Scale / Rotate (Q W E R, Gizmo 3D: pivot, World / Surface axes, Mask, All SubTools, Falloff, numeric entry; `cmd:transform`), Symmetry X / Y / Z (X), Frame (F), Solo (`cmd:subtool_solo`), Wireframe |

ZBrush has no separate top-level SubTool / Layer / Masking / Polygroups palettes, so they are **Tool** sub-palettes only. The Mesh and View menus disappear, and their items move to Tool > Geometry/Remesh and Render. Palettes are sorted by name at registration, so a new palette lands in alphabetical order automatically.

### 3.3 Top shelf (h 64, two rows)
Row 1:
- `[LightBox]` (`act:ui.lightbox`, `,`)
- `[Sculpt | Paint]` (`paintMode_`)
- `[Zadd | Zsub]` (`invertToggle_`)
- `[Rgb]` (`paintRgb_`)
- `[Vertex | Texture]` paint target (`act:draw.paint_vertex`, `act:draw.paint_texture`; contextual: only when the SubTool has a texture)
- `Draw Size 60` slider (w 180; `radiusPx_`, `S`)
- `Strength 0.50` slider (w 180; `strength_`, `I`)

Row 2:
- `[AI Brush]` (toggles the Brush Lab window)
- `[Front]` (front faces only)
- `[Dynamic]` dynamic topology (`act:draw.dynamic`, ZBrush's Sculptris Pro) and `Detail 50` slider (w 120; contextual: only while Dynamic is on)
- `[X][Y][Z]` symmetry (`X` toggles X)
- `[Pressure>Str] [Pressure>Size]`
- right-aligned `5.1k / 10.2k` (ActivePoints / TotalPoints analogue: visible vs total tris)

**Contextual controls.** A shelf button that only applies in some situations (the Vertex / Texture paint target
needs a texture, Splats needs a capture, Detail needs Dynamic on) is *hidden* while it doesn't apply, rather than shown greyed out, so the
shelves don't fill with dead buttons. Customize mode shows them all so they can be moved or removed. Palette
entries stay visible and disabled (with the reason in Ctrl help), because palettes are where users look things up.

Sliders use the ZBrush style: the label sits inside the frame (`SliderFloat("##size", ..., "Draw Size %.0f")`), and the grab is a thin accent bar.

### 3.4 Left shelf (w 88)
A vertical stack of **thumbnail tiles** (76x76, label under). A click opens the pop-up picker beside the tile (§4):
1. Brush: current brush thumbnail + name. Picker §4.1.
2. Stroke: icon for Dots / DragRect / Spray / LazyMouse. Picker = 4 tiles + settings.
3. Alpha: alpha preview (today's 48 px preview). Picker = alpha grid + Load image.
4. Material: matcap sphere. Picker = material grid.
5. Colour: a 76x76 SV square + hue strip (`ImGui::ColorPicker3` with `NoSidePreview|NoInputs|NoLabel`, width 76), then two swatches (primary / secondary, a click swaps them, like "Alternate"), then `[Fill]` (`cmd:paint_fill`).

ZBrush's Texture slot is left out (no texture feature yet). It's reserved in the layout data so it can be added later.

### 3.5 Right shelf (w 52)
Icon + tiny label, 44x40 each. Groups are separated by 8 px.
- **Navigation**: Frame (F). **Move, Zoom and Rotate are drag buttons**: left-drag on the button pans / zooms / orbits the camera, as in ZBrush. Implement with `InvisibleButton` + `IsItemActive()` + `io.MouseDelta`, feeding the existing camera functions.
- **View**: Persp (only if the camera supports ortho; otherwise leave it out), Floor (optional grid, only if cheap), PolyF = wireframe (Shift+F; W is the Gizmo 3D's Move since 0.5), Groups (`showPolygroups`), Mask (`showMask`), Paint (`showPolypaint`).
- **Object**: Solo (`cmd:subtool_solo`), Splats (show splats; shown only when splats are loaded).

Toggles that are on get an accent background.

### 3.6 Bottom shelf (h 60): quick brushes (ZBrush Core / Blender asset shelf)
Default contents, in order: Standard, Clay, ClayBuildup, DamStandard, Move, Smooth, Inflate, Pinch, Flatten, Crease, Trim, SnakeHook, Layer, Nudge, Paint. Then any custom brushes the user adds, and a `[+]` tile that opens the brush picker in "add to shelf" mode.

Tiles are 40 px brush thumbnails with the name under them. The active brush gets an accent outline. Hotkey digits (1-9, 0) are drawn small in the tile's top-left. Right-click a tile: "Remove from shelf", "Assign shortcut...". Right-click in the brush picker: "Add to bottom shelf".

### 3.7 Right tray (default) / left tray (empty by default)
- Right tray default stack: **Tool** (SubTool sub-palette open).
- Trays scroll vertically. Palette headers are full-width bars (h 22, `#2e2e32`) with the tray icon (click returns the palette to the bar), the title and a collapse arrow.
- Divider: 8 px. Click or double-click toggles the tray, and dragging resizes it (cursor `ResizeEW`). It draws a 2 px grip in its centre.
- With Preferences > Interface > "Auto Collapse", opening one palette in a tray collapses the others when they'd overflow (ZBrush default: on for the left tray, off for the right).

### 3.8 Floating windows that stay floating
Brush Lab, Generate 3D, About, Controls, project / SubTool confirmation pop-ups. They keep their current ImGui windows and their imgui.ini persistence. Two changes: **Brush Lab defaults to closed**, opened from the top-shelf `[AI Brush]` or Brush palette. Its first-use position is the canvas top-left + (16,16), so it no longer sits over the model at 1600x960.

---

## 4. Pop-up pickers (ZBrush-style)

All pickers are ImGui pop-up windows (`BeginPopup`). They open next to the shelf tile that opened them (to its right, top-aligned), or **under the mouse** when opened from a key. They close on pick, Esc, or a click outside. Tiles are 64x64 + 16 label, 6 px gap, 6 columns (max width ~460), with section headers ("Built-in", "Custom", "Library").

### 4.1 Brush picker (B, or click the Brush tile)
- A "Recent" row of the last 8 brushes at the top.
- Grid sections: **Built-in** (the 14 `BrushType`s + Paint), **Custom** (`doc.brushes` recipes, with AI-made ones marked by a small sparkle badge), **Library** (`.sfbrush` files in `<user lib>/Brushes` that aren't loaded yet; picking one runs `cmd:brush_load`).
- **Type-to-filter, the ZBrush way.** The first letter typed keeps only brushes whose name starts with it, and the grid shows letter-index chips A-Z with counts at the top. Each remaining tile then shows an **orange key badge**: the shortest next letter that tells it apart (Standard after S -> `T`, SnakeHook -> `N`, Smooth -> `M`). Pressing that letter picks the brush.
  - If no prefix matches, fall back to a substring match (shown in a filter field at the top).
  - Backspace undoes one letter. Enter picks the first match.
  - Thumbnails come from §6 (fallback: Lucide glyph + name).
- Hover: Quick Info, meaning a 192 px thumbnail and the brush description (recipe `description`). With Ctrl held you get the standard Ctrl help.
- Right-click a tile: Add to bottom shelf / Assign shortcut... / Save as .sfbrush... / Delete (custom only).

### 4.2 Stroke, Alpha and Material pickers
- Same tile grid, no letter badges (few items), but typing still filters.
- **Material** is also opened with **M** (currently unbound in SculptForge).
- Hovering a material tile previews it on the model (ZBrush "Preview Material On Mesh"): set `renderSettings_.matcap` temporarily and restore it on leave.

### 4.3 Size / Strength at cursor
`S` and `I` open a one-slider pop-up under the mouse ("Draw Size 60", 200 px wide, already focused for drag or typing). It closes on release, Enter or Esc. `[` and `]` keep working.

### 4.4 Optional: Space quick menu (P2)
Holding Space shows a compact panel under the cursor: Brush / Stroke / Alpha / Material tiles + Draw Size + Strength. Releasing Space closes it. Only do this if time allows. It's ZBrush's QuickMenu.

---

## 5. LightBox (`,`)

### 5.1 Layout
- An overlay window anchored to the **top edge of the canvas**, full canvas width, `NoDecoration|NoMove|NoSavedSettings`, background `#141416` at 96%. Preference "Placement: bottom" anchors it above the bottom shelf instead.
- **Header row (h 28)**: tabs (text buttons, active = accent underline):
  `Project | Tool | Brush | Alpha | Material | Image | Splats | QuickSave | Recent`
  then back / forward arrows (tab+folder history), a breadcrumb (`User > Tools > creatures`), a filter field (w 160, our addition; ZBrush has none), the rows selector `[1][2][3][4]`, `[Open folder...]` (picks a folder and adds it as a library root), and close.
- **Strip**: tiles 96x96 + 22 label (elided, full name in hover). Tiles flow top-to-bottom within the row count, then left to right, so the strip scrolls **horizontally**.
  - The mouse wheel scrolls sideways.
  - Left-drag on empty space or a tile **scrubs** (ZBrush). Drag starts after 4 px, so a click is still a click.
  - The first tile in a sub-folder is the up-folder tile. Folders show a folder glyph and are entered with a double-click.
- Hover: 1.5x scale glow (ZBrush thumbnail glow) and full path. Ctrl+hover shows Ctrl help with file size, modified date and (for meshes) the cached triangle count.

### 5.2 Sources (what SculptForge shows)
Library roots:
- **Built-in**: assets shipped with the app. `assets/demo/*` (lion-head.glb, teddy-sketch.png), built-in brushes, built-in alphas, matcaps, primitives.
- **User**: `SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)/SculptForge/`, created on first run with `Projects/ Tools/ Brushes/ Alphas/ Images/ Splats/ QuickSave/`. The Preferences > LightBox "library folder" setting overrides it.

Each tab shows a "Built-in" and a "User" folder at its root, as ZBrush shows Maxon and User.

| Tab | Shows | Double-click | Other actions |
|---|---|---|---|
| Project | `*.sfproj` in User/Projects + `recentFiles_` projects | open (via `confirmDiscard`) | |
| Tool | meshes `.obj .glb .gltf .fbx .stl .ply .usda .usdz .GoZ` + built-in primitives Sphere, Plane, Cube (`makeCube` exists) + demo lion | open (replace scene, with `confirmDiscard`) | **Shift+double-click = append as SubTool** (`cmd:subtool_append`) |
| Brush | built-in brush types, `doc.brushes`, `.sfbrush` files | activate / `cmd:brush_load` | |
| Alpha | round/square/noise + images in User/Alphas | `cmd:brush_set_alpha` | |
| Material | Studio + every matcap | `cmd:set_material` | |
| Image | images in User/Images + demo sketch | small menu: Generate 3D... / Relief / Inflate / Project onto model / Use as alpha | |
| Splats | `.ply .splat` captures, splat `.glb` | `cmd:open` (loads as reference) | |
| QuickSave | QuickSave + autosave files, newest first | open | |
| Recent | `recentFiles_` (all kinds) | open | |

- Right-click any tile: Open / Append as SubTool (meshes) / Show in file manager (`SDL_OpenURL("file://...")`) / Refresh thumbnail.
- Dragging a tile onto the canvas = double-click.
- Directory listing runs on a worker thread and is re-scanned when a tab opens and every 5 s while LightBox is open (cheap `last_write_time` check).
- Preferences > LightBox > "Show at startup" defaults **on**, opening the Project tab (ZBrush behaviour). It's forced off for `--ui-test`, `--render`, recording and demos.

---

## 6. Thumbnails

`ThumbnailCache` gives every visual asset an `ImTextureID`:

- **Meshes and projects**: rendered offscreen. Use `Renderer::renderToImage`, or a new `Renderer::renderMeshThumbnail(const Mesh&, matcap, size, rgba&)` if `renderToImage` is tied to the synced scene. Size 192x192 (displayed at 96, crisp on HiDPI), studio material, transparent background, camera framed on the bounds from a 3/4 front view (yaw 30, pitch 15).
  - Mesh loading uses the existing importers on a worker thread. GL rendering happens on the main thread, **at most 2 thumbnails per frame**.
  - Projects: `saveProject` also writes `thumbnail.png` (256x256, current camera) into the `.sfproj` ZIP. The LightBox reads that instead of loading the whole project. Older projects without it are loaded on the worker and rendered once.
- **Brushes**: on a worker, sculpt one S-curve stroke with the brush on a fresh `makePlane(96)` (or an icosphere sdiv 4 for Inflate/Pinch-type brushes) using the same `Sculptor`/recipe code as real strokes. Then render it with clay material, side-lit. Key = brush type or recipe JSON hash.
- **Alphas**: the alpha's grayscale samples straight into a texture.
- **Materials**: a sphere rendered with that matcap.
- **Images**: stb-decoded, downscaled to 192.
- **Disk cache**: `<prefDir>/thumbs/<fnv1a64(kind|absPath|size|mtime|version)>.png` (stb_image_write is in third_party). In-memory LRU of 256 GL textures.
- **Placeholder**: while a thumbnail is pending, the tile shows the Lucide glyph for its kind (`box`, `brush`, `circle-dashed`, `palette`, `image`, `sparkles`, `folder`) on `#2a2a2e`.

---

## 7. Ctrl-hover help, command palette, console

### 7.1 Ctrl-hover help ("Popup Info")
- **Trigger**: an item is hovered (`ImGuiHoveredFlags_AllowWhenDisabled`) **and Ctrl is held**. No delay while Ctrl is down. Preferences > Interface > Popup Info (default on).
- **Ctrl+Alt+hover** shows the alternate page: the command's parameter list (name, type, description, required), i.e. the scripting reference.
- **Plain hover** without Ctrl: only icon-only buttons and elided labels get a small tooltip (label + shortcut) after 0.5 s. All other text tooltips that exist today (e.g. custom-brush description, Voxel remesh) move into the Ctrl help.
- **Look** (tooltip window, width 340, padding 10, rounding 6, bg `#18181a` at 97%, 1 px border accent at 60% alpha):
  ```
  +------------------------------------------------------+
  | [icon] Subdivide                        [Ctrl]+[D]   |   <- SemiBold 15, shortcut keycaps right
  |------------------------------------------------------|
  | Smoothly subdivide the mesh (Loop subdivision).      |   <- Regular 14, #c8c8c8, wrapped
  | Each level multiplies the triangle count by 4.       |
  |                                                      |
  | Tool > Geometry                       subdivide      |   <- 12 px dim path left, command name right (mono)
  +------------------------------------------------------+
  ```
  Keycaps: rounded rect `#2c2c30`, 1 px `#4a4a50`, text accent. Placement: cursor + (18, 18), flipped to stay inside the viewport.
- **Text sources**: Action description (§8). For command-backed actions this is `Command::description`, so it stays in sync with the AI tool schema and CLI help. Non-action widgets (sliders, checkboxes) use `ui::HelpText(...)`. **Every** interactive item in shelves, palettes and pickers must have help; a UI test checks a sample.

### 7.2 Command palette (F3, also Ctrl+Shift+P; the title-strip search field)
- A modal-less pop-up at the top centre of the canvas, width 560, max 12 rows.
- Fuzzy search over all Actions: label (weight 3), palette path (2), id / command name (2), description words (1). Brushes and materials are actions too ("Brush: Clay", "Material: Chrome").
- Rows: icon, label, dim palette path, right-aligned shortcut. Disabled actions are shown dimmed, with the reason in Ctrl help.
- Empty query: the 8 most recently used actions (persisted).
- Keys: Up/Down, Enter runs, Esc closes.
  - Shift+Enter on a command with parameters opens the console with `name ` pre-typed.
  - Commands with **required** parameters always go to the console instead of running.

### 7.3 Console (the "ZScript window"; `` ` `` toggles it, Script palette)
- Bottom strip above the bottom shelf, h 160 (drag the top edge, 100-400). Font: ImGui's built-in ProggyClean via `AddFontDefault()` (MIT, ships inside ImGui, no new file) at 13 x scale.
- **Log**: every `App::runCommand` (UI, keys, palette, AI agent, MCP) echoes as a script line followed by its result:
  ```
  > subdivide
    Subdivided: 40,962 vertices, 81,920 triangles
  > remesh resolution=200
  [AI] > brush_create name="Dragon Scales" ...
  ! open path="missing.obj"    -> Could not open ...      (red)
  ```
  Status messages that aren't commands appear dim.
- **Input**: one line, parsed with `parseScript` (`core/Script.h`), so `;` and multiple statements work.
  - Tab completes command names, then `param=` names; a second Tab lists the candidates.
  - Up/Down walks history (last 200, persisted to `<prefDir>/console_history.txt`).
  - Typing `help` or `help <cmd>` prints descriptions; `?` = help.
- **Buttons**: Run script... (a text file of statements), Copy session as script (puts the echoed commands on the clipboard, which makes it a macro recorder), Clear.
- This needs `std::string formatStatement(const std::string& name, const Args&)` in `core/Script` (quotes strings containing spaces, `true`/`false`, `%g` numbers), the inverse of `parseScript`. Add a round-trip unit test.

---

## 8. Actions and shortcuts (the glue)

One registry drives shelves, palettes, help, the command palette, keys and customization.

```cpp
// src/app/Actions.h
struct Action {
    std::string id;           // "geometry.subdivide", "ui.lightbox", "brush.clay", "view.wireframe"
    std::string label;        // "Subdivide"
    const char* icon = nullptr;   // ICON_LC_* or nullptr
    std::string description;  // shown in Ctrl help; defaults to Command::description
    std::string palette;      // "Tool > Geometry" (help footer, command palette)
    std::string shortcut;     // default chord, e.g. "Ctrl+D", "Shift+F", ","; "" = none
    std::string command;      // CommandRegistry name when the action runs a command
    std::function<void()> run;
    std::function<bool()> checked;  // toggles; null = not a toggle
    std::function<bool()> enabled;  // null = always
};
class ActionRegistry {
public:
    void add(Action a);                          // replaces by id
    const Action* find(std::string_view id) const;
    const std::vector<Action>& all() const;
    bool handleKey(SDL_Keycode, SDL_Keymod);     // true if consumed (respects io.WantCaptureKeyboard / WantTextInput)
    std::string shortcutText(std::string_view id) const; // effective chord, after user overrides
    void setShortcut(std::string_view id, std::string chord); // user override ("" clears)
    Json overridesToJson() const; void overridesFromJson(const Json&);
    void touch(std::string_view id);             // MRU for the command palette
};
// Every CommandRegistry command without required params is auto-registered as "cmd.<name>".
```

UI helpers that every panel uses (so help and customization come for free):

```cpp
// src/app/Ui.h
namespace sf::ui {
enum class Look { Label, Icon, IconLabel, Tile };
bool ActionButton(App&, const char* actionId, ImVec2 size = {}, Look = Look::Label); // runs the action on click;
                                                         // toggles draw accent when checked(); Ctrl help; Ctrl+Alt drag source in customize mode
void HelpText(const char* title, const char* shortcut, const char* description, const char* path = nullptr); // for the last item
void HelpAction(const char* actionId);                   // for the last item
bool SliderInside(const char* id, float* v, float lo, float hi, const char* fmt, ImGuiSliderFlags = 0); // ZBrush-style label-in-frame
}
```

**Default shortcut map.** Existing bindings are kept, ZBrush ones are added, and nothing existing changes meaning:

| Key | Action | | Key | Action |
|---|---|---|---|---|
| `,` | LightBox | | `B` | Brush picker |
| `Tab` | Hide/show shelves (+ trays) | | `M` | Material picker |
| `` ` `` | Console | | `S` / `I` | Size / Strength at cursor |
| `F3`, `Ctrl+Shift+P` | Command palette | | `Shift+F` | Wireframe (`W` until 0.4; since 0.5 `Q W E R` are Draw and the Gizmo 3D's Move, Scale, Rotate) |
| `Ctrl+Shift+I` | Store Config | | `Ctrl+L` | Load UI... |
| `F1` | Controls | | `1-9, 0, [ ] X F P C, Ctrl+D/Z/Y/O/S/N/E/U/V` | unchanged |

`onKey`'s switch becomes `actions_.handleKey(...)` with these defaults. The F1 Controls window is generated from the registry, grouped by palette. User overrides are stored with Store Config.

Optional (P2): ZBrush-style shortcut assignment. Ctrl+Alt+click any ActionButton, then press a key ("Press a key for 'Clay'... Esc cancels"). Conflicts reassign, as in ZBrush.

---

## 9. Customize, Store Config, Restore

### 9.1 Layout data (the shared contract)

```cpp
// src/app/UiLayout.h
struct UiLayout {
    int version = 1;
    bool shelvesVisible = true, paletteBarVisible = true, leftHanded = false;
    bool leftTrayOpen = false, rightTrayOpen = true;
    float leftTrayWidth = 300, rightTrayWidth = 300;          // logical px
    std::vector<std::string> leftTray, rightTray;              // palette ids, top to bottom
    std::vector<std::string> openSubpalettes;                  // "tool.subtool", ...
    std::vector<std::string> topShelf, leftShelf, rightShelf, bottomShelf; // item ids: action ids, "widget.*", "sep", "row" (top shelf line break)
    bool consoleOpen = false; float consoleHeight = 160;
    bool lightboxOpen = false; int lightboxRows = 1; std::string lightboxTab = "project"; bool lightboxBottom = false;
    bool customize = false;                                    // runtime only, never stored
};
UiLayout standardLayout();                // the built-in default described in §3
Json toJson(const UiLayout&); bool fromJson(const Json&, UiLayout&, std::string& err); // unknown ids are dropped, not fatal
```

### 9.2 Behaviour
- **Preferences > Config > Enable Customize** sets `customize`. While it's on, the shelves draw a dashed 1 px outline and the status line says "Customize: Ctrl+Alt+drag to move or remove items".
- **Ctrl+Alt+drag** any ActionButton or shelf widget. It works from palettes and pickers too, which is how you *add* an item to a shelf.
  - ImGui drag source, payload `"SF_UIITEM"` = `{ itemId, fromShelf, fromIndex }`.
  - Shelves draw insertion targets between items (2 px accent line).
  - **Dropping anywhere that isn't a shelf (e.g. the canvas) removes the item** if it came from a shelf (ZBrush semantics).
  - Palettes can't be dropped on shelves, only into trays (`"SF_PALETTE"` payload from the palette header icon).
- **Store Config (Ctrl+Shift+I)** writes `<prefDir>/ui.json` = `{ layout, shortcuts: overrides, prefs: {popupInfo, uiScale, oneSubpalette, autoCollapse, lightbox...} }`. Status: "UI configuration stored".
- **Restore Standard UI**: `layout_ = standardLayout()` and shortcut overrides cleared. Not persisted until Store Config, as in ZBrush.
- **Restore Custom UI**: reload `ui.json`.
- **Save UI... / Load UI... (Ctrl+L)**: the same JSON at a chosen path (for sharing layouts).
- At startup `ui.json` is loaded if it's there, otherwise the standard layout. `--ui-test`, `--render` and recording **always** use the standard layout (deterministic tests and videos).
- ZBrush never auto-saves the layout. To avoid surprising non-ZBrush users, the first time the layout changes in a session (tray moved, shelf edited) the status line hints once: "Layout changed - Ctrl+Shift+I to keep it".

### 9.3 Presets (title-strip `[1][2][3]`)
1. **Standard** (§2).
2. **Sculpt**: trays closed, shelves on, LightBox off. Maximum canvas.
3. **Wide**: left tray open with Brush + Stroke + Alpha, right tray Tool + Material.

Clicking a preset applies it (layout only). Store Config makes it stick.

---

## 10. Theme

Keep the existing orange accent. Make it denser and greyer, like ZBrush. Put this in `applyStyle()` (to be moved to `src/app/Theme.cpp`) as named constants:

| Token | Value | Use |
|---|---|---|
| `shellBg` | `#1e1e20` | shelves, trays, title strip, area around the canvas |
| `panelBg` | `#252528` | palette drop-downs, tray contents, pop-ups (`WindowBg`, `PopupBg` at 98%) |
| `header` | `#2e2e32` / hover `#3a3a3f` | palette and sub-palette headers |
| `frame` | `#2b2b2f` / hover `#36363b` | slider and input frames |
| `button` | `#38383c` / hover `#47474c` / pressed `#e8812a` | buttons |
| `accent` | `#f0882e` (hover `#ff9a45`) | toggled-on buttons (bg accent, text `#ffffff`), slider grab, check marks, selected outline, LightBox active tab |
| `text` / `textDim` | `#d8d8d8` / `#8c8c90` | |
| `border` | `#121214` | 1 px between regions |
| `danger` | `#e5604a` | errors |
| canvas | the existing gradient; 1 px `#0e0e10` frame | |

Style:
- Rounding: `WindowRounding 0` (shells), `PopupRounding 6`, `FrameRounding 3`, `GrabRounding 2`.
- Spacing: `FramePadding (6,3)`, `ItemSpacing (4,4)`, `ItemInnerSpacing (4,4)`.
- `ScrollbarSize 10`, `GrabMinSize 6`.
- Font: Inter Regular 15 x scale (as now). Shelf labels and tile captions 12 px (a second Inter size), help titles Inter SemiBold 15, console ProggyClean 13.
- Trays: sub-palette headers are quiet (no fill until hovered or open, a small chevron, a hairline under each); the accent is not used on them. The active row of a list (SubTools) is the accent at ~15% with a 2 px accent bar on its left, not a solid fill. Helper text is 12 px `textDim`.
- The left tray's colour field is drawn by hand (`App::drawColorWidget`, Pickers.cpp): a rounded saturation/value square with a ring marker, an 8 px hue strip with a thin pill notch, and the main/second swatches with a 1 px edge.

---

## 11. Icons: recommendation = **Lucide** icon font merged into ImGui

- **Licence**: Lucide is **ISC** ("Copyright (c) Lucide Icons and Contributors"). The icons derived from Feather are **MIT** ("Copyright (c) 2013-present Cole Bemis"). Both notices must ship with copies, so add both texts to `THIRD_PARTY_NOTICES.md` and the About dialog ("Lucide icons (ISC/MIT)"). Both are GPL-3.0-compatible. [R15]
- **Why Lucide over Material Symbols (Apache-2.0)**:
  - Lucide's codepoints are all in the BMP. `IconsLucide.h` defines `ICON_MIN_LC 0xe038` and `ICON_MAX_16_LC == ICON_MAX_LC 0xe6fd`, so it works with ImGui's default 16-bit `ImWchar`.
  - The TTF is small (a few hundred KB).
  - Thin outline glyphs suit a dense, ZBrush-like UI.
  - Material Symbols is a 10 MB+ variable font. Its full range goes past 16-bit (IconFontCppHeaders has a separate `ICON_MAX_16_MS`), and Apache-2.0 adds NOTICE handling. [R16][R17]
- **Files** (vendored, pinned to one `lucide-static` version; codepoints aren't stable across versions, so header and TTF must come from the same version):
  - `assets/fonts/lucide.ttf` from `https://unpkg.com/lucide-static@<pinned>/font/lucide.ttf`
  - `third_party/IconFontCppHeaders/IconsLucide.h` (zlib licence), regenerated by that repo's `GenerateIconFontCppHeaders.py` from the same version's `lucide.css`
- **Embedding**: add `lucide.ttf` to the existing `embed_files(... EmbeddedFonts.cpp sf::assets ...)` in CMake. After adding Inter Regular in `App::init`:
  ```cpp
  static const ImWchar kIconRanges[] = {ICON_MIN_LC, ICON_MAX_16_LC, 0};
  ImFontConfig ic; ic.MergeMode = true; ic.PixelSnapH = true; ic.FontDataOwnedByAtlas = false;
  ic.GlyphMinAdvanceX = 15.0f * scale; ic.GlyphOffset = ImVec2(0, 2.0f * scale);
  io.Fonts->AddFontFromMemoryTTF((void*)assets::fontLucide, int(assets::fontLucide_size), 15.0f * scale, &ic, kIconRanges);
  ```
  Repeat the merge for the 12 px shelf font. In the right shelf, use a separate non-merged 20 px Lucide font for the large icons.
  - With ImGui >= 1.92 (dynamic fonts), the glyph ranges can be dropped. Not relevant on 1.91.9.
- **Glyphs to use** (check each name against the pinned header): `brush, paintbrush, spray-can, move-3d, rotate-3d, scale-3d, scan (frame), hand (pan), zoom-in, grid-3x3 (wireframe), eye, eye-off, boxes (subtools), layers, droplet (mask), shapes (polygroups), palette (color), circle-dashed (alpha), sun (material), image, folder, folder-open, save, undo-2, redo-2, search, square-terminal, circle-help, settings, sparkles (AI), wand-sparkles, package (export), gamepad-2 (game ready), panel-left, panel-right, chevron-*, x`.
- Sculpt-specific visuals (brushes, alphas, materials, models) use **rendered thumbnails** (§6), not glyphs. No icon set has good sculpting-brush icons, and ZBrush itself uses rendered previews.

---

## 12. ImGui constraints (non-docking 1.91.9)

Works without the docking branch:
- Fixed shells as windows with `NoDecoration|NoMove|NoResize|NoSavedSettings|NoBringToFrontOnFocus|NoScrollWithMouse` (trays scroll inside a child), placed every frame.
- Tray and console resize via `InvisibleButton` splitters.
- Palette drop-downs, pickers and the command palette as pop-ups.
- Customize drag and drop via `BeginDragDropSource/Target`.

Each shell window needs `NoFocusOnAppearing` and must not steal keyboard focus, so `onKey` still gets keys. Draw order: shells first, then LightBox, then floating windows, then pop-ups and help.

**Needs the docking branch (flagged, not planned):** dragging panels out into separate OS windows (multi-viewport); arbitrary tabbed or split docking of palettes; ImGui-native persistence of dock layouts. ZBrush has none of these either: elements "cannot be undocked outside the window" [R1].

---

## 13. Implementation: phase 0 + three parallel workstreams

### Phase 0: contracts (one small commit, ~half a day, before splitting)
Create these with real declarations and **stub** implementations, so all three streams compile and merge independently:
- `src/app/Actions.h/.cpp`: `Action`, `ActionRegistry` (stub `handleKey` returns false). App member `ActionRegistry actions_`.
- `src/app/Ui.h/.cpp`: `ui::ActionButton` (stub = `ImGui::Button(label)` + run), `ui::HelpText`/`HelpAction` (stubs no-op), `ui::SliderInside`.
- `src/app/UiLayout.h/.cpp`: struct + `standardLayout()` + JSON. App member `UiLayout layout_`.
- `src/app/LightBox.h`: `class LightBox { void draw(App&, CanvasRect); bool open; void toggle(); }`. `src/app/Thumbnails.h`: `class ThumbnailCache { ImTextureID brush(int type); ImTextureID recipe(const BrushRecipe&); ImTextureID alpha(const BrushAlpha&); ImTextureID material(int matcap); ImTextureID file(const std::string& path); void pump(Renderer&); }`. All stubs return 0 / draw nothing.
- `src/app/Pickers.h`: `enum class Picker { Brush, Stroke, Alpha, Material }; void openPicker(Picker, ImVec2 anchor); void drawPickers(App&);`
- Lucide font + header embedded and merged (§11), the `ICON_LC_*` macros available.
- `App.h`: three clearly fenced member sections, `// ---- Shell (A) ----`, `// ---- LightBox (B) ----`, `// ---- Help/Console/Config (C) ----`. Each stream adds members only inside its own section.

### Workstream A: shell, layout, palettes, theme
**Owns**: `src/app/Shell.cpp` (new: title strip, palette bar, shelves, trays, splitters, layout math, `canvasRect()`), `src/app/Palettes.cpp` (new: palette registry, `drawPalette_*` for all 16 palettes, Tool sub-palettes), `src/app/Theme.cpp` (moved `applyStyle` + tokens), the existing `Panel*.cpp` (converted to `draw*Contents()` without `Begin/End`), `App.cpp` `drawUi`/`drawMenuBar` (removed)/`drawToolPanel` (removed)/panel functions, the canvas-rect changes in `run`/`handleEvent`/`pick`/brush cursor, and `src/render/Renderer.*` (viewport rect param).
**Delivers**: §2, §3 (except the picker internals), §9.1 runtime use (reads and edits `layout_`, Ctrl+Alt drag/drop targets on shelves and trays), §9.3 presets, §10. Registers the Actions for everything it draws (brush.*, view.*, ui.toggle_shelves, ui.palette.<id>...). Uses `ui::ActionButton` everywhere.
**Tests**: rewrite `tests/ui/*.sfui` steps that clicked View-menu items or old window titles. Item labels stay the same wherever possible: "Brush Lab/..." stays a floating window with the same name, and SubTools buttons keep their labels inside Tool > SubTool. Add `tests/ui/shell_layout.sfui` (shots at 1600x960 and 1920x1080, Tab hides shelves, tray divider toggle, palette into tray).

### Workstream B: LightBox, thumbnails, pickers
**Owns**: `src/app/LightBox.cpp`, `src/app/Thumbnails.cpp`, `src/app/Pickers.cpp`, `src/app/AssetLibrary.h/.cpp` (folder roots, worker-thread scanning), `src/core/Project.cpp` (+ `thumbnail.png` in `.sfproj`, read helper `loadProjectThumbnail(path, Image&)`), and maybe a `Renderer::renderMeshThumbnail` (coordinate the signature with A, who owns `Renderer.*`; B adds the new method in a separate block).
**Delivers**: §4 (brush picker with letter badges, stroke/alpha/material pickers, S/I sliders, material hover preview), §5, §6, QuickSave target folder. Registers `ui.lightbox` (`,`), `ui.picker.brush` (B), `ui.picker.material` (M), `ui.size_popup` (S), `ui.strength_popup` (I), `file.quicksave`.
**Interface used by A**: the left-shelf tiles call `thumbs.brush(...)` etc. and `openPicker(Picker::X, tileRectMax)`. The bottom shelf uses `thumbs.brush/recipe`. The Brush/Alpha/Material palettes reuse `drawBrushGrid(App&, GridMode)` exported from `Pickers.h`.
**Tests**: `tests/ui/lightbox.sfui` (`key ,` -> expect "LightBox", tab switch, double-click the "Sphere" tile -> triangles changed, Shift+double-click -> subtools == 2), the brush picker (`key b`, `key s`, `key t` -> Standard active), unit tests for the cache key and the project-thumbnail round-trip.

### Workstream C: help, command palette, console, config, keys
**Owns**: `src/app/Actions.cpp` (real `handleKey`, chord parsing, overrides, MRU, auto-registering commands), `src/app/Ui.cpp` (real Ctrl-hover help rendering, customize-mode drag sources inside `ActionButton`), `src/app/CommandPalette.cpp`, `src/app/Console.cpp`, `src/app/Config.cpp` (Store/Restore/Load/Save UI, `ui.json`, preferences values), `src/core/Script.*` (`formatStatement` + test), `App::onKey` (replaced by `actions_.handleKey`), the one-line echo hook in `App::runCommand`, and the generated F1 Controls window. Also `src/app/UiTest.cpp` key aliases (`comma`, `backtick`, `tab`, `f3`), and a test step `expect_help TEXT` (the Ctrl-help box shown this frame contains TEXT).
**Delivers**: §7, §8, §9.2. Registers `ui.command_palette`, `ui.console`, `config.store` (Ctrl+Shift+I), `config.restore_standard`, `config.restore_custom`, `config.load` (Ctrl+L), `config.save`, `config.customize`. Provides `drawPalette_Preferences_Config` content for A's Preferences palette (A calls `config::drawConfigSection(App&)` and `config::drawInterfaceSection(App&)` from `Config.h`).
**Tests**: `tests/ui/help_console.sfui` (`hold ctrl` + `hover Subdivide` -> `expect_help "Loop subdivision"`; `key f3`, `type "Command palette" subdiv`, `key enter` -> triangles x4; `` key ` `` + type `remesh resolution=64` + enter -> `expect_text "> remesh"`; Store Config writes `ui.json` in the isolated settings dir; Restore Standard UI restores tray state).

### Overlap and merge order
- The only shared files are `App.h` (fenced sections), `App.cpp` (A: drawUi and panels; C: `onKey`, a one-line `runCommand` hook; B: none), `Renderer.*` (A owns, B adds one method) and `CMakeLists.txt` (each stream appends its own sources to one list; trivial conflicts).
- Merge order: **Phase 0 -> C -> B -> A**. A rewires the most call sites, so it merges last against real helpers. A can still develop fully against the stubs.
- Each stream adds the new `.cpp` files to `CMakeLists.txt`, runs the unit tests plus `./build/sculptforge --ui-test tests/ui/<file>.sfui`, and attaches 1600x960 and 1920x1080 shots to its PR.

### Acceptance checklist (whole feature)
- [ ] The default UI at 1600x960 matches §2.2. The canvas is clear of floating windows.
- [ ] Every former menu item and panel control is reachable from a palette. The command palette finds every command.
- [ ] Ctrl+hover shows description + shortcut on every shelf, palette and picker item.
- [ ] `,` / `B` / `M` / `Tab` / `` ` `` / `F3` / `Ctrl+Shift+I` work. Existing shortcuts are unchanged.
- [ ] Customize: Ctrl+Alt drag adds, moves and removes shelf items; Store Config persists across restarts; Restore Standard UI resets.
- [ ] LightBox shows thumbnails for every tab within ~1 s of opening (cached afterwards). Double-click loads; Shift+double-click appends a SubTool.
- [ ] Tests, `--render` and demo recordings are unchanged (standard layout, full-window canvas when `hideUi_`).
- [ ] Lucide ISC + MIT notices are in THIRD_PARTY_NOTICES.md and About.

---

## References
- [R1] ZBrush User Interface (reference guide): https://help.maxon.net/zbr/en-us/Content/html/reference-guide/interface-overview/interface-overview.html
- [R2] Finding Your Way Around: https://help.maxon.net/zbr/en-us/Content/html/getting-started/finding-your-way-around/finding-your-way-around.html
- [R3] Interface Operation: https://help.maxon.net/zbr/en-us/Content/html/user-guide/customizing-zbrush/interface-operation/interface-operation.html
- [R4] Areas for Customization / Custom Interface: https://help.maxon.net/zbr/en-us/Content/html/user-guide/customizing-zbrush/interface-layout/custom-interface/custom-interface.html
- [R5] ZBrushCentral, brush pop-up letter selection: https://www.zbrushcentral.com/t/does-nobody-use-shortcuts-for-brushes/397885 and https://www.zbrushcentral.com/t/popup-hotkey/320306
- [R6] ZBrush Shortcuts by Palette: https://help.maxon.net/zbr/en-us/Content/html/user-guide/keyboard-shortcuts/shortcuts-by-palette/shortcuts-by-palette.html (PDF: https://help.maxon.net/zbr/en-us/Content/Resources/Uploads/ZBrush-Shortcuts.pdf)
- [R7] LightBox: https://help.maxon.net/zbr/en-us/Content/html/getting-started/basic-concepts/lightbox/lightbox.html
- [R8] ZBrushCentral, tooltip (Quick Info) opacity: https://www.zbrushcentral.com/t/how-can-i-change-tooltip-transparency-answered/310810
- [R9] Preferences > Quick Info: https://help.maxon.net/zbr/en-us/Content/html/reference-guide/preferences/quick-info/quick-info.html
- [R10] Preferences > Config: https://help.maxon.net/zbr/en-us/Content/html/reference-guide/preferences/config/config.html
- [R11] Blender manual, Brush introduction / asset shelf: https://docs.blender.org/manual/en/dev/sculpt_paint/brush/introduction.html
- [R12] Blender 4.3 sculpting features: https://www.blendernation.com/2024/11/09/blender-4-3-new-sculpting-features/
- [R13] Nomad Sculpt manual, Getting started / Interface: https://nomadsculpt.com/manual/gettingstarted , https://nomadsculpt.com/manual/interface
- [R14] 3DCoat documentation (rooms, panels, activity bar): https://3dcoat.com/documentation/?p=2248
- [R15] Lucide licence: https://lucide.dev/license
- [R16] IconFontCppHeaders (zlib; IconsLucide.h, ImGui merge example): https://github.com/juliettef/IconFontCppHeaders
- [R17] Material Symbols guide (Apache-2.0): https://developers.google.com/fonts/docs/material_symbols
- Dear ImGui font merging: https://github.com/ocornut/imgui/blob/v1.91.9/docs/FONTS.md
