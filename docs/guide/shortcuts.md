# Keyboard and mouse

This chapter is the reference for every default keyboard shortcut and mouse gesture in SculptForge. Inside the
app, <kbd>F1</kbd> opens the same list as the Controls window, showing your own keys if you've changed any.

SculptForge has four key styles, chosen in **Edit > Preferences > Input > Style** (and by the **Coming from** question the
first time SculptForge starts): **SculptForge** (the default for new installs), **ZBrush-style**, **Blender** and
**Maya**. The main table shows the SculptForge style; the sections after it list what the
[ZBrush-style](#zbrush-style-keys) and [Blender](#blender-keys) styles change. If you used an earlier version,
your saved style is kept.

## Changing shortcuts

Every key below can be reassigned: hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> and click a button, then press the new
key, or use **Edit > Preferences > Hotkeys**. Press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> (Store Config) to
keep your changes. See [Customizing the interface](interface.md#customizing-the-interface).

On macOS, <kbd>Cmd</kbd> works wherever a shortcut says <kbd>Ctrl</kbd>. Single-letter shortcuts don't fire
while you type in a text field.

## Keyboard shortcuts

The table lists the default shortcut of every action in the SculptForge style, grouped by palette. It is
generated from SculptForge's action registry, the same source as the Controls window.

<!-- BEGIN GENERATED: shortcuts -->

### Brush

| Action | Keys | What it does |
|---|---|---|
| Brush picker | <kbd>B</kbd> | Pop up every brush under the cursor. Type the first letter of a brush, then the orange letter on its tile (B S T = Standard). |
| Capture from view | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>C</kbd> | Alpha and VDM from the viewport: drag a square over any sculpted detail (Shift for a circle) and capture it as a grayscale height alpha and as a vector displacement brush, ready to stamp with the VDM brush or along strokes with Detail Clone, and to save in your library. |
| Clay | <kbd>2</kbd> | Builds up clay that fills the hollows first. |
| Clay Block | <kbd>0</kbd> | Square, flat-topped clay strokes for blocking in forms. |
| Crease | <kbd>6</kbd> | Cuts a sharp crease; Alt raises a ridge. |
| Flatten | <kbd>7</kbd> | Presses the surface toward a plane. |
| Inflate | <kbd>5</kbd> | Swells the surface outward along its normals. |
| Move | <kbd>4</kbd> | Grabs the surface under the cursor and pulls it along. |
| Paste brush | <kbd>Ctrl</kbd>+<kbd>V</kbd> | Add the brush on the clipboard: a brush code someone shared, or the text of a .sfbrush file. |
| Pinch | <kbd>8</kbd> | Pulls the surface toward the stroke to tighten edges. |
| Sharp Crease | <kbd>9</kbd> | A sharp V-shaped crease with the sides pinched in (ZBrush DamStandard). |
| Smooth | <kbd>3</kbd> | Relaxes the surface. Hold Shift to smooth with any brush. |
| Standard | <kbd>1</kbd> | Raises the surface along the stroke; Alt digs in. |

### Color

| Action | Keys | What it does |
|---|---|---|
| Pick colour | <kbd>C</kbd> | Take the paint colour from the model under the cursor. |
| Switch Color | <kbd>V</kbd> | Swap the main and the secondary colour. |

### Draw

| Action | Keys | What it does |
|---|---|---|
| Draw Size at cursor | <kbd>S</kbd> | A Draw Size slider under the cursor: drag it, or type a number and press Enter. |
| Falloff at cursor | <kbd>O</kbd> | A Falloff slider under the cursor: below 0 a broad, hard brush tip, above 0 a soft point (-100..100). The inner ring of the brush cursor shows it. |
| Intensity at cursor | <kbd>U</kbd> or <kbd>I</kbd> | A Intensity slider under the cursor: drag it, or type a number (1-100) and press Enter. Hold Shift (or pick Smooth) to set Smooth's own Intensity. |
| Larger brush | <kbd>]</kbd> | Make the brush 10% larger (hold to repeat; Shift+] steps one pixel). |
| Larger brush by 1 px | <kbd>Shift</kbd>+<kbd>]</kbd> | Make the brush one pixel larger (half a pixel below 2 px). |
| Paint | <kbd>P</kbd> | Paint colour without sculpting (ZBrush Rgb on, Zadd off). Alt paints back to white, Shift still smooths. |
| Smaller brush | <kbd>[</kbd> | Make the brush 10% smaller (hold to repeat; Shift+[ steps one pixel). |
| Smaller brush by 1 px | <kbd>Shift</kbd>+<kbd>[</kbd> | Make the brush one pixel smaller (half a pixel below 2 px), down to 0.5 px. |

### Edit

| Action | Keys | What it does |
|---|---|---|
| Redo | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> or <kbd>Ctrl</kbd>+<kbd>Y</kbd> | Redo the last undone action. |
| Undo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> | Undo the last action. |

### File

| Action | Keys | What it does |
|---|---|---|
| Export GLB... | <kbd>Ctrl</kbd>+<kbd>E</kbd> | Export the model as glTF binary, with sculpt layers as blend shapes (morph targets). |
| New Sphere | <kbd>Ctrl</kbd>+<kbd>N</kbd> | Replace the scene with a new sphere ready for sculpting. |
| Open... | <kbd>Ctrl</kbd>+<kbd>O</kbd> | Open a project (.sfproj), a model (.obj, .glb, .fbx, .stl, .ply, .usd ...) or splats. |
| Quit | <kbd>Ctrl</kbd>+<kbd>Q</kbd> | Close SculptForge (asks to save changes first). |
| Save | <kbd>Ctrl</kbd>+<kbd>S</kbd> | Save the project (.sfproj: every Part, layer, brush and setting). Asks for a name the first time. |
| Save As... | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> | Save the project under a new name. |
| Send to Unreal | <kbd>Ctrl</kbd>+<kbd>U</kbd> | Export a game-ready FBX and import it into the running Unreal Editor (Python remote execution). |

### Help

| Action | Keys | What it does |
|---|---|---|
| Command palette | <kbd>F3</kbd> or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> | Search every command, brush and setting by name and run it. Commands that need parameters open in the console. |
| Controls | <kbd>F1</kbd> | Every shortcut and mouse gesture, generated from the commands themselves. |
| Quick Menu | <kbd>Space</kbd> | ZBrush's QuickMenu under the cursor: brush, stroke, alpha, material, colour, Draw Size, Falloff and the intensities. Hold Space and let go to close it, or tap Space to keep it open. |
| User Guide | <kbd>Shift</kbd>+<kbd>F1</kbd> | The SculptForge user guide: every tool explained, with search. Hold Ctrl over a control and press F1 to open the chapter about it. |

### Library

| Action | Keys | What it does |
|---|---|---|
| Library | <kbd>,</kbd> | Show or hide the Library: thumbnails of your projects, models, brushes, alphas, materials, images, splats and Snapshots along the top of the canvas. Double-click to load. |

### Material

| Action | Keys | What it does |
|---|---|---|
| Material picker | <kbd>M</kbd> | Pop up the materials under the cursor. Hover one to preview it on the model; click to keep it. |

### Preferences

| Action | Keys | What it does |
|---|---|---|
| Load UI... | <kbd>Ctrl</kbd>+<kbd>L</kbd> | Load a layout file someone shared (or you saved with Save UI). |
| Radial menu 1: Brushes | <kbd>T</kbd> | Hold the key, flick toward an item and let go to run it; or tap the key and click one. Edit it in Preferences > Input > Radial Menus. |
| Radial menu 2: Masking | <kbd>Y</kbd> | Hold the key, flick toward an item and let go to run it; or tap the key and click one. Edit it in Preferences > Input > Radial Menus. |
| Radial menu 3: Strokes | <kbd>J</kbd> | Hold the key, flick toward an item and let go to run it; or tap the key and click one. Edit it in Preferences > Input > Radial Menus. |
| Save UI... | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> | Save the layout, shortcuts and interface preferences to a file you can share. |
| Shelves | <kbd>Tab</kbd> | Hide or show the shelves and trays around the canvas. |
| Store Config | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> | Keep the current layout, shortcuts and interface preferences for future sessions. Like ZBrush, SculptForge never saves them on its own. |

### Render

| Action | Keys | What it does |
|---|---|---|
| Fullscreen | <kbd>F11</kbd> | Fill the screen with SculptForge (again: back to a window). |
| Mask | <kbd>Ctrl</kbd>+<kbd>H</kbd> | Show or hide the mask (masked areas drawn darker). Hiding it keeps the mask: masked areas stay protected from brushes until you clear it. |
| Wireframe | <kbd>Shift</kbd>+<kbd>F</kbd> | Show the mesh's polygons (quads and triangles) over the surface (ZBrush PolyFrame). |

### Script

| Action | Keys | What it does |
|---|---|---|
| Console | <kbd>`</kbd> | The script console: every command you run appears as a script line, and you can type commands yourself. |

### Stroke

| Action | Keys | What it does |
|---|---|---|
| Steady Stroke | <kbd>L</kbd> | Steady Stroke on or off, with every stroke type (Dots, Drag Stamp, Slide Stamp, Spray): the brush trails the cursor on a string and only moves once the string is taut, for steady lines without hand shake. |
| Stroke popup | <kbd>G</kbd> | A grid at the cursor with the stroke types and the modifiers (Add / Sub, Smooth, Mask, Select). Hold the key, point at one and let go; or tap the key and click. Smooth, Mask and Select stay on without holding Shift or Ctrl, until you pick them again or another brush. |

### Tool

| Action | Keys | What it does |
|---|---|---|
| Clear Mask | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> | Unmask everything (also: Ctrl+drag on the background, away from the model). |
| Divide | <kbd>Ctrl</kbd>+<kbd>D</kbd> | Add a subdivision level: four times the polygons, and the lower levels are kept, so you can step down to shape big forms and back up to the detail. Quads stay quads (Catmull-Clark). Smt off divides without smoothing. |
| Duplicate Part | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd> | Copy the active Part, with its layers and attributes, and make the copy active (also in the Parts list's right-click menu). |
| Group Masked | <kbd>Ctrl</kbd>+<kbd>W</kbd> | Turn the masked part into a new face group and clear the mask (ZBrush Ctrl+W). With nothing masked, everything visible becomes one group. |
| Higher Res | <kbd>D</kbd> | Step one subdivision level up (more polygons, for detail). Edits made below carry the detail along. |
| Invert Mask | <kbd>Ctrl</kbd>+<kbd>I</kbd> | Mask what is unmasked and unmask what is masked (also: Ctrl+click the background). |
| Lower Res | <kbd>Shift</kbd>+<kbd>D</kbd> | Step one subdivision level down (fewer polygons, for big forms). The detail of the higher levels is kept. |
| Mask All | <kbd>Ctrl</kbd>+<kbd>A</kbd> | Mask the whole visible Part. |
| Preview | <kbd>A</kbd> | Show the skin Make Mesh would build instead of the spheres (ZBrush's Preview). Turn it off to edit the spheres again. |

### Transform

| Action | Keys | What it does |
|---|---|---|
| Back view | <kbd>Ctrl</kbd>+<kbd>Numpad 1</kbd> | Look at the model from behind (orthographic). |
| Bottom view | <kbd>Ctrl</kbd>+<kbd>Numpad 7</kbd> | Look up at the model from below (orthographic). |
| Draw | <kbd>Q</kbd> | Draw mode: brushes sculpt and paint (leaves the Gizmo). |
| Frame | <kbd>F</kbd> | Fit the whole model in the view. |
| Frame Part | <kbd>Numpad .</kbd> | Fit the active Part in the view (Blender: View Selected). |
| Front view | <kbd>Numpad 1</kbd> | Look at the model from the front (orthographic). |
| Left view | <kbd>Ctrl</kbd>+<kbd>Numpad 3</kbd> | Look at the model's left side (orthographic). |
| Move | <kbd>W</kbd> | Gizmo, Move: drag an arrow to move along an axis, a square to move in a plane, the centre to move in the view. Masked parts stay; Shift snaps to the grid; Alt+drag moves only the gizmo. W again: back to Draw. |
| Opposite side | <kbd>Numpad 9</kbd> | Look at the model from the other side. |
| Orbit down | <kbd>Numpad 2</kbd> | Turn the view 15 degrees down under the model. |
| Orbit left | <kbd>Numpad 4</kbd> | Turn the view 15 degrees to the left around the model. |
| Orbit right | <kbd>Numpad 6</kbd> | Turn the view 15 degrees to the right around the model. |
| Orbit up | <kbd>Numpad 8</kbd> | Turn the view 15 degrees up over the model. |
| Orthographic | <kbd>Numpad 5</kbd> | Switch between a perspective and an orthographic view (no foreshortening: sizes don't shrink with depth). |
| Right view | <kbd>Numpad 3</kbd> | Look at the model's right side (orthographic). |
| Rotate | <kbd>R</kbd> | Gizmo, Rotate: drag a ring to turn about that axis, the outer ring to turn in the view. Mask the body and put the pivot at a joint to pose a limb. Shift snaps to 15 degrees. R again: back to Draw. |
| Scale | <kbd>E</kbd> | Gizmo, Scale: drag a box to stretch along an axis or the centre to scale evenly (right or up: bigger). Shift snaps to 10% steps. E again: back to Draw. |
| Top view | <kbd>Numpad 7</kbd> | Look down on the model (orthographic). |
| X | <kbd>X</kbd> | Mirror strokes across X (left and right). |
| Zoom in | <kbd>Numpad Plus</kbd> | Move the view closer. |
| Zoom out | <kbd>Numpad Minus</kbd> | Move the view back. |

### UV Editor

| Action | Keys | What it does |
|---|---|---|
| UV Editor | <kbd>Alt</kbd>+<kbd>U</kbd> | Open the UV Editor: the Part's UV layout in 2D over the UDIM tiles. Select vertices, edges, faces or islands (here or on the model), move, rotate and scale them, mark seams, unwrap, pack, set texel density and lay islands into trim sheet strips. Stretch and texel density show live on the model while you edit. |

### View

| Action | Keys | What it does |
|---|---|---|
| Lock camera | <kbd>Shift</kbd>+<kbd>L</kbd> | Lock the camera for left-drags: a drag that starts on the background sculpts once it reaches the model instead of turning the view (Preferences > Input > Start strokes off the model, #62). The status bar shows CAMERA LOCKED while it is on. Right-drag or Alt+drag still move the view. Shift+L turns it on and off. |

<!-- END GENERATED: shortcuts -->

## Mouse and pen gestures

| Action | Gesture |
|---|---|
| Sculpt | Left-drag on the model |
| Sculpt from beside the model | Turn on **Edit > Preferences > Input > Start strokes off the model** (or press <kbd>Shift</kbd>+<kbd>L</kbd>), then left-drag from the background onto the model; the view then turns with a right-drag (see [Navigation](navigation.md#mouse-and-pen)) |
| Invert the brush (subtract) | Hold <kbd>Alt</kbd> while sculpting |
| Smooth | Hold <kbd>Shift</kbd> while sculpting (works with any brush, and with Paint) |
| Straight line | Press <kbd>Shift</kbd> after a stroke has started (45 degree steps) |
| Roll the view | <kbd>Shift</kbd>+click the background, let go of <kbd>Shift</kbd>, drag |
| Rotate | Left-drag on the background, or right-drag; hold <kbd>Shift</kbd> to snap to an axis view |
| Pan | <kbd>Alt</kbd>+left-drag on the background, <kbd>Alt</kbd>+right-drag, or middle-drag |
| Zoom | Mouse wheel; <kbd>Alt</kbd>+drag on the background then release <kbd>Alt</kbd>; <kbd>Ctrl</kbd>+right-drag; <kbd>Ctrl</kbd>+middle-drag; <kbd>Ctrl</kbd>+left-drag on the background when nothing is masked (not in the ZBrush style) |
| Paint the mask | Hold <kbd>Ctrl</kbd> while stroking on the model |
| Erase the mask | Hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd> while stroking on the model |
| Blur / sharpen the mask | <kbd>Ctrl</kbd>+click / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click the model |
| Invert the mask | <kbd>Ctrl</kbd>+click the background |
| Clear the mask | <kbd>Ctrl</kbd>+drag on the background with something masked (with **Mask Pen**), or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> |
| Mask inside a lasso or rectangle | <kbd>Ctrl</kbd>+drag with **Mask Lasso** or **Mask Rectangle** picked in **Tool > Masking** (add <kbd>Alt</kbd>: unmask) |
| Isolate a face group | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click it (again: invert the visibility; with something hidden already: hide it) |
| Hide a face group | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+click it |
| Show everything | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click the background |
| Hide outside a rectangle or lasso | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag (add <kbd>Alt</kbd>: hide inside); **Select Rectangle** or **Select Lasso** in **Tool > Visibility** |
| Select a Part | <kbd>Alt</kbd>+click it in the view (<kbd>Shift</kbd>+<kbd>Alt</kbd>+click, or a triple-click on another Part: select and frame it) |
| Move, scale or rotate with the Gizmo | <kbd>W</kbd>, <kbd>E</kbd> or <kbd>R</kbd>, then drag a handle; hold <kbd>Shift</kbd> to snap (15 degrees, the grid, 10%) |
| Move the gizmo only | <kbd>Alt</kbd>+drag a gizmo handle |
| Put the gizmo's pivot on the model | <kbd>Alt</kbd>+click the model in Move, Scale or Rotate mode |
| Pan, zoom or orbit with a button | Drag on **Move**, **Zoom** or **Rotate** on the right shelf |
| Help on any control | Hold <kbd>Ctrl</kbd> and point at it (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>: scripting parameters) |
| Assign a shortcut | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click any button, then press the key |
| Move or remove shelf items | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+drag (with **Enable Customize** on) |
| Tablet pressure | Used automatically when a pen is detected (**Pressure > Strength** / **Pressure > Size**) |
| Open files | Drop a model (`.obj`, `.glb` ...), project, brush (`.sfbrush`), splat capture or image onto the window |

## Keys inside pop-ups and panels

Some keys only mean something while a pop-up or panel has the keyboard.

### Brush picker (B)

| Key | Action |
|---|---|
| A letter | Keep the brushes whose names start with the letters typed so far |
| The highlighted letter on a tile | Pick that brush (<kbd>B</kbd> <kbd>S</kbd> <kbd>T</kbd> = Standard) |
| <kbd>Backspace</kbd> | Take back one letter |
| <kbd>Enter</kbd> | Pick the first match |
| <kbd>Esc</kbd> | Close |

### Sliders at the cursor (S, U, I, O)

| Key | Action |
|---|---|
| Digits and <kbd>.</kbd> | Type a value |
| <kbd>Enter</kbd> | Apply the typed value and close |
| <kbd>Esc</kbd> | Close without applying a typed value |

### Command palette (F3)

| Key | Action |
|---|---|
| <kbd>Up</kbd> / <kbd>Down</kbd>, <kbd>Page Up</kbd> / <kbd>Page Down</kbd> | Choose a row |
| <kbd>Enter</kbd> | Run it |
| <kbd>Shift</kbd>+<kbd>Enter</kbd> | Open a command with parameters in the console to edit them |
| <kbd>Esc</kbd> | Close |

### Console (`)

| Key | Action |
|---|---|
| <kbd>Enter</kbd> | Run the line (statements separated by `;`) |
| <kbd>Tab</kbd> | Complete a command or parameter name; press again to list the candidates |
| <kbd>Up</kbd> / <kbd>Down</kbd> | Walk through earlier lines (the last 200 are remembered between sessions) |

### Shortcut capture (Ctrl+Alt+click)

| Key | Action |
|---|---|
| Any key or chord | Becomes the new shortcut (taken from another action if it had it) |
| <kbd>Backspace</kbd> or <kbd>Delete</kbd> | Remove the shortcut |
| <kbd>Esc</kbd> | Cancel |

## ZBrush-style keys

With **Edit > Preferences > Input > Style** on **ZBrush-style** (also at the top of the Controls window, <kbd>F1</kbd>,
or **ZBrush** in the first-start **Coming from** question), the keys and gestures work as in ZBrush. Store Config
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) keeps the choice. Compared with the table above, these change:

| To | SculptForge style | ZBrush-style |
|---|---|---|
| Pick one of the first ten brushes | <kbd>1</kbd> to <kbd>9</kbd>, <kbd>0</kbd> (Standard, Clay, Smooth, Move ...) | No key: the number keys are free |
| Save Snapshot | No key | <kbd>9</kbd> (ZBrush's QuickSave) |
| Slider at the cursor on <kbd>I</kbd> | Intensity (as <kbd>U</kbd>) | RGB Intensity |
| <kbd>Ctrl</kbd>+drag on the background with nothing masked | Zooms | Draws a mask rectangle (one that misses the model clears the mask) |
| Zoom drags (<kbd>Ctrl</kbd>+right-drag, <kbd>Alt</kbd>+drag then release) | Up zooms in | Up zooms out |

Everything else, from the <kbd>B</kbd> brush letters and <kbd>S</kbd> / <kbd>U</kbd> / <kbd>O</kbd> to the
<kbd>Ctrl</kbd> and <kbd>Ctrl</kbd>+<kbd>Shift</kbd> gestures, is the same in both styles. The **Maya** style keeps
these ZBrush-style keys and changes only the view controls (<kbd>Alt</kbd>+left, middle and right drags).

## Maya keys

With **Edit > Preferences > Input > Style** on **Maya** (#32), the view works as in Maya, on the model or beside it, as
long as <kbd>Alt</kbd> is the only key held:

| To | Maya style |
|---|---|
| Rotate | <kbd>Alt</kbd>+left-drag |
| Pan | <kbd>Alt</kbd>+middle-drag |
| Zoom | <kbd>Alt</kbd>+right-drag **sideways**: right zooms in, left zooms out, as Maya's own dolly |
| Invert the brush (subtract) | Hold <kbd>Ctrl</kbd> while sculpting |
| Paint a mask / erase it | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+drag / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+drag |
| Smooth | Hold <kbd>Shift</kbd> while sculpting |
| Visibility gestures | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>, as in the other styles |

Maya's default dolly follows a sideways drag, so that is the default here too. **Edit > Preferences > Input > Zoom
drag** switches it to **Up and down** (drag up to zoom in) if you set Maya up that way. **Invert zoom**, in every
style, turns zoom drags around (the mouse wheel stays as it is).

## Blender keys

With **Edit > Preferences > Input > Style** on **Blender** (also at the top of the Controls window, <kbd>F1</kbd>), the
view and the sculpt keys work as in Blender 4.2 and later, Sculpt Mode (with 5.1's Alt mask). Compared with the table above, these
change in the Blender style:

| To | Blender style |
|---|---|
| Rotate | Middle-drag, or <kbd>Alt</kbd>+left-drag beside the model (everywhere with **Edit > Preferences > Input > Emulate 3 Button Mouse**, for pens and trackpads), or left-drag beside the model |
| Pan | <kbd>Shift</kbd>+middle-drag, or <kbd>Alt</kbd>+<kbd>Shift</kbd>+left-drag |
| Zoom | Mouse wheel, <kbd>Ctrl</kbd>+middle-drag (up: closer), <kbd>Alt</kbd>+<kbd>Ctrl</kbd>+left-drag, <kbd>Numpad Plus</kbd> / <kbd>Numpad Minus</kbd> |
| Invert the brush (subtract) | Hold <kbd>Ctrl</kbd> while sculpting |
| Mask / unmask | <kbd>Alt</kbd>+drag / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+drag on the model (Blender 5.1 and later), or the Mask brush (<kbd>M</kbd>) |
| Smooth | Hold <kbd>Shift</kbd> while sculpting |
| Front, right, top view | <kbd>Numpad 1</kbd>, <kbd>Numpad 3</kbd>, <kbd>Numpad 7</kbd>; with <kbd>Ctrl</kbd>: back, left, bottom |
| Orthographic on or off | <kbd>Numpad 5</kbd> (the axis views turn it on until you rotate again) |
| Orbit in 15 degree steps, the other side | <kbd>Numpad 4</kbd> <kbd>Numpad 6</kbd> <kbd>Numpad 8</kbd> <kbd>Numpad 2</kbd>, <kbd>Numpad 9</kbd> |
| Frame the active Part / everything | <kbd>Numpad .</kbd> / <kbd>Home</kbd> |
| Brush radius / strength | <kbd>F</kbd> / <kbd>Shift</kbd>+<kbd>F</kbd> (drag, or type a number) |
| Draw, Smooth, Pinch, Inflate, Grab (Move) | <kbd>V</kbd>, <kbd>S</kbd>, <kbd>P</kbd>, <kbd>I</kbd>, <kbd>G</kbd> |
| Clay Strips (Clay Block), Crease, Tendril, Scrape (Flatten) | <kbd>C</kbd>, <kbd>Shift</kbd>+<kbd>C</kbd>, <kbd>K</kbd>, <kbd>Shift</kbd>+<kbd>T</kbd> |
| Mask brush | <kbd>M</kbd>, then drag to mask and <kbd>Ctrl</kbd>+drag to unmask; <kbd>M</kbd> again goes back to your brush |
| Clear / invert the mask | <kbd>Alt</kbd>+<kbd>M</kbd> / <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide the face group under the cursor | <kbd>H</kbd>; <kbd>Shift</kbd>+<kbd>H</kbd> shows only it (again: everything); <kbd>Alt</kbd>+<kbd>H</kbd> shows everything |
| Lower / higher subdivision level | <kbd>Alt</kbd>+<kbd>1</kbd> / <kbd>Alt</kbd>+<kbd>2</kbd> (<kbd>Shift</kbd>+<kbd>D</kbd> / <kbd>D</kbd> still work) |
| Swap colours / sample colour | <kbd>X</kbd> / <kbd>Shift</kbd>+<kbd>X</kbd> |
| Smooth stroke (Lazy Stroke) | <kbd>Shift</kbd>+<kbd>S</kbd> (or <kbd>L</kbd>) |
| Wireframe | <kbd>Shift</kbd>+<kbd>Z</kbd> |

Choosing the Blender style also sets **Rotation** to **Turntable**, as Blender orbits; change it back under
**Edit > Preferences > Input** if you like. Blender's face sets are SculptForge's face groups, and its brushes map to the
nearest SculptForge brush. **Symmetry X**, **Paint** and the material picker have no key in the Blender style
(they are on the shelves); give them one with <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click. Laptops without a numpad:
turn on **Edit > Preferences > Input > Emulate Numpad** and the number row works as the numpad.

The numpad views (<kbd>Numpad 1</kbd>, <kbd>3</kbd>, <kbd>7</kbd>, <kbd>5</kbd> ...) work in every style.

### Your own Blender keymap

If you changed Blender's keys, export them in Blender (**Edit > Preferences > Keymap**, the **Export...** button
next to the keymap name) and in SculptForge choose **Import Blender keymap...** under **Edit > Preferences > Input** or
at the top of the Controls window. SculptForge switches to the Blender style and takes, from the file's 3D View
and Sculpt keymaps, the keys of every action it has an equivalent for: the views, the brushes above, the radius
and strength, the mask, hiding, subdivision levels, undo and redo. Mouse buttons and keys for things
SculptForge doesn't have are left alone; the status bar says how many keys it took. The file is read as text and
never run. <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> (Store Config) keeps the keys; **Restore Standard UI**
goes back to the defaults.

## ZBrush keys that work differently

ZBrush's keys and gestures work in SculptForge with the [ZBrush-style keys](#zbrush-style-keys). The few that
differ even then, because the feature works differently or doesn't exist yet:

| ZBrush | SculptForge |
|---|---|
| <kbd>P</kbd> (perspective) | <kbd>P</kbd> is the Paint brush; <kbd>Numpad 5</kbd> switches to an orthographic view |
| <kbd>T</kbd> (Edit mode) | Always in Edit mode: there is no 2.5D canvas |
| <kbd>W</kbd> / <kbd>E</kbd> / <kbd>R</kbd> (Transpose line) | The Gizmo (see [Gizmo](gizmo.md)) |
| <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd> (replay and record strokes) | Not yet |
| <kbd>N</kbd> (list the SubTools) | **Tool > Parts** in the right tray |
| <kbd>F1</kbd> to <kbd>F6</kbd> (pop-ups) | <kbd>F1</kbd> is the Controls window, <kbd>F3</kbd> the command palette; <kbd>B</kbd>, <kbd>M</kbd> and the left-shelf tiles pop up brushes, strokes, alphas and materials |
| <kbd>Ctrl</kbd>+<kbd>E</kbd> (edge loop), <kbd>Ctrl</kbd>+<kbd>U</kbd> (reload ZScript) | Export GLB and Send to Unreal |
| <kbd>H</kbd> (hide the ZScript window) | <kbd>`</kbd> toggles the console |

See [Coming from ZBrush](coming-from-zbrush.md) for the whole list.

## See also

- [The interface](interface.md)
- [Navigating the view](navigation.md)
- [Masking](masking.md)
- [Face Groups and visibility](face-groups.md)
