# The interface

SculptForge's window is laid out like ZBrush's: a canvas in the middle, shelves of the most-used controls
around it, a bar of alphabetical palettes that hold every function, and trays at the sides where palettes can
stay open. This chapter walks through each region, the pop-up pickers, the Library, the built-in help, and
how to rearrange the interface and keep your changes.

![The SculptForge window with its main regions labelled](images/interface-overview.jpg)

## The regions at a glance

| Region | Where | What it holds |
|---|---|---|
| Title strip | Top edge | Document name, scene statistics, status messages, Snapshot, layout presets, Menus, Search |
| Palette bar | Under the title strip | The 16 palettes, which hold every function |
| Top shelf | Above the canvas | Library, Sculpt/Paint, Add/Sub, Rgb, Draw Size, Intensity, symmetry, pressure |
| Left shelf | Left of the canvas | Brush, Stroke, Alpha and Material tiles, the colour picker |
| Canvas | Centre | The 3D view; strokes and navigation start only here |
| Right shelf | Right of the canvas | Undo and Redo, Frame, Move, Zoom and Rotate, display toggles, Solo |
| Bottom shelf | Below the canvas | Your quick brushes |
| Trays | Far left and far right | Palettes you keep open; the right tray holds the Tool palette by default |

Press <kbd>Tab</kbd> to hide the shelves and trays for a bigger canvas, and again to bring them back. The
palette bar stays, so every function is still one click away.

## The title strip

From left to right:

- **SculptForge** and the **document name** ("Untitled" until you save). A `*` after the name means there are
  unsaved changes; click the name to save (<kbd>Ctrl</kbd>+<kbd>S</kbd>).
- **Scene statistics**: vertices and triangles across all Parts, the active Part and its position
  ("Part Body (2/5)"), the active sculpt layer ("Layer Base" when you sculpt the base mesh), the number of
  Gaussian splats when a capture is loaded, and the frame rate.
- **The status message**: the result of the last command or action. Errors show in red for a few seconds.
  Point at a long message to read all of it.
- **Snapshot**: saves a timestamped copy of the whole scene to your library without touching your project
  file (see [Projects, autosave and snapshots](projects.md)).
