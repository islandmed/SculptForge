# What's new

What changed in each version of SculptForge, newest first. The first time you start a new version, a window
shows its main features, each with a **Try it** button; **Help > What's New** opens it again. Below are the last
three versions in full, then the older ones in groups. Every detail is in the
[changelog](https://github.com/islandmed/SculptForge/blob/main/CHANGELOG.md).

## Version 0.12.8

```features
date: 2026-11-11
tagline: Reference images in the view, two-key shortcuts, and a Linux start-up fix.
theme: Your drawings in the view.
hero: images/wn-0128-hero.jpg
highlight: Reference images: your front and side drawings in the view while you sculpt (idea by Oslen on Discord)
highlight: Start-up fix: SculptForge should start again on Linux NVIDIA drivers where 0.12.5 to 0.12.7 stopped with a shader error (#192)
highlight: Two-key shortcuts: bind an action to two keys in a row, like A then S (requested by Artizmo on Discord)
highlight: Shortcuts with two keys in a row
highlight: Reference images to sculpt against
title: Reference images
line: Front and side drawings stay in the view while you sculpt, behind or as planes.
image: images/wn-0128-reference-images.jpg
icon: image
action: guide:reference-images.md
title: Two-key shortcuts
line: Bind an action to two keys in a row, like A then S, in Preferences > Hotkeys.
image: images/wn-0128-two-key-shortcuts.jpg
icon: keyboard
action: guide:interface.md#assign-shortcuts
title: Starts again on Linux
line: SculptForge starts again on Linux NVIDIA drivers that stopped with a shader error.
image: images/wn-0128-starts-again-on-linux.jpg
icon: rocket
action: guide:troubleshooting.md#sculptforge-doesnt-start
title: Quick strokes stay whole
line: A fast mouse or trackpad stroke right after a pen stroke keeps its whole line.
image: images/wn-0128-quick-strokes-stay-whole.jpg
icon: brush
action: guide:sculpting.md
```

- **Two-key shortcuts**: bind an action to two different keys in a row, like A then S (**Edit > Preferences >
  Hotkeys**, the small button beside a key).
- **Reference images**: **View > Reference images** keeps pictures in the view while you sculpt: behind the model, or
  as planes at its front, side or top that turn with the view. **Front + side...** sets up a character's two drawings
  in one go. They never take strokes and stay out of renders unless you ask.
- **Start-up fix**: SculptForge should start again on graphics drivers where 0.12.5 and 0.12.6 stopped with "Shader
  compile error".
- **A fast mouse or trackpad stroke right after a pen stroke** keeps its whole line.

## Version 0.12.7

```features
date: 2026-11-11
tagline: Booleans between Parts, Gaussian splats with Blender, and a panel of your own.
theme: Splats, straight into Blender.
hero: images/wn-0127-hero.jpg
highlight: Booleans between Parts: Add, Subtract or Intersect from Tool > Parts, with a live preview
highlight: Gaussian splats both ways with Blender 5.3 through the Blender bridge
highlight: Crisp Flatten, Smooth Peaks, an alpha pool with jitter, and Shift and Ctrl brushes (thanks _tfx on Discord)
highlight: My Panel: a panel of your own, built by Alt-dragging buttons and sections (requested by Hameron)
highlight: Faster Undo after a remesh on dense meshes, and a Threads value above 16 now applies (#181, #184)
highlight: Alpha pool and jitter for non-repeating detail
highlight: Crisp Flatten: hard planes with a sharp edge
highlight: Warnings before destructive operations
highlight: Put any brush on Shift or Ctrl
highlight: Orbit around the point under the pointer
highlight: Exact Booleans between Parts, with a live preview
highlight: My Panel, a panel you build from your own buttons and sections
highlight: Smooth Peaks brush
highlight: Repeat a Part along a curve, real braids and springs
title: Splats with Blender
line: Gaussian splats go to Blender 5.3 and back in one click, colour and placement kept.
image: images/wn-0127-splats-with-blender.jpg
icon: sparkles
action: guide:blender.md
title: Booleans between Parts
line: Add, Subtract or Intersect Parts with a live preview: exact and watertight.
image: images/wn-0127-booleans-between-parts.jpg
icon: box
action: guide:parts.md
title: Alpha pool and jitter
line: Each dab takes a random alpha from a pool, for detail that never repeats.
image: images/wn-0127-alpha-pool-and-jitter.jpg
icon: stamp
action: guide:sculpting.md
title: My Panel
line: Alt-drag any button or section into a panel of your own; F4 opens it.
image: images/wn-0127-my-panel.jpg
icon: layout-template
action: guide:interface.md
title: Faster Undo on dense models
line: Undo after a remesh on a very dense model no longer stops for a second.
image: images/wn-0127-faster-undo-on-dense-models.jpg
icon: undo-2
action: guide:performance.md#working-with-big-meshes
```

- "**Alpha pool and jitter**: **Sculpt > Alpha** has **Scale jitter** and **Position jitter**, and right-clicking an
  alpha or a folder of images in the Library gives **Add to alpha pool** / **Use folder as alpha pool**: each dab
  takes a random alpha, for wood grain and scans that never repeat. Three procedural wood grains are included.
  Requested by a member."
- **Splats with Blender**: with the Blender add-on and Blender 5.3, Gaussian splats go to Blender and back with one
  click, view-dependent colour and placement kept. **File > Export > Gaussian Splats (.ply)** in Blender saves them
  (Blender can't), and a dropped `.splat` file imports there.
- **Crisp Flatten** (Hard Surface pack; thanks _tfx on Discord): flattens to the plane the stroke starts on with a
  clean, sharp edge instead of a soft fade, for hard planes and crisp bevels. In Brush Lab, any custom brush can use
  the new **Lock plane** and **Crisp edge** options.
- **More warnings before levels go**: Delete Masked, Remove Hidden, Split, Mirror, Make Symmetric and Merge by
  Distance ask before they drop a Part's subdivision levels.
- **Shift and Ctrl brushes**: **Shift+click** a brush on the bottom shelf (Trim, Flatten...) and holding Shift sculpts
  with it instead of Smooth; **Ctrl+click** one and holding Ctrl sculpts with it instead of masking. Your main brush
  stays.
- **Orbit around the pointer**: Preferences > Input. A rotation that starts over the model turns the view about the
  point under the pointer, and zooms after it work at that depth. On for new installs; turn it on there if you used
  SculptForge before (thanks dm4nd on Discord).
- **Booleans between Parts**: in **Tool > Parts**, click the mark in a Part's row for **Add**, **Subtract** or
  **Intersect**; the view shows the result live, and **Make Boolean Mesh** turns it into a new Part (the sources stay,
  hidden). Exact and watertight, with face groups, vertex paint and UVs kept.
- "**My Panel** (Edit menu): a panel of your own. Hold Alt and drag any button or whole section from any palette into
  it, then keep it in a tray or press F4 to open it at the cursor."
- "**Smooth Peaks brush**: a new brush in the Smooth family that only lowers the high points, the parts that stick out
  above the surrounding surface, and leaves the hollows alone. Shift-smooth is unchanged. Requested by ABHITAR."
- **Tube**: Braid now weaves over and under, Rope and Braid take 1 to 8 **Strands** (one is a spring), and **Repeat
  along curve** lays copies of another Part along a tube or curve (chain links, beads, scales).
- **A leaner Welcome** (Help > Welcome): the 10-minute lesson and the quick tour side by side, shorter AI and
  membership lines, and a row of small icons for Discord, GitHub, X, Bluesky, Instagram and Facebook (also in Help >
  About).
- The layout now saves itself. Customize Shelves edits, trays and palettes are written a couple of seconds after you
  change them and when you quit, so a restart or a crash no longer loses them. **Save Layout** now marks the layout
  that **Revert to Saved Layout** returns to.
- **SpaceMouse** (macOS, Windows): a SpaceMouse that 3Dconnexion's driver was holding when SculptForge started should
  be picked up on its own once the driver lets go, no unplugging needed.
- **More threads, for real**: a **Threads** value above 16 (Preferences > Performance) now reaches the brushes and the
  remesher after a restart.
- **Faster Undo after a remesh**: undoing or redoing a Voxel Remesh (or another whole-model step) on a very dense
  model no longer stops for a second while the model's search tree is rebuilt. Thanks @TheLycanFenrir and @Devon0209
  (#181).

## Version 0.12.6

```features
date: 2026-11-11
tagline: Pan and zoom keys work again, layouts revert safely, and garments drape on a body.
theme: Drape it, pan it, keep it.
hero: images/wn-0126-hero.jpg
highlight: Pan and zoom keys work again: a tap nudges the view, holding glides (thanks Artizmo on Discord)
highlight: Revert to Saved Layout should no longer crash, and the previous autosave is kept (thanks phoenix and raffaele_3d_design)
highlight: Vertex paint keeps its detail across subdivision levels (thanks masterfull95 on Discord)
highlight: Drape a garment on a body in Tool > Garments, with a steadier cloth rest
highlight: Save Visible Parts As and a Mask colour preference
highlight: Save only the visible Parts
highlight: Featured creator card in the Welcome window
title: Drape a garment on a body
line: Click the body for each panel, press Drape, and the pattern sews itself shut and settles.
image: images/wn-0126-drape-a-garment-on-a-body.jpg
icon: shirt
action: guide:garments.md#drape-it-on-a-body
title: Pan and zoom keys glide
line: A tap nudges the view and holding glides, on their own keys or rebound.
image: images/wn-0126-pan-and-zoom-keys-glide.jpg
icon: orbit
action: guide:navigation.md
title: Layouts revert safely
line: Revert to Saved Layout no longer crashes, and Reset Layout is always a way out.
image: images/wn-0126-layouts-revert-safely.jpg
icon: layout-template
action: guide:interface.md#saving-preferences-and-layouts
title: Save only the visible Parts
line: File > Save Visible Parts As writes a copy with just the Parts you can see.
image: images/wn-0126-save-only-the-visible-parts.jpg
icon: save
action: guide:projects.md#save-a-project
title: Paint keeps its detail
line: Paint at a low level and it blends over the finer colour above instead of replacing it.
image: images/wn-0126-paint-keeps-its-detail.jpg
icon: palette
action: guide:vertex-paint.md
```

- **Drape a garment on a body**: in **Tool > Garments**, click the body once for each panel, press **Drape** and watch
  the pattern sew itself shut and settle, then **Convert to Part** to sculpt it (UVs from the flat pattern).
- "**Save Visible Parts As**: **File > Save Visible Parts As...** writes a copy of the project with only the Parts you
  can see, keeping their materials, layers and the scene's cameras and settings. The open project is untouched."
- **Featured creator**: the Welcome window (Help > Welcome) shows one video by an artist who made something about
  SculptForge, with its picture, title and a **Watch** button that opens it in your browser. The small x hides that
  video. It comes with the daily update check, so it is off with **Preferences > Updates > Check for updates** and
  when you are offline.
- **Alt+click a Part** finds thin and far Parts: a click a few points off the Part counts, and a trackpad click that
  wobbles is still a click.
- **Projects with no models** open: a project saved without any Parts loads as an empty scene, with its settings,
  lights, cameras and views.
- **Pan and zoom keys work again**: Pan left / right / up / down and Zoom in / out (on their own keys or rebound) move
  the view again; a tap nudges it, holding glides.
- **Revert to Saved Layout** should no longer crash (it, Reset Layout and Import Interface now run between frames), a
  colour picker or tile taken off a shelf can be put back (**Preferences > Saving & Layouts**), and there is always a
  way out of a broken layout: **Reset Layout** and `--reset-layout`.
- **Mask colour**: **Edit > Preferences > Interface > Mask colour** picks the colour masked areas show.
- **Vertex paint keeps its detail across subdivision levels**: paint a stroke at a low level and it blends over the
  finer colour above instead of replacing it.

## Version 0.12.5

```features
date: 2026-11-10
tagline: Colour now paints on Sub strokes, your brushes are safer, and the pan keys glide.
theme: Your brushes, safer and smoother.
hero: images/wn-0125-hero.jpg
highlight: Rgb lays colour on Sub strokes too, for every brush and stroke type
highlight: Brushes you made are safer: a missing bottom-shelf brush shows a mark and comes back from its backup (thanks rand_gogh)
highlight: Holding the pan and zoom keys glides smoothly at any frame rate (requested by Artizmo on Discord)
highlight: Thread counts above 16 set in Preferences > Performance apply at the next start (#184)
highlight: A startup splash shows the bust and version while SculptForge starts
title: Colour paints on Sub strokes
line: Colour now paints on Sub strokes too, with every brush and stroke type.
image: images/wn-0125-colour-paints-on-sub-strokes.jpg
icon: palette
action: guide:sculpting.md#sculpt-paint-add-sub-rgb-and-front
title: Brushes you made, kept safe
line: A brush file that can't be read comes back from its backup; a missing slot says so.
image: images/wn-0125-brushes-you-made-kept-safe.jpg
icon: shield-check
action: guide:brush-lab.md#your-brush-library
title: Keys that glide
line: Hold a pan or zoom key: it eases in, keeps one speed at any frame rate and eases out.
image: images/wn-0125-keys-that-glide.jpg
icon: move
action: guide:navigation.md
title: More than 16 threads
line: A thread count above 16 in Preferences > Performance applies at the next start.
image: images/wn-0125-more-than-16-threads.jpg
icon: cpu
action: guide:performance.md#edit--preferences--performance
```

- **Startup splash**: while SculptForge takes a moment to start, the bust, wordmark and version show on the dark
  background, and the main window appears once it is ready (no empty window first). A quick start shows nothing.
  **Preferences > Interface > Show startup splash** turns it off.
- **Smooth pan and zoom keys**: holding Pan left/right/up/down or Zoom in/out now glides: it eases in over a tenth of
  a second, moves at the same speed at any frame rate and eases out when you let go, instead of a jump and a pause.
  Requested by Artizmo on Discord.
- **Member sign-in says what happened**: after you enter the code on GitHub, **Preferences > Updates > Member builds**
  now tells you the result: signed in with member builds on, signed in but not on the member list yet (or an
  invitation still waiting, with a button to the invitations page), declined, or the code ran out.
- **Rgb** now lays colour on Sub strokes too, for every brush and stroke type.
- **Brushes you made are safer**: a brush file that can't be read is kept and comes back from its backup; a
  bottom-shelf slot whose brush is missing shows a "missing" mark instead of a brush that looks usable.
- **Threads above 16**: a thread count you set in **Preferences > Performance** (up to every thread your computer has)
  now applies at the next start; the default stays at 16.

## Version 0.12.4

```features
date: 2026-11-10
tagline: Shortcuts ask before they take a key, and Front faces only shows when it is on.
highlight: Shortcuts ask before they take a key another action uses (requested by Artizmo on Discord)
highlight: Pan and zoom from the keyboard: Ctrl+Numpad 4, 6, 8, 2 and Zoom in/out keep moving while held
highlight: Front faces only shows a tag by the brush cursor and a Turn off button (thanks rand_gogh)
```

- **Shortcuts ask before they take a key**: press a key that another action already uses and a box names that
  action, with **Replace** (Enter) and **Cancel** (Esc); a changed shortcut has a reset arrow in **Preferences >
  Hotkeys**. Requested by Artizmo on Discord.
- **Pan and zoom keys**: Pan left, right, up and down (Ctrl+Numpad 4, 6, 8, 2) step on a press and keep moving while
  held, like Zoom in and Zoom out; **Preferences > Input > Pan and zoom key speed** sets how fast.
- **Front faces only** now shows when it is on: a small tag by the brush cursor and a line at the top of the Brush
  palette, with a button to turn it off.

## Version 0.12.3

```features
date: 2026-11-10
tagline: Your language from the first start, and Slide Stamp turns as you slide.
highlight: The interface starts in your language (Spanish, Traditional or Simplified Chinese)
highlight: Hold Shift to turn a Slide Stamp as you slide it (thanks @Devon0209)
highlight: The welcome has a Language row with all four languages, each in its own script
```

- **Your language on the first start**: if your computer is in Spanish, Traditional Chinese or Simplified Chinese,
  SculptForge starts in it, with a note to change it. Any other language starts in English and the welcome has a
  **Language** row with all four, each in its own script.
- **Turn a Slide Stamp as you slide it**: hold **Shift** while dragging a Slide Stamp and the alpha turns toward the
  cursor instead of the stamp moving; let go and it slides on with that turn. Thanks @Devon0209 (#175).

## Version 0.12.2

```features
date: 2026-11-10
tagline: Curves and rope tubes, a Wood brush pack, an Unlit view, and origin tools.
highlight: Curves, and rope, braid and sleeve tubes (requested by Hameron)
highlight: A Wood brush pack (requested by Hameron)
highlight: Shift smoothing keeps Smooth's own Intensity in every stroke type (thanks @nekirk11)
highlight: Unlit view on Alt+L (thanks @nagdav853); Center on origin and Drop to floor in the Gizmo panel
```

- **Curves**: **Tool > Tube > Result > Curve** draws a guide line on or over the model; **Resample** sets its point
  count exactly and **Tube from curve** turns it into a tube. New tube profiles **Rope**, **Braid** and **Sleeve**,
  and **Twist**, **Spin** and **Spread** sliders. Requested by Hameron.
- **Wood brush pack**: ten brushes and six alphas for weathered grain, splinters, cracks, knots, end-grain rings and
  plank seams (**Sculpt > Brush**, pack **Wood**). Requested by Hameron.
- **Unlit view** (Render menu, <kbd>Alt</kbd>+<kbd>L</kbd>): shows the base colour alone, with no lighting, so painted
  colour reads exactly as stored. Thanks @nagdav853.
- **Center on origin, Drop to floor**: two buttons in the Gizmo panel put the Part's bounding-box centre on 0,0,0 or
  rest it on the floor, in one undo step. With the Classic sculpting keys, **F** frames the active Part and **F**
  twice quickly frames the whole scene.
- **Shift smoothing keeps Smooth's own Intensity** in every stroke type, Drag Stamp too, from the first dab. Thanks
  @nekirk11.
- **Stencil**: Adjust, Tile and Remove are back in **Sculpt > Alpha > Stencil** in the SculptForge layout. Thanks
  MasterFULL on Discord.
- **Drape**: sleeves no longer jitter once the cloth is at rest.
- **Customize Shelves**: every button and value field can be moved with Ctrl+Alt+drag, including greyed-out options
  and the Size, Intensity and Detail fields.

## Version 0.12.1

```features
date: 2026-11-09
tagline: Panels on your second monitor, Voxel Remesh up to 4096, Forge Spheres in one mode, and a Find Problems that finishes the job.
highlight: Palettes, the User Guide and Controls on your second monitor (requested by Richard Throgmorton (MakIt3D), our first Founding Member)
highlight: Voxel Remesh up to 4096, with a live triangle estimate (thanks foirgairwolf on Discord)
highlight: Forge Spheres: draw, move and resize without switching modes (thanks @tristratos)
highlight: Find Problems: Fix all repairs messy meshes completely and says what it couldn't fix (thanks foirgairwolf on Discord)
highlight: Drape settles in tight folds instead of twitching, and Clay, Flatten and Trim on micro levels avoid stretched triangles
```

- **Panels in their own windows**: every palette has a new **Open in its own window** icon in its header, and the
  **User Guide** and **Controls** have one in their toolbar. Drag that window to your second monitor or pen display;
  it opens there again next time. Close it (or **Back in the main window**) to bring the panel back. Requested by
  Richard Throgmorton (MakIt3D), our first Founding Member.
- **Solo** (Tool > Parts) puts back exactly the Parts that were shown before it: a Part you had hidden stays hidden.
  Thanks MasterFULL on Discord.
- **Voxel Remesh up to 4096**: the **Tool > Remesh** Resolution slider now goes to 4096 (two thirds of it still covers
  32-512), the line under it estimates the triangles it makes, and above 512 it asks once and never starts a remesh
  that won't fit in the free memory. A sculpted head remeshes at 1024 into about 5.6 million triangles in about 3 s.
  Thanks foirgairwolf on Discord.
- **Forge Spheres in one mode**: in **Tool > Forge Spheres > Draw**, click a sphere to grow a new one, drag it to
  move it, drag near its edge to resize it; the cursor and a small tag say which. Each drag is one undo step. Thanks
  @tristratos.
- **Find Problems > Fix all** repairs messy meshes (a rough remesh, solids touching along an edge) completely, and
  anything it can't fix is listed under **Not fixed**, with why. The inner wall of a hollow is no longer counted as
  flipped. Thanks foirgairwolf on Discord.
- **Drape** settles where it used to keep twitching: cloth pressed into an armpit or the inside of an elbow stays put,
  and sewn seams no longer pull the cloth through the body while they close.
- **Micro levels**: Clay, Clay Block, Flatten and Trim keep big moves from stretching triangles; ordinary strokes
  look and feel the same.
- **Focus Detail in projects**: a streamed level saves and reopens with its full detail, and bake, displacement
  export, surface noise and copies use it too. Linux and macOS for now.
- **Big stamps on curved surfaces**: Drag Stamp, Slide Stamp and every alpha brush now cover their whole footprint on
  a curved surface, with no untouched gaps. Thanks @Devon0209 (#174).
- **Remesh asks before dropping levels**: Voxel Remesh, Quad Remesh and Decimate say how many subdivision levels they
  would drop, with **Freeze Levels**, **Proceed** or **Cancel**. Thanks @TheLycanFenrir (#173).
- **Founding Studio** is for the first 5 studios, a flat $150 a month, locked.
- **Keyboard and mouse** guide: Option is Alt on macOS, and how to make Option Alt and Cmd Ctrl when the Windows
  version runs under CrossOver or Wine on a Mac. Thanks @fenixpoints.
- **Find Problems**: **Fix all** now repairs messy meshes (a rough remesh, touching solids) completely, and anything
  it can't fix is listed under **Not fixed**, with why.

## Version 0.12.0

```features
date: 2026-11-08
tagline: Garments and cloth, sandboxed plugins, a Tool palette in the order you work, and cleaner cuts and creases.
theme: Garments, plugins, cleaner creases.
hero: images/wn-0120-hero.jpg
highlight: Garments and cloth: real-size sewing patterns (millimetres, curves, notches, grain line, seam allowance) from 8 templates at your sizes, sewn with matched seam lengths; print at home at real size (PDF/SVG pages) or export DXF for cutters; plus a Cloth brush and Drape for folds on any mesh. For cosplay, costume and textile work
highlight: Plugins: install sandboxed add-ons from packs (deformers and generators; no file or network access) (thanks @bribblej-web, #20)
highlight: The Tool palette in the order you work: Reshape gathers Size and the Stack, Mirror moves to Geometry > Symmetry, and Texture Image and Viewport Look say what they do (thanks foirgairwolf on Discord)
highlight: Cleaner cuts and creases: crease edges (select them by angle too) and they stay sharp when you add levels (thanks @NicTanghe on GitHub, #108, and _tfx on Discord); cut caps are even triangles instead of thin slivers (thanks dm4nd on Discord)
highlight: Tube shapes: round, square, triangle or ribbon profiles with taper that follow the surface, and any brush, tool or Esc leaves Draw Tube (thanks GABO on Discord); pose every Part at once (thanks @ABHITAR)
highlight: Each online AI service asks once before anything is sent
highlight: Cloth brush: folds that behave like fabric
highlight: Creased edges stay sharp as you add levels
highlight: Select edges by angle, loop or click
highlight: Sewing patterns from your measurements, printed at home
highlight: Generate 3D: pick the best of three, then one click to a clean sculpting base
highlight: Pose a whole character at once
highlight: Tube: square, triangle and ribbon shapes, tapers, fewer points (thanks GABO on Discord)
highlight: One-click Turntable, Ortho, Floor and Flat
highlight: Cut caps are clean, even triangles instead of thin slivers (thanks dm4nd on Discord)
highlight: Quad Remesh up to 2.3x faster
title: Garments and cloth
line: Sewing patterns at your sizes, printed at home or cut from DXF, plus Cloth and Drape.
image: images/wn-0120-garments-and-cloth.jpg
icon: shirt
action: guide:garments.md
title: Plugins
line: Sandboxed deformers and generators from packs, with no file, network or system access.
image: images/wn-0120-plugins.jpg
icon: puzzle
action: guide:packs-and-macros.md#plugins
title: Pose all Parts
line: Pose every visible Part at once on one light mesh; Apply keeps full resolution.
image: images/wn-0120-pose-all-parts.jpg
icon: person-standing
action: guide:parts.md#pose-all-parts
title: Tube shapes
line: Round, square or ribbon cross-sections, with a taper at either end of the tube.
image: images/wn-0120-tube-shapes.jpg
icon: spline
action: guide:tube.md#shape-cross-section-and-taper
title: Edge creases
line: Crease edges (select them by angle too) and they stay sharp when you add levels.
image: images/wn-0120-edge-creases.jpg
icon: diamond
action: guide:edges.md#creases
```

- **Nothing leaves without your OK**: the first time AI Brush or Generate 3D would send anything to a service on
  someone else's computer, the panel says what is sent, to which address and who pays, and waits for **Allow** (once
  per service). **Use local instead** switches to a model running on this computer. Revoke any service in **Edit >
  Preferences > AI > Online AI services**.
- **Cloth brush**: drag the surface like fabric and it folds where it bunches up; **Sculpt > Brush > Cloth** sets
  Drag, Push, Settle, Grab, Expand, Pinch or Inflate, the fabric (Silk, Cotton, Denim, Leather), gravity reach, Soft
  body and Self-collision. It rests on other Parts and the mask pins it.
- **Drape**: **Tool > Drape** drops the active Part as cloth onto the other Parts (a sheet over a chair, a shirt over
  a body), live, with Silk, Cotton, Denim or Leather; the mask pins it; at rest it is one undo step.
- **Edge creases** (**Tool > Geometry > Edges**): give the selected edges a crease from 0 to 1 with **Crease**, or
  remove it with **Uncrease**, and Add Level keeps them sharp: hard rims, panel lines and corners on a smooth
  subdivided model (#108).
- **Edges mode** (**Tool > Geometry > Edges**): select edges of the base level on the model (click, Shift adds, Ctrl
  removes, double-click takes the edge loop), every hard edge at once with **Select by Angle**, open borders, Grow and
  Shrink. The first step towards edge creases for subdivision (#108).
- **Templates and Print in the Pattern window** (Tool > Garments): pick a garment at your sizes, edit the size chart,
  add garments side by side for an outfit, and print the pattern on A4 or Letter with a live page count.
- **Garment templates and home printing**: trousers, straight and A-line skirts, a hoodie with hood and kangaroo
  pocket, a dress, a hooded cloak and gloves, drawn at real size from a size chart you type in once (`garment_size`,
  `garment_new template=...`, `add=true` for an outfit). `garment_export path=x.pdf` prints any pattern on A4 or
  Letter pages with trim lines, matching triangles, page names and a 50 mm check square; mirrored pieces print once,
  marked "cut 2". Guide: [Garments](garments.md).
- **Generate for form, remesh for topology**: **Generate 3D** gives you 1-3 candidates to compare by silhouette; pick
  one, then **Use as sculpting base** remeshes it into clean, even topology and keeps the detailed original as a
  hidden reference. Bring your own key to use your own provider and model at no markup.
- **A cleaner gizmo**: in **All** (Shift+W) each axis is a thin line with its scale ball and its move arrow, well
  inside the rings, and rings seen edge-on fade away from the centre. Move and Scale use the same arrows and balls.
  Design by fenix on Discord (@fenixpoints).
- **Esc** (or picking a brush) now leaves Move, Scale, Rotate and All, so the next stroke sculpts again.
- **Pose all Parts**: **Tool > Parts > Pose all Parts** merges every visible Part into one light Pose Mesh; pose it
  with the Gizmo and masks, then **Apply Pose** gives every Part the pose at full resolution, as one undo step.
  Requested by @ABHITAR.
- **Tube shapes**: **Tool > Tube > Shape** draws square, triangular and flat ribbon tubes as well as round ones,
  turned and tapered to a point at either end, and changes them after drawing. **Point spacing** draws with fewer
  points, and **Simplify** takes points out of a tube you already drew.
- **View toggles on the right shelf**: **Turn** (turntable), **Ortho**, **Floor** and **Flat** buttons under Rotate
  and Paint switch the rotation style, the projection, the floor grid and the shading in one click, lit while on.
- **Live brush ring on the Size slider**: dragging **Size** shows the brush ring on the model at the new size, fading
  out when you let go.
- **Plugins**: packs can now bring sandboxed plugins: deformers that reshape the active Part and generators that add a
  new Part. Open one from **Tool > Reshape > Plugins**: its settings, **Apply**, and **Stop** while it runs. A plugin
  sees only a copy of the mesh: no files, network or system access, with time and memory limits.
- **Cut** caps and walls come out as clean, even triangles about the size of the mesh's own, ready to sculpt without a
  remesh.
- **Tidier Reshape panel** (thanks foirgairwolf on Discord): **Tool > Reshape** reads top to bottom as Size, then the
  Stack. Polish, Relax, Smooth and Balloon are now steps in Tool > Reshape > Stack (Add step > Surface); their menu
  items and keys still run them at 50.
- **Sharper text on Retina and 5K screens**: text and icons are drawn at the display's full resolution, and
  **Preferences > Interface > UI scale** has finer steps (**110%**, **115%**, **135%**) and goes to **250%** and
  **300%** for large high-resolution monitors.
- **The Tool palette in the order you work**: Parts, then shaping (Geometry, Remesh, **Reshape**, Cut, Layers),
  masking and groups, painting (Paint Layers, **Texture Image**), then Bake and Export, with **Viewport Look** last.
  **Reshape** lists Smooth, Relax, Polish and Balloon from gentlest to strongest, and **Mirror** now sits beside
  **Make Symmetric** in **Tool > Geometry > Symmetry**. Extract's button is **Make Shell**, the mesh check is **Find
  Problems**, and **Edit > Preferences > Saving & Layouts** holds Save Layout, Reset and Export / Import Interface.
  Shortcuts, shelves, scripts, saved layouts and files are unchanged, and the command search (F3) still finds each one
  by its old name.
- **Masked only** in **Tool > Geometry > Merge by Distance**: weld just the masked points, for example one seam, and
  leave the rest of the Part alone.
- **Hidden faces**: Quad Remesh, Voxel Remesh, Decimate and Merge by Distance ask first when part of the Part is
  hidden, with **Unhide and continue** or **Cancel**.
- **Paint Intensity**: in Paint mode the **Intensity** slider (top bar, Draw palette, Quick Menu, U) sets how strongly
  the paint colours, for every stroke type, with or without an alpha.
- **Quad Remesh on pointed parts**: **Tool > Geometry > Quad Remesh** on a thin part with a sharp tip (a horn, a claw,
  a blade) no longer tangles along its length, and the quads now end on the point. Thanks Oslen on Discord for the
  report.
- **Starter meshes**: the Human Head's neck ends in a clean, even rim (no more two spikes under the jaw), and the
  Stylised Head, Bust, Torso, Hand and Ear have clean open edges too.
- **The tier card puts itself away**: **Got it**, its new close button, Esc or Enter dismiss the "Tuned for your
  machine" card on the first press, and it stays away, also on laptops with two graphics cards.
- **Tube and Insert let go**: picking any brush or tool (**Draw**, **Move**, **Scale**, **Rotate**), or pressing
  **Esc**, now ends **Draw Tube** and **Parts > Append > Insert on the model**; the status bar says how.
- **Pen pressure on Linux**: Wayland pens keep their pressure through the whole stroke, the eraser end works on
  Wayland, and **Edit > Preferences > Tablet > Troubleshooting details** shows what the pen sends, with **Copy
  details** for a bug report (and **Classic pen timing**, should pressure feel worse with your tablet).
- **Faster remeshing**: **Tool > Geometry > Quad Remesh** at the defaults is up to 2.3 times faster (a 1,000-triangle
  sculpt: 1.24 s before, 0.54 s now on our floor-tier laptop), and **Voxel Remesh** on a 10 million triangle sculpt
  takes 1.6 s instead of 4.0 s. The quality is the same. Thanks factoberplay on YouTube for the report.

## Version 0.11.16

```features
date: 2026-11-07
tagline: One gizmo for move, rotate and scale, symmetric cuts, and clearer labels.
highlight: Gizmo All: one gizmo with move, rotate and scale handles at once, on Shift+W (thanks fenix on Discord, @fenixpoints)
highlight: Cut with symmetry cuts both sides alike, and Blender keys get Ctrl+Shift cut shapes and Alt+Q to pick a Part (thanks dm4nd on Discord)
highlight: Split can keep the original: copy hidden, masked or unmasked areas into a new Part and leave the source as it is (thanks _tfx on Discord)
highlight: Tool > Geometry is reorganised by task (Levels, Micro Detail, Resolution, Cleanup, Symmetry) with clearer names, Extract reads Thickness, Edge Smooth, Both Sides, Corners and Border, and popups are fully opaque
highlight: macOS: SculptForge > Preferences... in the app menu opens Preferences (thanks @fenixpoints)
```

- **Gizmo, All**: **Shift+W** (or **All** beside Rotate) puts move, rotate and scale handles on one gizmo: arrows and
  squares inside the rings, boxes past them to stretch, four outer tabs to scale evenly.
- **Keep original** for **Tool > Parts > Split**: tick it and Hidden, Masked or Unmasked copy those triangles into a
  new Part, leaving the source Part as it is.
- **Cut** with symmetry cuts both sides alike, also across the middle, and its caps stay closed for the next cut.
- **Blender keys**: Ctrl+Shift hides and cuts again, and Ctrl+Shift+click on the brush tile (or Ctrl+Shift+B) offers
  the Cut shapes.
- **Extract labels in full**: **Tool > Parts > Extract** reads **Thickness**, **Edge Smooth**, **Both Sides**,
  **Corners** and **Border**, and in a narrow tray (a small window, 150% or 200%) the controls move onto their own
  rows instead of cutting words off.
- **Tool > Geometry, grouped by what you do**: **Levels** (the **Level** slider, **Lower / Higher Level**, **Add
  Level** with **Smooth**, **Rebuild Lower Levels**, then **Remove Below / Remove Above**), **Micro Detail**,
  **Resolution** (Decimate, Quad Remesh, Subdivide), **Cleanup** (**Fill Holes**, Merge by Distance, Find Problems) and
  **Symmetry** (**Make Symmetric**). Quad Remesh controls say what they do too (**Target Quads**, **50% / 100% /
  200%**, **Size Variation**, **Follow Face Groups**, **Keep Hard Edges**, **Guide Strength**), and **Remove Hidden**
  in Tool > Visibility. Shortcuts, shelves, scripts and saved files are unchanged, and the command search (F3) still
  finds each control by the words it had before.
- **macOS**: **SculptForge > Preferences...** in the app menu opens Preferences (it was greyed out).
- **Opaque popups**: menus, popups, tooltips and windows no longer let the interface behind them show through (it was
  easy to see at 200% scale), in every look and colour scheme.

## Version 0.11.15

```features
date: 2026-11-07
tagline: Micro sculpting fixed, a 3D tube pen, and tools for tidy Parts.
highlight: Fixed: big Clay strokes on Micro levels could break the surface into shards (since 0.11.10); Micro sculpting is back to 0.11.9's speed
highlight: Tube: draw clay tubes in the air like a 3D pen, then edit their points (thanks GABO on Discord)
highlight: Parts tools: split hidden, masked or unmasked areas into a new Part, merge points by distance, and Find Problems with a Fix for each problem (thanks foirgairwolf on Discord)
highlight: Hardware tiers and Turbo: SculptForge tunes itself to your computer, with a memory readout in the top bar (thanks Oslen on Discord)
highlight: Side shelves no longer scroll sideways (thanks dm4nd on Discord), and Frame fits the model into the free area of the canvas
```

- **Tube**: draw clay tubes in the air like a 3D pen. **Tool > Tube > Draw Tube**, then drag anywhere: on the model,
  off it, or out of it. Pen pressure thins the tube; drag its points afterwards to reshape it, then **Bake** to sculpt
  it, or draw straight into the active Part with **Merge** (even edges, ready for dynamic topology). Thanks to GABO on
  Discord for the idea.
- **Hardware tiers and Turbo**: SculptForge sorts your computer into a tier (Essential, Standard, High, Extreme) and
  switches on what it can carry: more Focus Detail memory, 16K textures. A card on first launch says what it found;
  the tier sits in the top bar. The new bolt (**View > Turbo**, Ctrl+Alt+T) trades antialiasing and detail while you
  sculpt and orbit for speed, and puts everything back the moment you stop.
- **Split > Hidden / Masked / Unmasked** (**Tool > Parts**): move the hidden, masked or unmasked triangles of a Part
  into a new Part just below it, in one undo step. Asked for by foirgairwolf on Discord.
- **Delete masked** (**Tool > Visibility**) deletes what is masked, as **Remove Hidden** does for what is hidden.
  Undoable.
- **Smooth or flat per Part, right in the list**: click the mark left of a Part's eye to go from the scene's shading
  to Smooth to Flat.
- **Merge by Distance** (**Tool > Geometry**): merge points closer than a distance, with a live count and the points
  shown on the model before you click. For unwelded imports. Asked for by foirgairwolf on Discord.
- **Find Problems** (**Tool > Geometry**): counts non-manifold edges, holes, duplicate, degenerate, zero-area and flipped
  triangles and isolated points, with a **Fix** per row and **Fix all**. Checking never changes the mesh.
- **Recent colours**: the last eight colours you painted with sit under the colour picker on the left shelf; click one
  to paint with it again. Thanks Oslen on Discord.
- **Frame** (F) fits the model into the part of the canvas the open Library and the Sculpt Timeline leave free, and
  the left shelf's brush tiles and colour picker shrink to fit short screens.
- **Pen view speed**: **Preferences > Tablet > View speed** sets how fast the pen turns, pans and zooms the view,
  apart from the mouse. Turn it down if the view spins too fast with a tablet. Thanks to GonzaloHernandez3D on Reddit.

## Version 0.11.14

```features
date: 2026-11-07
tagline: A deformation stack, a slimmer layout, and palettes that open on small screens.
highlight: Deformation stack: bend, twist, taper and more on a whole Part, with a live preview you can reorder and apply (thanks foirgairwolf on Discord)
highlight: Slimmer Library and timeline: about a third less height, with the timeline as one thin row by default (thanks factoberplay on YouTube)
highlight: Small screens: below 1280 px the side palettes open as drop-downs instead of not opening at all
highlight: Membership moments: a quiet heart in the top bar and an occasional thank-you at a milestone; nothing in the app is ever paywalled
```

- **Deformation stack** (requested by foirgairwolf on Discord): **Tool > Reshape > Stack** bends, twists, tapers,
  shears, turns, moves, scales, squashes, rounds, boxes, flattens, sags or puts depth on the whole Part, along any of
  X, Y and Z, with a live preview. Stack steps up, switch them on and off, repeat one, then **Apply** (one undo step),
  **Apply Selected** or **Reset**. Every step is a command too: `deform_stack ops="bend 45 y soft; twist 90 y"`.
- **Exact size** (requested by foirgairwolf on Discord): **Tool > Reshape > Size** shows the Part's width, height
  and depth in scene units; drag one or type an exact value to resize it, with **Uniform** keeping its proportions.
- **Membership moments**: the small membership card can now also come at a milestone (your 10th start, 10 hours of
  sculpting, your 100th save; counted on your computer only, never sent) and says the true number. After a third **Not
  now** it waits a month, after a sixth it only comes with an update. A quiet heart at the right of the top bar opens
  **Help > Support SculptForge...**; members see a small badge there instead.
- **Requested by a member**: changes members asked for through Member Requests are marked in What's New.
- **Slimmer Library and timeline**: the Library strip and the Sculpt Timeline under it take about a third less of the
  viewport's height. The timeline is one thin row (the full strip is one click away, or **Edit > Preferences >
  Interface > Compact timeline**), and you can drag the Library's edge to size its thumbnails. Thanks factoberplay on
  YouTube.

## Version 0.11.13

```features
date: 2026-11-07
tagline: Linked duplicates, an orbit lock, and cleaner Image to 3D.
highlight: Linked duplicates: sculpt one Part and every copy follows, each in its own place (thanks @ebarranco, #94)
highlight: Orbit lock: View > Orbit lock turns the view about X, Y or Z only (thanks foirgairwolf on Discord)
highlight: Image to 3D: remove the flat background plane a generated model stands on, in one click (thanks fenix on Discord)
highlight: Rebuild Lower Levels: lower levels keep their UVs, even where seams cross coarse faces (thanks @RodTDai, #145)
highlight: Timelapse: rendered timelapses no longer flicker between detail levels (thanks @ebarranco)
```

- **Remove flat background**: Image to 3D drops the flat plane a picture's background often turns into (a wall behind
  or a ground under the subject) before the model lands. On by default in **Generate 3D**; Undo brings a removed plane
  back. Thanks fenix on Discord.
- **Linked Duplicates**: **Tool > Parts > Linked Duplicate** makes a copy that shares the Part's mesh but sits in its
  own place. Sculpt, paint or mask any of them and all change; move, turn or scale each with the Gizmo. **Make
  Unique** gives one its own mesh again. Hundreds of copies cost about one mesh.
- **Orbit lock**: **View > Orbit lock** turns the view about one axis only: **Free**, **Y (turntable)**, **X** or
  **Z** (<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> / <kbd>Y</kbd> / <kbd>X</kbd> / <kbd>Z</kbd>). The axis gizmo
  shows which axis is locked. Thanks foirgairwolf on Discord for the idea.
- **Version at a glance**: the window title and the top bar show the SculptForge version, so a screenshot says which
  one you run. A beta's tag shows in the accent colour.
- **Rebuild Lower Levels** keeps UVs on the lower levels of a high-poly that was unwrapped on its own: no more faces
  stretched across UDIM tiles, even with overlapping islands.
- **Timelapse** videos of dense sculpts no longer flicker (**Edit > Export timelapse...**).
- The membership card now shows the Studio membership's flat price for any number of artists.

## Version 0.11.12

```features
date: 2026-11-07
tagline: Your names in your language, a cleaner Restart to update, and the cube is back.
highlight: Your languages: names you type in Chinese, Japanese or Korean (Parts, layers, folders, brush sets, files) draw with your system's fonts instead of "?"
highlight: Stencil: clean, sharp groove edges when you sculpt through a stencil, and detailed photos stay even on lighter meshes
highlight: Restart to update: on Linux the previous version now quits, and Discord stops showing SculptForge when you close it (thanks @ebarranco)
highlight: SpaceMouse: turning the cap slowly no longer zooms the view away (thanks @fenixpoints)
highlight: App icon: the outline cube is back, and a fix for garbled small icon sizes on macOS (thanks @tristratos, #156)
```

- Names you type in Chinese, Japanese or Korean (Parts, folders, layers, brush sets, files, Library items) draw with
  your system's fonts instead of **?**, in any interface language.
- After **Restart to update** on Linux, the previous version quits instead of staying hidden in the background (it
  kept showing SculptForge on Discord). Discord shows the outline cube icon.

## Version 0.11.11

```features
date: 2026-11-06
tagline: Sculpt through a reference image, a live link to Maya, and creature bones to build from.
highlight: Stencil: place a reference image over the model, then sculpt or paint through it, and move it and repeat (thanks @kenttumanda123-netizen, #155)
highlight: Maya Live: each stroke goes to Maya on its own, and Maya's edits come back the same way. An optional Live link makes it near-instant; it's off by default and warns when you turn it on (thanks @tristratos, #42)
highlight: Creature bones: 13 original starting pieces for building creatures, in Insert sets (thanks @kenttumanda123-netizen, #154)
highlight: Brush sets: swap the whole bottom shelf and its keys in one step (thanks @NicTanghe, #143)
highlight: Floor grid: View > Floor grid draws a faint grid on the floor the model stands on (thanks @fenixpoints, #115)
```

- **Brush sets**: save the bottom shelf and its brushes' keys as a named set (Wood, Metal, Stone...) and swap both in
  one step from **Sculpt > Brush > Brush set**, the shelf's right-click menu or <kbd>F3</kbd>. Asked for by
  @NicTanghe.
- **Creature bones**: **Append > Insert on the model > Creature bones** is a built-in set of original fantasy creature
  bones (skull, jaw, teeth, horns, spine, ribs, limb and wing bones, claws, tail): pick one and drag it onto your
  model, no file needed. Thanks @kenttumanda123-netizen (#154).
- **Floor grid**: **View > Floor grid** draws a faint grid on the floor the model stands on, for grounding,
  perspective and lining up Parts. Off by default.
- **Maya Live**: turn on **File > Maya > Live** (and **Live** on the Maya shelf) and each app sends what you change by
  itself, 300 ms after a stroke or edit, only the Parts that changed. Thanks @tristratos (#42).
- **Maya Live link**: **File > Maya > Live link...** connects SculptForge and Maya on this computer so Maya follows
  your stroke while you draw it. It asks every time, is off at every start, and the ember **Live link on** chip in the
  top bar turns it off.
- **Package managers**: the install guide lists where SculptForge is today: Homebrew available, winget and Omarchy in
  review, the AUR package ready to publish, apt and dnf repositories on the way. Thanks @NicTanghe (#149).
- **Stencil**: lay an image over the view and sculpt or paint through it (**Sculpt > Alpha > Stencil**). Move, scale
  and turn it on the canvas with **Adjust**, stroke over it, turn the model and repeat. Sculpt brushes take its
  brightness, Vertex Paint its colours; Tile repeats it across the view.
- **Dynamic topology** no longer shows a long spike and a patch of broken faces after zooming in and sculpting (only
  the display was wrong: Undo or the next strokes put it right) (thanks @editordenis4-collab, #153).
- **Big brushes on big meshes** follow the pen closer on 4-core machines and settle sooner after pen-up.

## Version 0.11.10

```features
date: 2026-11-06
tagline: Exports keep their real size, Alt+Q switches Parts, and insert sets for kitbashing.
highlight: Exports keep their real size: what you import in mm or cm comes back out the same size, for 3D printing and other apps (thanks @Swar55, @NicTanghe, #148)
highlight: Alt+Q sculpts the Part under the cursor, in every input style (thanks ziel9627 on Discord)
highlight: Insert sets: pick a piece from a multi-object file or folder by its thumbnail, step through them with [ and ]
highlight: Spanish and Chinese everywhere: the Generate 3D panel and the last untranslated labels now read in your language
highlight: Kitbash sets for Insert
```

- **Insert sets**: **Parts > Append > Insert on the model > Insert set from file...** or **from folder...** loads a
  kitbash set (a file with several objects, or a folder of meshes); pick one in its thumbnail window and drag it onto
  the model, `[` and `]` step through the set.
- **Sculpt the Part under the cursor**: point at another Part and press <kbd>Alt</kbd>+<kbd>Q</kbd> (as in Blender, in
  every input style): it becomes the Part you sculpt, and the view stays where it is.
- **Real-world size**: parts for 3D printing keep their size through every export. Pick the unit sizes show in (mm,
  cm, m, in) in **Edit > Preferences > Interface > Units**; **Tool** shows the Part's size and **File** the size
  exports write. OBJ and PLY files can be read in mm, cm, m or in (**OBJ/PLY in**). Asked for by @Swar55.
- **Spanish, Traditional Chinese and Simplified Chinese** now cover the whole interface: **Generate 3D** (tabs, services, hints, Quad Remesh,
  Generate 3D model, Quick shapes), the About box, Proof mode, the phone link, the brush gallery, Brush Lab, splat
  training and about 400 other labels that stayed in English.
- Fewer shards when sculpting rough micro-level surfaces (Clay strokes ending where hair or strands meet skin). Clay
  over rough strands can still leave small steps; a fuller fix is coming.
- **Paint with alphas**: vertex paint now follows the brush alpha with every stroke (Drag Stamp, Dots, Spray), so a
  Cracks stamp paints cracks instead of a soft round spot. Thanks @fxrtst.
- **Full detail after a new paint layer**: on dense meshes, adding a paint layer no longer leaves the top subdivision
  level looking unsubdivided until a restart.
- **SpaceMouse with a pen**: turning the cap while the pen hovers or rests on the model should no longer move the
  cursor or the model; tell us if it does. Thanks @fenixpoints.
- **Smoother view on big models**: the processor prepares the next frame while the graphics card draws this one, so
  turning the view and sculpting on large meshes run at up to twice the frame rate on some computers.

## Version 0.11.9

```features
date: 2026-11-06
tagline: Folders for your Parts, a Line Gradient for paint, and Auto UV that never freezes the app.
highlight: Folders in Tool > Parts: drag Parts in, fold, hide and move them together (thanks @ebarranco, #96)
highlight: Line Gradient: drag a colour ramp across the model (thanks @Devon0209, #99)
highlight: Auto UV runs in the background and can be cancelled
highlight: Paint colour stays visible while you orbit or paint dense models
highlight: Hyper3D Rodin, direct: generate a model from an image or a description with your own Hyper3D key (thanks @ElgoogUdiab, #123)
```

- **Line Gradient**: **Color > Line Gradient**, then drag across the model: the paint colour runs into a second colour
  (or fades out) along the line; drag its ends to adjust it. Thanks @Devon0209 (#99).
- **Part folders**: in **Tool > Parts**, group Parts in folders (drag them in and out), fold, rename, hide or move a
  whole folder at once.
- **Hyper3D Rodin, direct**: **Generate 3D > Hyper3D Rodin (direct)** runs Rodin Gen-2.5 on Hyper3D's own service with
  your own Hyper3D key, from an image or a description, in five tiers from Extreme Low to Extreme High. It stays off
  until you add your key, which is kept in the system keychain; your image or description goes to Hyper3D only, and
  generations are billed to your Hyper3D account. Rodin through fal.ai stays as it was.
- **Paint stays while the view moves**: on dense meshes, vertex paint, paint layers and the mask no longer vanish
  while you orbit or paint and pop back at rest.
- **Input style keys after a restart**: the Blender, Classic and Maya styles keep every key when SculptForge starts
  again (F for the Size slider in the Blender style, 9 for a Snapshot with the Classic keys).
- **Auto UV never freezes the app**: the `uv_auto` command from the console, a script or an AI tool now unwraps in the
  background like the **Tool > Texture Image > Auto UV** button, which gains a **Cancel** button beside its progress.
- **Faster Pack**: **UV Editor > Pack** across a UDIM grid takes well under a second on most models (it could freeze
  the app for half a minute), runs in the background with a progress bar and **Cancel**, and lays islands out tighter.
  Unwrap runs in the background too.

## Version 0.11.8

```features
date: 2026-11-06
tagline: Clearer corner axes, one-click Library, a new cube app icon, and pen stamps that keep their strength.
highlight: Clearer corner axes: lettered, depth-shaded, click twice for the opposite view
highlight: One click in the Library uses a brush, alpha or material (thanks @NicTanghe, #142)
highlight: The app icon is now an orange cube
highlight: Drag and Slide Stamp keep their strength when the pen lifts (#146)
highlight: One click sends to Maya and back; SculptForge picks up Maya's exports by itself (thanks @tristratos, #42)
```

- **Edit > Preferences...** in the Classic layout too: Preferences is its own palette there, and now Edit also opens
  it. The guide says where Preferences is in each layout (also <kbd>Ctrl</kbd>+<kbd>,</kbd> or F3 > Preferences).
- **New app icon**: an orange cube is the app icon in the taskbar, dock and title bar; it stays sharp at small sizes.
  The bust and anvil stays the SculptForge brand.
- **Library**: one click uses a brush, alpha or material. Double-click still works.
- **Maya bridge, fewer clicks**: what Maya sends loads by itself, **Send to Maya** is one click in the top bar, and
  the Maya shelf's new **Auto** loads what SculptForge sends. Thanks @tristratos.
- **Easier to read view axes**: the corner axes now show **X**, **Y** and **Z** in filled circles, with smaller rings
  for the opposite ends; the ends pointing away are smaller and fainter, on a soft disc that reads on dark and light
  backgrounds. Click an end for that view, click it again for the opposite view, drag on the disc to orbit; an exact
  view's name ("Front view", "Top view"...) shows under it.
- **Rebuild Lower Levels** works on open meshes such as UV-tiled panels, and the lower level keeps each face's UVs.
- **Drag Stamp with a pen**: a stamp (a vector displacement or any brush) dragged to size keeps its strength when you
  lift the pen, instead of going weak or vanishing.

## Version 0.11.7

```features
date: 2026-11-06
tagline: Dense scans orbit smoothly on entry-level machines, and Preferences > AI speaks Spanish and Chinese.
highlight: Dense scans orbit smoothly on entry-level machines
highlight: Preferences > AI in Spanish and Chinese
highlight: Live mode's confirmations in your language
```

- **Smoother view on very dense meshes**: a scan or import with more triangles than your computer draws comfortably
  now draws a lighter version of itself while you turn the view, and the real detail as soon as the view rests. Parts
  you sculpt always show in full detail. Orbiting a 28-million-triangle scan on an entry-level machine is about twice
  as smooth: its slowest frames go from 24 to 12 ms.

## Version 0.11.6

```features
date: 2026-11-06
tagline: Studio controls for AI: a managed policy, an audit log, keys in the system keychain and single sign-on, plus shelves that hide when they're empty.
highlight: Studio controls: one policy file decides which AI tools and providers are allowed
highlight: An audit log of every AI step, and API keys kept in the system keychain
highlight: Sign in with your studio's identity provider (device sign-in, no browser plug-in)
highlight: Empty shelves stay out of the way outside Customize Shelves
highlight: Studio-ready AI: policy, audit log, single sign-on
```

- **Empty shelves hide**: a shelf you empty is hidden outside **Customize Shelves** and the canvas takes its room; it
  comes back while customizing so you can drop buttons onto it again.
- **Studio controls**: IT can deploy a policy file that locks AI, network features, AI providers, AI tools and updates
  (**Managed by your studio** shows on locked settings), keep an audit log of every AI action, and sign artists in
  with the studio account (the guide's "SculptForge for studios" page, IT deployment).
- **AI providers**: **Edit > Preferences > AI > Provider** has presets for Anthropic, OpenAI, Ollama, LM Studio, vLLM,
  llama.cpp, a LiteLLM studio gateway and OpenRouter. Keys now live in your system keychain.

## Version 0.11.5

```features
date: 2026-11-06
tagline: Connecting your AI assistant now takes three steps, the interface speaks Simplified Chinese, and the brush size ring follows the pen.
highlight: Help > Connect an AI tool: three steps, copyable commands for your tool, example prompts
highlight: The interface in Simplified Chinese
highlight: The brush size ring previews on the mesh while you resize
highlight: Discord status clears when you quit and shows the new SF icon
highlight: Connect an AI tool in three steps
```

- **Help > Connect an AI tool** is now the one guide to working with an AI tool, on one screen: pick your tool and
  copy its command, turn on **Let AI tools control this window** right there, see what happens next (the 6-digit code,
  the **AI connected** badge, **AI:** steps in Undo, what asks you first), and copy an example prompt. The AI blocks
  out a starting point; you do the art.
- **The brush ring shows the new size while you set it**: with the **S** pop-up open, dragging or typing a size draws
  the ring at that size on the model, where you pressed S; **O** shows the Falloff ring the same way.
- **Simplified Chinese**: **Preferences > Interface > Language** has a Simplified Chinese button, with mainland terms
  and glyph shapes.
- **Paint menu**: no longer jitters on short screens (1280x720 fullscreen); a menu taller than the screen scrolls
  smoothly.

## Version 0.11.4

```features
date: 2026-11-05
tagline: Let your AI assistant sculpt in your open window, and fixes.
highlight: Live mode: an AI tool works in your open window, only after you approve it
highlight: Your AI tool sees the viewport, turns the view and adds strokes, and every step is one undo
highlight: Fixes: rare wrong shading after strokes
```

## Version 0.11.3

```features
date: 2026-11-05
tagline: Connect your AI tool in one window, saved camera views, a readable mask, solid remeshes of open parts and the new SF icon.
highlight: Connect your AI tool in one window
highlight: Saved camera views and a readable mask
highlight: Quick Remesh and Voxel Remesh keep open parts solid
highlight: The UV Editor fits at large UI scales
highlight: The new orange SF app icon
```

- The app icon is now the orange SF mark, as on the website; it reads at task bar size.
- **Help > Connect an AI tool**: use SculptForge from Claude, ChatGPT (Codex CLI), Gemini (Antigravity CLI), Grok
  (Grok Build), Cursor, VS Code and more. Pick your subscription; the window shows the exact command or settings for
  your install, with Copy buttons. Claude Desktop installs it from a bundle on each release.
- **Remeshing an open Part stays solid**: **Quick Remesh** and **Tool > Remesh > Voxel Remesh** cap open borders (the
  Stylised Head's neck, a cut) before remeshing, so the result is one closed surface, not a shell full of holes. A
  Part that can't be made solid stays as it is, and the status bar says why.
- **UV Editor at large UI scales**: the toolbar wraps on a 4K screen at 240 %, so **On model: Colours / Checker** and
  **Frame** are all visible and each takes its own click.
- **Dynamic on a subdivided model**: turning on **Dynamic** on a Part with subdivision levels now offers **Freeze
  Levels and Turn On Dynamic**: the level you see becomes one plain mesh (Ctrl+Z brings the levels back).
- **Saved views**: **View > Views** has Front, Back, Left, Right, Top and Bottom (Numpad 1, 3, 7; Ctrl for the other
  side) and **Save View** for your own named camera angles; Numpad 0 goes back to the last one. They are saved with
  the project.
- **A readable mask**: masked areas show a soft, dark tint instead of near-black, with **Mask darkness** in
  Preferences > Interface and Tool > Masking. Dense wireframes no longer turn into a dark moire.
- **Easier to see**: options that are on stand out in every colour scheme, and Quad Remesh guide curves draw smooth
  with an outline.
- **Safer levels**: in Tool > Geometry, **Add Level** and **Subdivide (removes levels)** no longer sit
  together, Micro has its own group, and Subdivide, Remove Below and Remove Above ask before removing levels (with
  **Don't ask again** for Subdivide).

## Version 0.11.2

```features
date: 2026-11-05
tagline: Faster pen-up on dense scans, clean creases on micro levels, moves that go into sculpt layers, Maya style view keys and a UDIM grid.
theme: Cleaner detail, smarter layers.
hero: images/wn-0112-hero.jpg
highlight: Big brushes on dense scans finish sooner after you lift the pen
highlight: Clean Sharp Crease, Crease and Pinch on micro levels
highlight: Move, Scale and Rotate into a sculpt layer
highlight: Maya style view keys, and UDIMs in Columns x Rows
highlight: Traditional Chinese, and a performance trace for slow moments
title: Clean creases on micro levels
line: Crease and Pinch cut clean grooves on a micro level, with no noise around them.
image: images/wn-0112-clean-creases-on-micro-level.jpg
icon: zoom-in
action: guide:micro-levels.md#sculpting
title: Transforms into a layer
line: With a sculpt layer active, Move, Scale and Rotate go into it; its weight dials them.
image: images/wn-0112-transforms-into-a-layer.jpg
icon: layers
action: guide:layers.md#add-a-layer-and-sculpt-into-it
title: Turn your VDM stamps
line: Stamps follow the drag, keep a fixed angle, or take a random turn each time.
image: images/wn-0112-turn-your-vdm-stamps.jpg
icon: stamp
action: guide:sculpting.md#stamp-rotation
title: UDIMs in Columns x Rows
line: UV Editor > Pack fills a grid of UDIM tiles: 10 x 2 fills 1001 to 1020.
image: images/wn-0112-udims-in-columns-x-rows.jpg
icon: grid-3x3
action: ui.uv_editor
```

- **Faster pen-up on dense scans**: with a micro level, a big brush stroke on a dense scan finishes sooner after you
  lift the pen.
- **Smoother orbit after big strokes**: orbiting a dense scan with a micro level right after big strokes is smoother
  on slower computers; the shading catches up while the view moves and finishes when it stops.
- **Transforms into a sculpt layer**: with a layer active in **Tool > Layers**, Move, Scale and Rotate (the Transform
  gizmo, masked or not) record into that layer like a stroke: the base mesh stays, the layer's weight dials the move
  in and out. With **Base mesh** active they move the base and every layer, as before. Thanks Devon0209 (#133).
- **Rotation for VDM stamps**: with the VDM brush, **Brush > Vector Displacement > Stamp rotation** has **Follow
  stroke direction**, **Angle** and **Angle jitter** (a random turn per stamp), kept per brush. Brush files keep the
  alpha's Angle and Angle jitter too, so shared brushes turn their alpha the same way. Thanks DonnieAlex (#136).
- **Maya style view keys**: hold **Alt**, then add **Ctrl**, and a left drag rolls the view; **Alt+F** frames the
  active Part and **Alt+H** resets the view. **Edit > Preferences > Input > Wheel zooms toward the pointer** keeps the
  point under the pointer in place (on with the Maya style, a checkbox in the others). Thanks RodTDai (#134).
- **UDIMs in Columns x Rows**: **UV Editor > Pack** has **Columns** and **Rows** (1-10 each) instead of a tile count,
  so a 10 x 2 layout fills 1001-1020 and 4 x 3 stays four tiles wide. Thanks RodTDai (#135).
- **Benchmark on slower computers**: **Help > Benchmark** no longer stops at "settling" when the scene takes a few
  seconds to open.
- New icon and logo.
- **Spanish and Traditional Chinese, complete**: every menu, panel, tooltip and Ctrl-help text is now translated in
  both languages (UV Editor, Quick Remesh, Preferences > Input, Packs, Bake and the rest).
- **Record a performance trace** (Help menu, #127): turn it on, do what feels slow, turn it off and attach
  sculptforge-trace.log (Help > Open log folder) to your report.
- **Traditional Chinese**: **Preferences > Interface > Language** has Traditional Chinese: it puts the menus,
  palettes, shelves and messages in Traditional Chinese (thanks yoyomaimai). Brush and tool names stay in English.
- **Clean creases on micro levels**: **Sharp Crease**, **Crease** and **Pinch** at full strength no longer leave
  random noise on a micro level. The title bar counts the micro triangles beside the triangles, and **Wire** shows the
  level below the micro level on the micro surface.
- **AI off means off in the menus too**: with **Edit > Preferences > AI > Enable AI features** off, the Mesh menu's
  section is **From Image** (Relief, Inflate, Stamp) instead of Generate 3D.
- **Voxel Remesh up to 512**: the **Tool > Remesh** Resolution slider now goes from 32 to 512, as the `remesh` command
  does.
- **A pen resting on the tablet no longer blocks the mouse or trackpad**: lay the pen down and the cursor, the shelf
  buttons (the mirror's X too) and the menus answer the mouse at once.

## Version 0.11.1

```features
date: 2026-11-05
tagline: Dense scans with micro levels keep up with the pen, and one-click updates on Windows and Linux.
theme: Dense scans keep up.
hero: images/wn-0111-hero.jpg
highlight: Dense scans keep up: orbit after a big stroke on a sculpted 112M scan, under 1 to 33 fps
highlight: One-click update, signed and checked
highlight: Remesh with one key, or after each stroke
highlight: Preferences save themselves
title: Dense scans, fast
line: Orbit a sculpted 112M scan right after a big stroke on an RTX 3060: under 1 to 33 fps.
image: images/wn-0111-dense-scans-fast.jpg
icon: gauge
stat: 33 fps
action: guide:performance.md#working-with-big-meshes
```

- **Organise your Lit Spheres**: right-click one of your own in the material picker, **Render > Materials** or the
  Library and choose **Rename...** or **Move to folder**; sub-folders of the Lit Spheres folder show as headings. Drop
  an image onto the Library's Material tab to load it. Search for "material capture" finds them too.
- **Founding Members and Member Requests**: the first 100 members are **Founding Members** for good (a badge, their
  name in the credits, their price locked for as long as they stay), and each month every member can ask for a brush,
  an alpha or a material in **#member-requests** on Discord: we make it, credited to them. A small membership card
  shows now and then at a good moment (never mid-stroke, at most once a week); members sign in once with **I'm already
  a member** and it never shows again.
- **Membership, simplified**: two memberships instead of four. **Member** ($5 a month or $48 a year) gets betas and
  early builds, the monthly feature vote, Member Requests, #members and a name in the credits; **Studio** ($150 a
  month) covers up to 10 artists, with priority support and a private channel. Help > Support SculptForge... shows
  both. Every release stays free for everyone.
- **One-click update**: on the Linux AppImage and on Windows (installer or portable zip) the update banner has an
  **Update** button: it downloads the new version in the background, checks its signature and checksum, and installs
  it on **Restart now** or when you quit (**Later**). The old version is kept until the new one has started, and put
  back if it doesn't. Package-manager installs show what to run instead.
- **Member builds in the app**: members sign in with GitHub in **Edit > Preferences > Updates > Member builds** (a
  code to enter on github.com/login/device) and get the early builds through the same Update button.
- **Big meshes are faster: a one-time tip**: the first time you open a scene over about 20 million triangles, a note
  along the bottom of the view says what got faster and names the two settings that help most on big meshes
  (**Preferences > Performance > Background Preview** and **View resolution**), with a button that opens them. It
  shows once and never again.
- **Pen pressure per brush**: **Brush > Pen pressure** sets Size and Strength to **Use global**, **On** or **Off** for
  the brush you hold (also in Brush Lab and **Edit > Preferences > Tablet**). Grab brushes start with Size off.
- **Quick Remesh**: turn it on in **Tool > Remesh**, then press **Shift+R**, or hold **N** and drag on the background,
  to remesh the active Part in the background while you keep sculpting. **Remesh after each stroke** does it by itself
  when a stroke stretched the surface.
- **Rodin 2.5 in Generate 3D**: **Generate 3D > fal.ai > Model** now offers Hyper3D **Rodin 2.5** and **Rodin 2.5
  Fast**, from an image or a description, with your fal.ai key.
- **Studio mode** for studio builds: no outside connections, updates only from the studio's own channel.
- **Touch and trackpad navigation**: two fingers pinch to zoom and twist to roll the view; the SpaceMouse holds still
  while you sculpt.
- **Preferences save themselves**: key style, shortcuts, tablet, interface, performance and radial menus are kept a
  moment after every change, with nothing to press. The layout (shelves, trays) is kept with **Edit > Preferences >
  Saving & Layouts > Save Layout**. Thanks fenixpoints (#117).
- **I is Falloff** with SculptForge keys (U stays Intensity); the Classic sculpting keys are unchanged.

## Version 0.11.0

```features
date: 2026-11-04
tagline: Sixty new brushes in six packs, a Masking panel, a quick tour for experienced sculptors, and experimental options for very dense meshes.
highlight: Sixty new brushes in six built-in packs
highlight: Mask by peaks, colour and facing in a new Masking panel
highlight: A quick tour for experienced sculptors
highlight: Experimental: Live Move and Background Preview for very dense meshes
highlight: Show SculptForge on Discord (opt-in)
```

- **Fast Big Brushes (Experimental)**: a new way to place big brushes on dense meshes, off by default. Try it on a
  heavy model with a big Clay or Standard brush and tell us how it feels: **Preferences > Performance > Fast Big
  Brushes (Experimental)**.
- **Brush packs**: sixty new built-in brushes in six packs (Clay & Form, Hard Surface, Organic & Skin, Cloth & Folds,
  Damage & Wear, Ornament), each with its own alpha and a one-line description; see [Brush packs](brush-packs.md).
- **A-Z everywhere**: brushes, alphas, materials and the Library list A-Z; **Sort** switches to the most recently used
  first or (brushes, alphas) by pack. Alphas get a filter field.
- **Masking panel**: **Tool > Masking** is one panel of buttons with pictures: the Shapes, **Mask by** (cavity, peaks,
  face group, colour, facing, border) and **Adjust** (grow, shrink, sharpen, blur, invert, clear, all).
- **Quick tour**: **Help > Quick tour** (or **Sculpted before? Take the quick tour** in the welcome) shows in ten
  short slides where remeshing, dynamic topology, quad retopology, levels, masking, cut, brushes and baking are, each
  with a **Show me** button that opens the panel. The guide has it too: Quick tour for experienced sculptors.
- **Live Move (experimental)**: **Preferences > Performance > Live Move**. The graphics card draws the Move brush's
  drag from where the surface started, so it keeps up with the pen on any mesh size, and the move is applied once,
  exactly, when you lift the pen ([Performance](performance.md)).
- **Background Preview (experimental)**: **Preferences > Performance > Background Preview**. On dense meshes, the dabs
  a big brush is still working out show as a preview under the pen, so the stroke no longer trails behind it. The
  finished sculpt is the same either way.
- **Fast Upload (experimental)**: **Preferences > Performance > Fast Upload**. Sculpted changes reach the graphics
  card through shared memory and a copy on the card, instead of a copy by the driver during the frame.
- **Pattern brushes on micro levels**: Dragon Scales, Skin Pores, Stone Carve and the other pattern brushes keep
  their pattern on a micro level, and carving ones (Wrinkle) carve.
- **Cmd+, opens Preferences** (Ctrl+, on Windows and Linux).
- **Show SculptForge on Discord**: **Edit > Preferences > Integrations** lets your Discord friends see you're
  sculpting, with the elapsed time and, if you like, the monthly challenge's theme or the file name. Off by default.