- **1**, **2**, **3**: the layout presets (see [Layout presets](#layout-presets)).
- **Menus**: shows or hides the palette bar.
- **Search** (<kbd>F3</kbd>): opens the [command palette](#the-command-palette).
- **Minimize** and **Close** at the far right. Close asks to save changes first, as
  <kbd>Ctrl</kbd>+<kbd>Q</kbd> does.

## Palettes

Every function in SculptForge lives in one of 16 palettes. Click a palette's name in the palette bar and it
drops down under the name. Click another name to switch, or press <kbd>Esc</kbd> or click outside to close it.

![The Stroke palette open as a drop-down from the palette bar](images/palette-dropdown.jpg)

The palette bar lists **File** and **Edit** first and **Help** last, where other desktop applications put
them, and the rest in alphabetical order. If you prefer ZBrush's strict A to Z order, turn on **Preferences >
Interface > Palettes strictly A-Z (ZBrush order)**.

| Palette | What's in it |
|---|---|
| **File** | New Sphere, New Plane, Open, Open Recent, Save, Save As, autosave interval, Import and every export (OBJ, GLB, LOD chain, USD/USDZ, FBX for Unreal, Send to Unreal, splats), ZBrush Bridge, Quit |
| **Edit** | Undo and Redo (with the name of the step), Paste brush |
| **Alpha** | The brush alphas: Round, Square, Noise and twelve patterns (Pores, Cracks, Scales...), images from your library, Load image, Invert loaded image |
| **Brush** | AI Brush (Brush Lab), Community brushes, Paste brush, your custom brushes, Front faces only, Invert (Alt), the recipe editor for a custom brush, and every brush as a thumbnail grid with a filter |
| **Color** | The Paint brush, RGB while sculpting, the paint colour and swatches, Fill, Fill masked, Cavity, Clear, Show vertex paint |
| **Draw** | Draw Size, Intensity, RGB Intensity, Falloff, Sculpt/Paint, Add/Sub, Rgb, Front, and the tablet options (Pressure > strength, Pressure > size) with a pen readout |
| **Generate** | Generate 3D (image or text to 3D), Relief, Inflate and Stamp from an image, and the AI brush provider settings |
| **Material** | The Lit Sphere materials (Soft Clay, the default, first), your own (**Load Lit Sphere...**) and Studio, Base colour, Smooth / Flat shading for the scene and the active Part |
| **Preferences** | Config, Interface, Input, Hotkeys, Display, Library, Tablet, Performance, Updates and Autosave sub-palettes (see [Customizing the interface](#customizing-the-interface)) |
| **Render** | Wireframe, Show Mesh, Show Vertex Paint, Show Mask, Show Face Groups, Show Splats, Background, Frame, Fullscreen |
| **Script** | Console, Run script file, Copy session as script, Clear console |
| **Splats** | Gaussian splat tools: show, flip, turn, fit, clean floaters, Make sculptable, sculpt to splats, save, clear |
| **Stroke** | The stroke types (Dots, Drag Stamp, Spray, Lazy Stroke) and their settings |
| **Tool** | The active Part with Open, Save and Import, and the Part, Layers, Paint Layers, Geometry, Remesh, Deformation, Masking, Face Groups, Visibility, Export and Display sub-palettes |
| **Transform** | Draw, Move, Scale and Rotate (the [Gizmo](gizmo.md) with its pivot, axes, options and numeric entry), Symmetry X, Y and Z, Frame, Solo, Wire |
| **Help** | Controls, User Guide, What's New, Command palette, Website & documentation, Report a problem, Support SculptForge..., About SculptForge |

### Sub-palettes

Large palettes are divided into **sub-palettes**: headers you click to open or close. The Tool palette is the
main example (Part, Layers, Geometry and so on). By default opening one sub-palette closes the others in
the same palette, which keeps the palette short. **Shift**+click a header to open it without closing the
others, or turn off **Preferences > Interface > One sub-palette open**.

> **ZBrush:** as in ZBrush, Parts (ZBrush's SubTool), Layers, Masking, Face Groups (Polygroups) and Visibility are sub-palettes of the Tool
> palette rather than palettes of their own. SculptForge's Remesh sub-palette is where ZBrush's DynaMesh would be.

## Trays

The trays are columns at the far left and far right of the window where palettes stay open while you work. The
right tray is open by default and holds the **Tool** palette; the left tray starts closed and empty.

- **Put a palette in a tray**: open it from the palette bar and click the tray icon at the left of its header.
  It goes into the right tray; **Shift**+click the icon to put it in the left tray. You can also drag the icon
  onto a tray.
- **Take it out again**: click the tray icon in its header inside the tray. The palette returns to the bar.
- **Collapse it in place**: click its title (or the arrow at the right of the header).
- **Find it**: a palette that is in a tray shows its name in orange in the palette bar. Clicking the name
  scrolls the tray to the palette and flashes its header.
- **Open or close a tray**: click its divider, the thin strip with a grip between the tray and the shelves.
  **Drag** the divider to make the tray wider or narrower (240 to 480 pixels).

![The Stroke palette kept in the right tray above the Tool palette](images/palette-tray.jpg)

When a tray holds several palettes, **Preferences > Interface > Auto collapse left tray** (on by default) and
**Auto collapse right tray** (off by default) collapse the other palettes when the one you open would not fit.

When you open a Gaussian splat capture, the Splats palette moves into the right tray by itself.

> **Note:** on a window narrower than 1280 pixels (at 100% UI scale) the right tray folds away and palettes
> open as drop-downs only. The left-shelf tiles get smaller and the bottom shelf scrolls sideways with the
> mouse wheel.

## The shelves

Shelves surround the canvas and hold the controls you use all the time. Point at anything with <kbd>Ctrl</kbd>
held to see what it does. Right-click an item for **Remove from shelf**; see [Customizing the
interface](#customizing-the-interface) to add items.

### Top shelf

First row:

- **Library** (<kbd>,</kbd>): shows or hides the [Library](#the-library).
- **Sculpt** / **Paint** (<kbd>P</kbd>): brushes change the shape, or paint colour without sculpting.
- **Add** / **Sub**: brushes add to or subtract from the surface. Holding <kbd>Alt</kbd> flips it for one
  stroke.
- **Rgb**: sculpt brushes paint the colour at the same time.
- **Draw Size**: the brush radius in screen pixels (2 to 800).
- **Intensity**: how strongly each dab changes the surface (1 to 100). While <kbd>Shift</kbd> is held it shows
  Smooth's own value.

Second row:

- **AI Brush**: opens Brush Lab (see [Brush Lab and sharing brushes](brush-lab.md)).
- **Front**: front faces only, so brushes leave the far side of thin parts alone.
- **X**, **Y**, **Z**: symmetry across each axis (<kbd>X</kbd> toggles X).
- **Pressure > Strength** and **Pressure > Size**: what pen pressure controls.
- **ActiveTris / TotalTris** at the right: the triangles you can see and sculpt (visible Parts, hidden
  parts left out) and the total in the scene.

Draw Size and Intensity are sliders with the label inside, as in ZBrush: drag along them, or
<kbd>Ctrl</kbd>+click to type a value.

### Left shelf

Four large tiles, each showing the current choice. Click one to open its [pop-up picker](#pop-up-pickers):

- **Brush**: the active brush, with a rendered preview.
- **Stroke**: Dots, Drag Stamp, Spray or Lazy Stroke.
- **Alpha**: the brush's stamp shape.
- **Material**: how the model is shaded in the view.

Under them is the **colour picker** for vertex paint (a saturation/value square and a hue strip), two swatches
(the main and second colour; click either to swap them, like ZBrush's Alternate) and **Fill**, which fills the
unmasked area with the main colour.

### Right shelf

- **Undo** and **Redo** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>): greyed out when
  there is nothing to undo or redo. A layout you customised before they were added gets them at the top.
- **Frame** (<kbd>F</kbd>): fits the model in the view.
- **Move**, **Zoom**, **Rotate**: drag on these buttons to pan, zoom and orbit the view, as in ZBrush.
- **Wire** (<kbd>Shift</kbd>+<kbd>F</kbd>): shows the mesh's polygons: quads as quads, triangles as triangles.
- **Groups**: colours each face group.
- **Mask** (<kbd>Ctrl</kbd>+<kbd>H</kbd>): shows or hides the mask's darkening (on by default). Hiding it keeps
  the mask, which still protects the surface.
- **Paint**: shows vertex paint colours (on by default).
- **Solo**: shows only the active Part (available with two or more Parts).
- **Splats**: shows or hides a Gaussian splat capture (only shown while one is loaded).

Options that are on are a lighter grey with a thin orange line; orange fill marks only the current tool or mode. See [Navigating the view](navigation.md) for the camera.

### Bottom shelf

A row of quick brushes: Standard, Clay, Clay Block, Sharp Crease, Move, Smooth, Inflate, Pinch, Flatten,
Crease, Trim, Tendril, Layer, Nudge and Paint. The small digit in a tile's corner is its shortcut
(<kbd>1</kbd> to <kbd>9</kbd> and <kbd>0</kbd> select the first ten built-in brushes, <kbd>P</kbd> is Paint).
The active brush has an orange outline.

![The bottom shelf of quick brushes](images/bottom-shelf.jpg)

- **Add a brush**: click **+** at the end of the shelf and pick any brush, built-in or one of your own. Or
  right-click a brush in the brush picker and choose **Add to bottom shelf**. Custom brushes show a small
  sparkle in the corner.
- **Remove a brush**: right-click its tile and choose **Remove from shelf**.
- **Bigger or smaller tiles**: right-click any brush on the shelf and choose **Tile size**: Small, Medium (the
  default) or Large. The same setting is in **Preferences > Interface > Shelf tiles**. Small leaves the most
  room for the model on a laptop screen.

## Pop-up pickers

Pickers are pop-up grids of thumbnails. They open beside the left-shelf tile you click, or under the mouse
when you use a key. They close when you pick something, press <kbd>Esc</kbd> or click elsewhere. Pointing at a
tile for a moment shows **Quick Info**: a large preview and a description.

### Brush picker

Press <kbd>B</kbd> (or click the Brush tile). The picker shows a **Recent** row, then **Built-in**, **Custom**
(your brushes and Brush Lab creations) and **Library** (`.sfbrush` files in your library that aren't loaded
yet; picking one loads it).

![The brush picker after pressing B and typing S](images/brush-picker.jpg)

Choose a brush with the keyboard the ZBrush way:

1. Press <kbd>B</kbd>.
2. Type the first letter of the brush. Only the brushes starting with that letter stay, and each shows an
   orange letter: the next key that tells it apart.
3. Press that orange letter to pick the brush. For example <kbd>B</kbd> <kbd>S</kbd> <kbd>T</kbd> picks
   Standard.

<kbd>Backspace</kbd> takes back a letter and <kbd>Enter</kbd> picks the first match. If no name starts with
what you typed, the picker falls back to names that contain it. The letter chips along the top (with a count
for each letter) filter by a click as well.

Right-click a brush for **Add to bottom shelf** and, for custom brushes, **Copy brush code**, **Rename...**,
**Save as .sfbrush...**, **Delete...** and **Reveal in folder** (see [Brush Lab and sharing
brushes](brush-lab.md)).

### Stroke, Alpha and Material pickers

- **Stroke** (click the Stroke tile): Dots, Drag Stamp, Spray and Lazy Stroke, with Scatter for Spray and Lazy
  radius for Lazy Stroke. See [Sculpting and brushes](sculpting.md).
- **Alpha** (click the Alpha tile): Round, Square, Noise and the twelve built-in patterns (Pores, Cracks, Scales,
  Rivets, Stitches, Fabric Weave, Bark, Rock Strata, Wrinkles, Skin Folds, Hair Strands, Dot Grid), the image alpha you loaded, images in your
  library's Alphas folder, **Load image...** and **Invert loaded image**.
- **Material** (<kbd>M</kbd>, or click the Material tile): the Lit Spheres and Studio. Point at a material to
  preview it on the model; click to keep it. Closing the picker without clicking puts the old material back.
  See [Vertex Paint and materials](vertex-paint.md).

### Sliders at the cursor and the Quick Menu

As in ZBrush, press <kbd>S</kbd> for a Draw Size slider under the mouse, <kbd>U</kbd> for Intensity,
<kbd>I</kbd> for RGB Intensity or <kbd>O</kbd> for Falloff. Drag the slider, or type a number and press
<kbd>Enter</kbd> (<kbd>Esc</kbd> cancels). The pop-up closes when you release the slider. <kbd>[</kbd> and
<kbd>]</kbd> make the brush 10% smaller or larger at any time. <kbd>Space</kbd> (or a right click on the canvas) opens the Quick Menu with all of
them and the brush, stroke, alpha and material (see [The Quick Menu](sculpting.md#the-quick-menu)).

## The Library

The Library is a strip of thumbnails across the top of the canvas for browsing everything you can load. Press
<kbd>,</kbd> (comma) or click **Library** on the top shelf to show or hide it. It opens on its own at
start-up (see [Preferences](#interface-preferences)).

![The Library on the Tool tab](images/lightbox.jpg)

| Tab | Shows | Double-click |
|---|---|---|
| **Project** | Projects (`.sfproj`) in your library and recently opened projects | Open the project |
| **Tool** | Sphere, Plane and Cube, and model files in your library | Open (replaces the scene) |
| **Brush** | Every brush: built-in, custom and `.sfbrush` files, and the VDM brushes (built-in ones too) | Select or load the brush |
| **Alpha** | Round, Square, Noise, the twelve built-in patterns and images in your Alphas folder | Use as the brush alpha |
| **Material** | The Lit Spheres (your own included) and Studio | Use the material |
| **Image** | Reference images in your library | A menu: Generate 3D, relief, inflate, project onto the model, use as alpha |
| **Splats** | Gaussian splat captures (`.ply`, `.splat`) | Open the capture as a reference |
| **Snapshots** | The autosave and your snapshots, newest first | Open it |
| **Recent** | Recently opened files of every kind | Open it |

Working with the strip:

- **Shift+double-click** a model, project or primitive to append it to the scene as a new Part instead of
  replacing the scene.
- Drag sideways (or use the mouse wheel) to scroll through the strip. A drag of more than a few pixels scrubs,
  so a plain click still selects.
- Drag a tile down out of the Library onto the canvas to load it, like a double-click.
- Folders open with a double-click; the first tile inside a folder goes back up. The arrows at the top go back
  and forward through tabs and folders, and the breadcrumb shows where you are.
- Type in **Filter** to show only items whose names contain the text.
- Choose **1** to **4** rows of thumbnails.
- **Open folder...** adds a folder of your own to the root of every tab.
- On the Snapshots tab, **Save now** takes a snapshot.
- Right-click a tile for **Open**, **Append as Part**, the image actions, **Show in file manager** and
  **Refresh thumbnail**.

At the root of the Tool and Image tabs is a **Built-in** folder (the models and images that ship with
SculptForge, such as the demo lion head), and the Project, Tool, Brush, Alpha, Image and Splats tabs have a
**User** folder, your library folder (**Documents/SculptForge** by default). To use a different library folder,
choose it in **Preferences > Library > Library folder**.

> **ZBrush:** the Library works like ZBrush's LightBox: comma toggles it, double-click loads, and Built-in and User
> folders play the part of ZBrush's Maxon and User folders.

## Getting help on anything

### Ctrl-hover help

Hold <kbd>Ctrl</kbd> and point at any button, slider, tile or shelf item. A box shows the item's name, its
shortcut as keycaps, what it does, where it lives (for example "Tool > Geometry") and the script command it
runs.

![Ctrl-hover help on the Subdivide button](images/ctrl-help.jpg)

Hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> instead to see the **scripting reference** for items that run a command:
the command's usage line and each parameter with its type and description. This is the same information the
console, `sculptforge-cli` and AI agents use.

Without <kbd>Ctrl</kbd>, icon-only buttons and labels that don't fit show their name after a short delay
(**Preferences > Interface > Tooltip delay**). Turn Ctrl-hover help off with **Preferences > Interface > Popup
Info (Ctrl+hover help)**, and make the box more or less see-through with **Popup Info opacity**.

### The Controls sheet

Press <kbd>F1</kbd> (or **Help > Controls**) for the **Controls** window. The **Keyboard** tab lists every
action that has a shortcut, grouped by palette and generated from the actions themselves, so it always shows
your current keys. The **Mouse & pen** tab lists the gestures. Type in the filter field to search both.
Double-click a row on the Keyboard tab to give that action a new key.

### This guide

**Help > User Guide** (<kbd>Shift</kbd>+<kbd>F1</kbd>) opens this guide in its own window: the contents on
the left (the open chapter lists its sections), the chapter on the right, and links between chapters.

![The User Guide window on the Masking chapter](images/user-guide.jpg)

- **Search:** type in the search box (<kbd>Ctrl</kbd>+<kbd>F</kbd>) to find every section that mentions all
  of your words. Click a result to jump there; the words are marked on the page. <kbd>Esc</kbd> clears it.
- **Back and forward:** the arrow buttons, <kbd>Alt</kbd>+<kbd>Left</kbd> / <kbd>Alt</kbd>+<kbd>Right</kbd>,
  or the back and forward buttons of your mouse.
- **From any control:** while Ctrl-hover help is showing, press <kbd>F1</kbd> (keep <kbd>Ctrl</kbd> held) to
  open the chapter about that control. The help box shows which chapter it is ("Learn more: ...").
- **Code blocks** have a **Copy** button, and **Open in browser** shows the same page on the SculptForge
  website.

The guide is built into SculptForge, so it works offline and always matches your version.

## The command palette

Press <kbd>F3</kbd> (or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>, or click **Search** in the title strip)
to find any action or command by name and run it. It opens at the top centre of the canvas.

![The command palette searching for subdivide](images/command-palette.jpg)

- Type part of a name, a palette or a word from the description. The search is forgiving: letters in order are
  enough ("sbdv" finds Subdivide), and every word you type must match somewhere.
- Each row shows the action, its palette and its shortcut.
- <kbd>Up</kbd> and <kbd>Down</kbd> (or <kbd>Page Up</kbd> and <kbd>Page Down</kbd>) choose a row,
  <kbd>Enter</kbd> runs it and <kbd>Esc</kbd> closes the palette.
- With nothing typed, the palette lists the actions you used most recently.
- Commands with a required parameter (for example `image_relief path=`) open in the console with the name
  already typed. Other commands run with their default values; <kbd>Shift</kbd>+<kbd>Enter</kbd> sends a
  command with parameters to the console instead, so you can set them first.

Everything scriptable is in the command palette, including commands that have no button in the interface.

## The console

Press <kbd>`</kbd> (the backtick key, left of <kbd>1</kbd> on US keyboards) to open the console, a strip
between the canvas and the bottom shelf. It works like ZBrush's ZScript window:

- Every command you run, from a button, a key, the command palette, a script or an AI agent, appears as a
  script line followed by its result. Errors show in red.
- Type a command and press <kbd>Enter</kbd>: for example `remesh resolution=200` or `subdivide; decimate
  ratio=0.5`. <kbd>Tab</kbd> completes command and parameter names, <kbd>Up</kbd> and <kbd>Down</kbd> recall
  earlier lines, and `help` (or `help remesh`) explains commands.
- Drag the console's top edge to make it taller or shorter.
- The **Script** palette has **Run script file...**, **Copy session as script** (every command you ran this
  session, ready to replay, which makes it a macro recorder) and **Clear console**.

![The console after a few commands](images/console.jpg)

See [Scripting and automation](scripting.md) for the command language, scripts and headless use.

## Customizing the interface

Everything about the layout can change: which palettes sit in the trays, what's on each shelf, and which keys
do what. As in ZBrush, **changes last until you quit** unless you store them with **Store Config**.

### Move, add and remove shelf items

1. Open **Preferences > Config** and turn on **Enable Customize**. The shelves get a dashed orange outline and
   the status line reads "Customize: Ctrl+Alt+drag to move or remove items".
2. Hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> and drag any button, slider or tile:
   - onto a shelf to put it there (an orange line shows where it will go), which is how you **add** buttons
     from palettes to a shelf;
   - along its shelf, or to another shelf, to **move** it;
   - off the shelves (onto the canvas, for example) to **remove** it.
3. Turn **Enable Customize** off when you're done, and press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> to
   keep the result.

Right-clicking a shelf item and choosing **Remove from shelf** works without Customize mode.

### Assign shortcuts

- Hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> and **click** any button (with Customize off), then press the key you
  want. <kbd>Esc</kbd> cancels and <kbd>Backspace</kbd> removes the button's shortcut.
- Or open **Preferences > Hotkeys**, find the action (there's a filter), click its key button and press the
  new key. Keys you changed show in orange. **Default shortcuts** undoes every change.
- Or double-click a row in the Controls window (<kbd>F1</kbd>).

If the key already belongs to another action, it moves to the new one (as in ZBrush) and the status line says
which action lost it.

### Store Config and friends

All in **Preferences > Config**:

| Item | What it does |
|---|---|
| **Store Config** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) | Keeps the current layout, shortcuts and interface preferences for future sessions |
| **Restore Custom UI** | Goes back to what the last Store Config saved |
| **Restore Standard UI** | Goes back to the built-in layout and default shortcuts (Store Config afterwards to make it stick) |
| **Save UI...** | Saves the layout, shortcuts and preferences to a `.json` file you can share |
| **Load UI...** (<kbd>Ctrl</kbd>+<kbd>L</kbd>) | Loads such a file (Store Config afterwards to keep it) |

The first time you change the layout in a session, the status line reminds you once: "Layout changed -
Ctrl+Shift+I to keep it". What a session changes by itself (the console and Library being open, the Library
tab) doesn't count as a layout change.

> **ZBrush:** Store Config has the same shortcut as in ZBrush, and SculptForge never saves the layout on its
> own either.

### Layout presets

The **1**, **2** and **3** buttons in the title strip (also in **Preferences > Config**) switch between three
layouts:

1. **Standard**: the Tool palette in the right tray and shelves around the canvas.
2. **Sculpt**: both trays closed, for the largest canvas.
3. **Wide**: Brush, Stroke and Alpha in the left tray, Tool and Material in the right tray.

A preset changes the layout only (the console, Library and left-handed setting stay as they are). Store
Config keeps it.

### Interface preferences

**Preferences > Interface**:

| Setting | Default | What it does |
|---|---|---|
| **Language** | English | The interface language: **English** or **Español** (see below) |
| **Popup Info (Ctrl+hover help)** | On | Ctrl-hover help boxes |
| **Popup Info opacity** | 0.97 | How solid the help box is |
| **Tooltip delay** | 0.5 s | How long to point at an icon-only button before its name appears |
| **One sub-palette open** | On | Opening a sub-palette closes the others in that palette |
| **Auto collapse left tray** / **right tray** | On / Off | Collapse other palettes in the tray when one wouldn't fit |
| **Palettes strictly A-Z (ZBrush order)** | Off | Strict alphabetical palette bar instead of File and Edit first, Help last |
| **Left-handed layout** | Off | Mirrors the shelves and trays: tools on the right, navigation on the left |
| **UI scale** | Automatic | Size of text and controls (100% to 200%); takes effect at the next start |
| **Camera notch** | Automatic | Keeps the interface below a laptop's camera notch in fullscreen (see below) |

**Language** changes the interface at once, without a restart; Store Config keeps it for the next start. In
**Español** the palette bar, the palettes, the shelves, the Library, the command palette and the Ctrl-help titles
are in Spanish, while the names of tools and brushes stay in English, as Spanish-speaking sculptors use them (Parts,
Quad Remesh, Forge Spheres, Library, Lit Sphere, Clay Block, Add). The command palette finds a command by its Spanish or its
English name. Anything not translated yet (most Ctrl-help descriptions, this guide) shows in English. Scripts and
`sculptforge-cli` commands are the same in every language.

**Preferences > Input** sets the key **Style** (the first start sets it from your **Coming from** answer):
**SculptForge** (the default for new installs): <kbd>1</kbd> to <kbd>9</kbd> and <kbd>0</kbd> pick the first ten
brushes, <kbd>I</kbd> opens Intensity like <kbd>U</kbd>, and <kbd>Ctrl</kbd>+drag on the background zooms when
nothing is masked; **ZBrush-style**: ZBrush's keys, with the number keys free (<kbd>9</kbd> is Save Snapshot),
<kbd>I</kbd> for RGB Intensity and <kbd>Ctrl</kbd>+drag on the background for a mask rectangle (see
[ZBrush-style keys](shortcuts.md#zbrush-style-keys)); **Maya**: Maya's view controls; or **Blender**: middle-drag navigation,
the numpad views and Blender's sculpt-mode keys (see [Blender keys](shortcuts.md#blender-keys)). In the Blender
style, **Emulate Numpad** makes the number row work as the numpad, and **Import Blender keymap...** takes the keys
from a keymap you exported in Blender. **Right click opens the Quick Menu** (on by default, in every style) opens
the [Quick Menu](sculpting.md#the-quick-menu) with a right click on the canvas; a right drag navigates either
way. **Hold Space and drag to move the camera** (off by default) turns <kbd>Space</kbd> into a camera key: held,
left-drag rotates, right- or middle-drag pans and <kbd>Ctrl</kbd>+drag zooms (see
[Navigation](navigation.md#mouse-and-pen)). **Radial Menus** edits your pie menus and their keys (see [Radial menus](sculpting.md#radial-menus)).
**Cursor follows surface** (on by default) lays the brush rings on the model under the pointer (see
[Draw Size and Falloff](sculpting.md#draw-size-intensity-rgb-intensity-and-falloff)); off, they are flat circles on the screen. **Rotation** chooses free rotation (ZBrush) or a turntable. See
[Coming from ZBrush](coming-from-zbrush.md).

### The background

**Preferences > Display** (and the **Render** palette) sets the view's **Background**: **Gradient**, the
charcoal radial gradient (the default), or **Flat Colour**, one solid colour without the gradient. Click the
**Background colour** swatch to pick the colour; picking one switches to Flat Colour. **Store Config** keeps the
choice for the next session, and each project keeps the background it was saved with. From a script:
`set_background mode=flat`, `set_background color=60,70,80` or `set_background mode=gradient`.

> **ZBrush:** like turning the gradient off in the Document palette (**Range** 0) and choosing the **Back**
> colour.

**Preferences > Library** has **Show at startup** (on by default), the number of **Rows**, **Placement**
(along the top of the canvas, or at the bottom above the bottom shelf) and the **Library folder**.
**Preferences > Autosave** sets how often a recovery copy is saved (see [Projects, autosave and snapshots](projects.md)).

![The Preferences palette with the Config and Interface sub-palettes](images/preferences.jpg)

### Fullscreen, the palette bar and the camera notch

- <kbd>F11</kbd> (or **Render > Fullscreen**) fills the screen with SculptForge; press it again to go back to
  a window. `sculptforge --fullscreen` starts that way.
- **Menus** in the title strip hides the palette bar; click it again to bring it back.
- <kbd>Tab</kbd> hides the shelves and trays.
- On MacBooks with a camera notch, SculptForge keeps the interface below the notch when it fills the screen
  and fills the band beside the notch with the shell colour. **Automatic** asks macOS for the safe area (on
  Linux it recognises a notched MacBook display); **Always reserve** always keeps a band free at the top,
  whatever the display; **Off** uses the full height.

## See also

- [Navigating the view](navigation.md)
- [Keyboard and mouse](shortcuts.md)
- [Scripting and automation](scripting.md)
- [Troubleshooting and FAQ](troubleshooting.md)
