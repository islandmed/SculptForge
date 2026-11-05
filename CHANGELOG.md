# Changelog

All notable changes to SculptForge are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- Orbiting a dense scan with a micro level right after big strokes is smoother on slower computers: the shading
  that catches up after the strokes now takes a few milliseconds a frame while the view moves, and the rest when it
  stops (Lucy 112M on the low-end profile: 13 to 19 frames a second).
- Move, Scale and Rotate (the Transform gizmo and the `transform` command) with a sculpt layer active now record into
  that layer, masked or not, like a stroke (#133, thanks Devon0209): the base mesh stays, the layer's weight dials
  the move in and out, undo works. With Base mesh active they move the base and every layer, as before.
### Added
- **Maya style view keys** (#134, thanks RodTDai): hold Alt, then add Ctrl, and a left drag rolls the view (left /
  right); Alt+F frames the active Part; Alt+H resets the view (front, upright, perspective, everything in view). Ctrl
  first (or both together) still masks with Ctrl+Alt, on the model and beside it.
- **Edit > Preferences > Input > Wheel zooms toward the pointer** (#134): the wheel zooms toward the point of the model
  under the pointer, which stays put. On with the Maya style; the other styles keep zooming toward the middle (the box
  turns it on in any style). **Reset view** is also an action in the Controls window, for a key of your own.

## [0.11.1] - 2026-11-05

### Added
- **Rename and organise your Lit Spheres** (#124, thanks Pumaman101): **Rename...** and **Move to folder** (existing
  folders, **New folder...**) in the right-click menu of the material picker, **Render > Materials** and the Library's
  Material tab rename or move the image in the Lit Spheres folder; a name in use is refused, nothing is overwritten,
  and projects that use a renamed one still find it (by its pixels). Sub-folders of the Lit Spheres folder (one level)
  are headings in all three lists. Pack, studio-folder and project-only Lit Spheres show the items greyed out with the
  reason. Scripts: `rename_litsphere`, `move_litsphere`.
- Dropping an image onto the Library's Material tab loads it as a Lit Sphere (#124); dropped on the canvas it does
  what it did before.
- **Founding Members**: the first 100 members keep a Founding Member badge, their name in the credits (Help > About,
  the website's Members page) and their price for good; the membership page counts the places left
  (website/founding.json).
- **One-click update** (the update banner's **Update** button) for the Linux AppImage and the Windows installer and
  portable zip: background download with progress and Cancel, then Restart now or Later (installs at quit). Files come
  only from SculptForge's GitHub releases (every redirect checked, TLS verified); the release's SHA256SUMS.txt must
  carry an Ed25519 signature under the key built into the app, and the package must match its checksum, before
  anything is written in place or run. The AppImage is replaced in one atomic rename with the old file kept as `.old`
  until the new version has started; a new version that doesn't start is rolled back. Flatpak, .deb/.rpm, AUR and
  Homebrew installs are never replaced (the banner names the command or says to download). macOS keeps the Download
  button for now. See the guide's Installing > Updating.
- **Member builds** (Preferences > Updates): GitHub sign-in with the device flow for the "SculptForge Member Builds"
  app, which can only read the member repository's releases; the token lives in the OS keychain (Windows Credential
  Manager, Secret Service on Linux; macOS later), never in settings or logs, and Sign out removes it. The update check
  then also lists the member releases; they are verified exactly like public ones.
- **Big-mesh tip**: shown once, the first time a scene holds more than about 20 million triangles (micro levels
  counted): what got faster on dense meshes and the settings that matter (Background Preview, View resolution), with
  **Performance settings** and **Got it**. Never during a stroke, a benchmark, the welcome, What's New or the quick
  tour; stored in project.json as soon as it shows, so it never comes back.
- **Pen pressure per brush** (#122, requested by Zoldyck-Kill): every brush has its own **Pen pressure: Size** and
  **Strength** switches, **Use global** / **On** / **Off**, in **Brush > Pen pressure**, Brush Lab and **Edit >
  Preferences > Tablet**. **Use global** follows Pressure > Size / Strength as before; **On** and **Off** override
  them for that brush. Kept per brush with its other settings (**Save Brush Settings** keeps them, **Reset Brush to
  Default** puts back the default) and in a custom brush's `.sfbrush`. Move, KelvinGrab, KelvinTwist, KelvinScale,
  Tendril and Elastic Tendril start with Size off; older saved settings load as Use global. Command:
  `brush_pen_pressure size=on|off|global strength=on|off|global`.
- **Quick Remesh** (Tool > Remesh, #121, thanks Zoldyck-Kill): with it on, Shift+R, or N held while clicking or
  dragging on the background, remeshes the active Part at the Voxel Remesh resolution in the background (progress bar
  and Cancel as the button; Keep Masked and Keep Texture apply). One undo step per remesh; a stroke or undo made while
  it runs is never overwritten (it remeshes again). Both keys are free in every key style and change in Preferences >
  Hotkeys; on the model a press stays a brush stroke.
- **Remesh after each stroke** (Tool > Remesh, off by default, #121): after pen-up, a stroke that stretched edges past
  1.6 times their length remeshes the active Part in the background; sculpting never waits for it. Not on Parts with
  subdivision levels, sculpt layers or a painted texture.
- **Rodin 2.5 (Hyper3D) via fal.ai** (#123, thanks ElgoogUdiab): **Generate 3D > fal.ai > Model** lists **Rodin 2.5**
  and **Rodin 2.5 Fast**. Image and text both work; the untextured GLB goes through the usual import and remesh. Uses
  your fal.ai key (`FAL_KEY`), nothing else is contacted. Commands: `image_to_3d service=fal
  model=fal-ai/hyper3d/rodin/v2.5`, `text_to_3d service=fal model=fal-ai/hyper3d/rodin/v2.5` (other fal models keep
  Hunyuan3D 3.1 Rapid for text).
- **Studio mode**: `--studio studio.json` (or a studio build) starts SculptForge with no outside connections: no
  public update check, AI services, community gallery or Discord presence, and network sockets only on this computer.
  Only the studio's own update channel or mirror is reached. `--offline` (or `SF_OFFLINE=1`) does the same with
  nothing allowed. See the user guide's "Offline deployment".
- **Touch screens and trackpads**: two fingers over the model pinch to zoom and twist to roll the view, and on a touch
  screen slide to pan; they never sculpt. One finger on a touch screen still sculpts. Thanks to phoenix (Discord) for
  the report.
- **Hold during strokes** (Preferences > Input > SpaceMouse, on by default): while you sculpt, paint or mask, the
  SpaceMouse leaves the view alone and its buttons do nothing, so a hand on the cap never moves the model under the
  pen or mouse. Pointer events from a SpaceMouse that the system also lists as a mouse are ignored. Thanks to phoenix
  (Discord).

### Changed
- **Faster builds from source**: on Linux the build links with mold when it is installed and precompiles the standard
  library and glm headers; app code the tests also run is a static library both link, so it compiles once. Packagers:
  `-DSCULPTFORGE_MOLD=OFF` keeps the system linker (the release packages do), `-DSCULPTFORGE_PCH=OFF` turns the
  precompiled headers off. See docs/build-speed.md.
- The command search and the Library filter also find Lit Spheres by "material capture" and "material image".
- Membership: **Member Requests** (one request per member per month: brushes, alphas, materials, small tweaks;
  "Requested by @name" on each item; out to members at month end, free for everyone a month later) replace the monthly
  brush and alpha pack, on the website, in the app, the guide and the README.
- The membership card: the full card shows on the first start only (and from Help > Support); after that a small card
  along the bottom of the canvas comes at a good moment (about 20 minutes of active sculpting in a session, a Save
  Render, the first start after an update, the monthly challenge's results), at most once a week, never mid-stroke or
  during a job. **Become a member**, **Not now** (a week) and **I'm already a member**, which signs in with GitHub
  like member builds: a member never sees it again (and gets member builds); no sign-in hides it for three months.
  Signed-in members never see it. The 10-hour reminder and the old "Maybe later" are gone.
- Memberships: Member ($5 / month or $48 / year) and Studio ($150 / month, up to 10 artists) replace Supporter,
  Insider, Studio and the one-time payment, on the website, in the support card, the Quick tour, the welcome note, the
  update banner ("Members get betas early") and the guide.
- Betas are a member perk: `X.Y.Z-beta.N` is published in the private members' repository and posted in
- **Release packages on staging**: the merge batch builds the release packages on the `staging` branch beside CI, and
  main reuses them. A release batch carries the version bump, so the release promotes packages that were already built
  and tested for the exact tagged commit: tag to published takes about 5 minutes instead of about 25.
- **Studio membership**: $300 a month for up to 10 artists (+$25 per extra artist; Founding Studio $150 for the first
  10 studios), now with the studio's own isolated build, a pinned version with security fixes, an offline signed
  installer and up to 3 custom items a month.
- **Testing flow** (docs/testing-flow.md): each change is tested once per tier instead of four times. T0 is
  `tools/test/impacted.py`, the tests a change can break, from the build graph. T1 is the whole suite once per merge
  batch on a GPU machine, in parallel with T2, CI on the `staging` branch; main fast-forwards only when both pass, and
  CI on main reuses that run. T3 is a nightly label and workflow for benchmarks and heavy meshes. `tools/test/run.py`
  wraps ctest with a content-hash test cache, one rerun for flaky tests (logged in tests/flaky.md), and a 20-second
  budget per test. A release bump that only adds paperwork reuses CI instead of waiting for it again.
- **Preferences save themselves** (#117, thanks fenixpoints): the key style, shortcuts, tablet, interface, performance
  and other preferences, and the radial menus, are written to ui.json half a second after they change (and at quit),
  through a temporary file. The layout is kept only by the new **Save Layout** (Edit > Preferences > Config). The old
  save command and its Ctrl+Shift+I key are gone; keymaps and macros that name its id (`config.store`) still load, as
  **Save Preferences Now** in the command palette.
- **SculptForge keys: I is Falloff** (as O); U stays Intensity. The Classic sculpting keys keep S size, U intensity, I
  colour intensity, O falloff (#117).
- **Plain names** for controls that carried another application's labels: Size, Intensity, Colour intensity, Falloff,
  Ctrl+hover help, Help box opacity, Customize Shelves, Reset Layout and Shortcuts, Revert to Saved Layout, Export /
  Import Interface, Lower / Higher Level, Delete Lower / Higher, Project Detail, Lasso Mask, Rectangle Mask, Radial
  Copies and Group from Mask. Script commands, their parameters and saved files are unchanged.

### Fixed
- Pinch and two-finger rotate did nothing on macOS trackpads and touch screens. Thanks to phoenix (Discord).
- **Big brushes on scans with micro detail**: on a micro level over a dense scan, a large Standard, Clay or Inflate
  brush now moves the base surface the way it does on a plain mesh, and the micro detail rides along untouched (one
  undo step, as before). A big Clay stroke there could freeze the app for seconds, throw spikes hundreds of units long
  and coarsen the whole level's detail; orbiting after such strokes could drop below one frame a second. Masks, hidden
  faces and alphas keep the detail brush.
- Orbiting a dense model right after sculpting no longer hitches on slower processors. The lighter copy that is drawn
  while the view moves was rebuilt in batches that a slow CPU took about 160 ms to finish, so the first frames of the
  orbit froze. Batches are now sized to fit a few milliseconds a frame. After a stroke the copy is also brought up to
  date while the view is still, so the next orbit starts with it ready (15M triangles, simulated integrated-graphics
  profile on slow cores: worst orbit frame 166 -> 24 ms).

### Security
- Release checksums (SHA256SUMS.txt) are now signed with Ed25519 (SHA256SUMS.txt.sig, a 64-byte raw signature); the
  release workflow checks the signing secret against the key compiled into the app.

## [0.11.0] - 2026-11-04

### Added
- **Fast Big Brushes (Experimental)** (Preferences > Performance, off by default): a dab that reaches more than
  150,000 vertices takes its plane from an even sample of them and turns the shading along with the move it makes
  (from the brush's own slope, no neighbour walks), and the dabs waiting for a frame go through the vertices together
  in one pass, so the normals, the picking structure and the GPU copy are updated once a frame instead of once a dab.
  Every vertex still moves by the brush itself; the exact shading follows behind the brush and when the pen lifts.
  Standard, Clay, Flatten, Crease, Pinch and Sharp Crease; Trim takes the sampled plane and shading but not the fused
  pass. Small brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas are placed dab by dab as
  before. The result matches the dab-by-dab stroke to within about 2% of how far it moved the surface
  (tests/test_beta_boost.cpp).
- **Brush packs**: 60 built-in brushes in six packs (Clay & Form, Hard Surface, Organic & Skin, Cloth & Folds, Damage
  & Wear, Ornament), made from SculptForge's own recipe engine and procedural alphas. A pack brush picked the first
  time brings its alpha (and turns it along the stroke where that helps). Guide chapter [Brush
  packs](docs/guide/brush-packs.md).
- Six new procedural alphas: **Cobblestone**, **Leather Grain**, **Hex Plates**, **Knit**, **Bolt** and **Chips** (21
  built-in alphas in all).
- **Sort** menu on the brush grid and list, the Alpha palette, the Material picker and the Library: A-Z (the default),
  Recent, and Pack (brushes, alphas). Saved in the preferences. The Alpha palette gets a filter field.
- **Mask by peaks**, **Mask by colour** and **Mask by facing** (`mask_by_peaks`, `mask_by_color`, `mask_by_facing`),
  and `brush_use name=...` to pick a library or pack brush from scripts and macros.
- Brush recipes (.sfbrush, brush codes) may name a built-in `alpha` and `alphaFollow`; older files are unchanged.
- **Quick tour** (Help > Quick tour, the welcome, and once when the Classic sculpting keys are picked): ten slides
  with pictures of SculptForge; **Show me** opens and flashes the panel while the tour waits in a bar at the bottom.
  The last slide is about memberships; **Maybe later** closes it for good. Guide chapter **Quick tour for experienced
  sculptors** (replaces the old comparison chapter).
- Experimental toggles in Preferences > Performance, all off by default. **Live Move**: the GPU draws a Move drag from
  the grab's start positions and the whole move is applied once at pen lift (one grab, equal to the per-frame grabs to
  1e-6). It falls back to the normal Move with masks, hidden faces, sculpt layers, micro levels, Connected Only or
  View falloff (`SF_LIVE_MOVE=0/1`). **Background Preview**: the predictive preview is drawn for background dabs
  (meshes of 2M+ vertices), and the frame no longer waits for a batch; the solver's dabs are unchanged
  (`SF_BG_GHOST=0/1`). **Fast Upload**: stroke uploads are packed into persistently mapped staging buffers
  (GL_ARB_buffer_storage) and copied on the GPU (`SF_PERSISTENT_UPLOAD=0/1`). The research behind them, including a
  design for focus detail on billion-polygon assets, is in docs/design/perf-moonshots.md.
- **Cmd+, / Ctrl+, opens Preferences** (#111, suggested by fenixpoints).
- **Show SculptForge on Discord** (Preferences > Integrations, off by default; idea by ebarranco): Discord shows
  SculptForge, its icon and the elapsed time; optionally "Monthly challenge: <theme>" (typed, 64 characters at most)
  and the file's name (never its folder). Nothing else is sent. It talks only to the Discord app on the same computer
  through Discord's local connection (no network connection, no port, no sign-in), sends its status and nothing else,
  and ignores whatever Discord sends back. See the guide's "Show SculptForge on Discord".

### Changed
- **Tool > Masking** is one panel of buttons with pictures and a hint each: Shapes, Mask by and Adjust, with their
  options under **Mask by options**.
- Brushes, alphas, materials and Library files list A-Z by default (alphas and materials were in a fixed order;
  Library projects were newest first, which is still one click away under **Sort: Recent**).
- The command search (F3) finds features by plain sculpting words: remesh, voxel, retopo, quads, multires, groups,
  clip, trim, slice, folders, insert, kitbash, vertex color ("Quad Remesh (also: retopo)").
- Scripts, commands and file names use SculptForge's own names throughout (`part_*`, `forgesphere_*`, `bridge_*`,
  `facegroup_*`, `load_litsphere`, `library_open`, `layout=classic`, `input_style classic`, the `--facegroups` flag,
  the `SCULPTFORGE_LITSPHERE_PATH` variable). Scripts, macros, keymaps, radial menus, packs, projects and preferences
  from older versions still load: their old names are read once and mapped to the new ones. The old command-line flag
  and environment variable names are no longer read.
- **Micro levels on slower computers**: while a stroke runs, the shading of what it changed takes at most about 10 ms
  a frame (measured on the computer), so big strokes keep up with the pen; the rest of the shading follows right
  after, as before.

### Removed
- The library export plug-in for another sculpting app and opening its library files through it. A folder of numbered
  Bridge files still opens as one scene.

### Fixed
- **Pattern brushes on micro levels** (#120, reported by RodTDai): a brush recipe on a micro level was a plain
  Standard dab, without its surface pattern (Dragon Scales, Skin Pores, Cracks...) and building up even when the
  recipe carves. It now keeps the pattern and its depth's sign there; planes, pinch and drag still don't apply to
  micro levels.
- **Windows portable zip starts on a fresh PC** (#109, reported by git-tiger): the Visual C++ runtime (msvcp140,
  vcruntime140, vcruntime140_1) now ships next to sculptforge.exe in the zip and the installer; before, a PC without
  the Visual C++ Redistributable couldn't start it. When SculptForge can't start (no OpenGL 3.3, an old graphics
  driver) it now says why in a message box, with the log's path, instead of quietly exiting.
- **Move with Pressure > Size on a pen** (#122, reported by Zoldyck-Kill): Move and the Kelvinlet grab brushes set the
  area they grab at the press, when a pen barely touches, so with Pressure > Size on they grabbed only a few percent
  of the brush and seemed not to work. They now always grab with the full brush size; Pressure > Strength still
  applies.

## [0.10.11] - 2026-11-03

### Added
- **Update channel** (Preferences > Updates): Stable or Preview. Preview is also offered the betas, which the update
  feed lists under `channels.preview`. It is never offered anything older than the stable release. Betas are GitHub
  pre-releases (`vX.Y.Z-beta.N`). They show "X.Y.Z Beta N" in the title bar and About.

### Changed
- Version comparison in the update check: a prerelease sorts before its version (0.11.0-beta.1 < 0.11.0), and betas
  compare by number.
- **Voxel Remesh** is 3-4 times faster with the same result (Discord, ThinkingPolygons and GABO): every stage runs on
  all cores, the distance to the old surface is only computed in a narrow band around it, and the flood fills work a
  run at a time. A 500,000-triangle sculpt at the default resolution 200 takes about 0.3 s instead of 1.1 s; 2 million
  triangles about 0.6 s instead of 2 s; resolution 400 about 1.1 s instead of 6 s (10-core laptop).
- SculptForge now uses only its own names in the app, guide, website and listings: **Classic sculpting keys** and the
  **Classic layout** (Preferences, first-start welcome), **File > Bridge** (Send, Receive, Import / Export Bridge
  file), Lit Spheres, Parts, Face groups, Vertex Paint, Library. Brushes are stored as SharpCrease, ClayBlock and
  Tendril (old names in projects, brush files, scripts and configs still load); the library's lit-sphere folder is now
  `Documents/SculptForge/Lit Spheres` (an existing folder moves over once). A new check (`tools/check_marks.py`, ctest
  `marks`) keeps third-party product names out of what we publish.

### Fixed
- Windows at 150-200 % display scaling (#105, a 2560x1600 laptop): the window opened at its 100 % size, 800-1067
  logical px wide, and the shelves and status bar ran past its right edge. The window now opens inside the screen's
  usable area at the display's scale (maximised when that doesn't fit), its title bar on screen, and keeps its logical
  size when the scale changes; the status bar drops its readouts before they reach the menus.
- The right tray stayed closed and wouldn't open on screens under 1920 px with Forge Terminal, Phosphor or Blueprint
  (#107, thanks ebarranco): the tiled looks' gaps made a 1280 px window count as too narrow for a tray.
- The QuickMenu (right click) squashed its brush, alpha and material thumbnails and had a blue tint in the Neutral
  look (thanks ThinkingPolygons): the thumbnails are square and uncropped, the background the look's grey.
- Voxel Remesh no longer freezes the app: it always runs in the background with a progress bar and Cancel (before,
  only when keeping a painted texture). Smoothing a remeshed surface and carrying mask and colour over also run in
  parallel.

## [0.10.10] - 2026-11-03

### Changed
- "Your first sculpt in 10 minutes" makes a cute owl head instead of the mushroom house: ear tufts pulled with
  Move, face discs dug with Alt+Clay, a beak, Divide three times, inflated eyes and Clay brows, then brown, cream,
  amber and orange Vertex Paint and a PBR render. Every step sets the brush's size (brushes keep their own since
  0.10.9). New step pictures and What's New card, made from tests/ui/first_sculpt.sfui and a 2x Save Render.
- Faster CI for branches and pull requests: the VFX Reference Platform and Blender add-on checks run on main, in the
  merge queue and on request (label `full-ci`, or a dispatch with full=true); Windows tests run on 4 runners; CMake's
  configure checks are cached; old compiler caches are pruned so main's stay warm (prune-caches.yml); docs-only
  changes skip the build; every test job lists its slowest tests in the job summary.
- Release notes come from one note fragment per branch, `changes/<branch>.md` (changes/README.md);
  `scripts/release.sh` step bump assembles them into CHANGELOG.md and What's New, and the update feed's highlights
  default to the version's highlight lines.
- SpaceMouse recognition (#104): a device whose name says 3Dconnexion counts whatever its USB ids, and one with no
  vendor id (as some systems report Bluetooth devices) counts when its name says SpaceMouse / SpaceNavigator; the
  CadMouse and the keyboards never do.
- 0.10.9 notes, added late (they were lost in that release's merges; the code shipped in 0.10.9): brush cursor (#88,
  TheRedWaxPolice, with rodrigomoretto): the ring is a true circle in the surface's tangent plane, symmetry and radial
  copies are dots, the crosshair stays while the view turns, pans or zooms, and Preferences > Cursor has Hide ring
  while stroking (off by default). Panels scroll with a left-drag on empty space or a middle-drag (#100, ebarranco).
  Neutral is the default look for new installs; Look > Outlines and Outline opacity (#98); the Strokes radial menu on
  J; viewport hints with Don't show again (Preferences > Hints).
- The membership page (sculptforge.app/support.html), the studios page and the in-app membership card drop the lion
  picture for a calm header: type, a warm glow in the site's orange and a faint grid; on the site it follows light and
  dark mode. The picture is no longer embedded in the app (about 115 KB smaller).
- Every paid membership lists **Vote on what we build next**: members vote in #feature-vote on Discord, anyone can
  suggest in #ideas, and members' votes pick what ships. Insiders also propose the options.

### Added
- **Stroke popup > Shapes** (Discord, Darth): Mask Pen, Lasso Mask, Rectangle Mask, Cut Line, Cut Lasso, Cut Rect and Cut
  Polyline. Picking one latches Mask or Select with that shape, so a plain drag masks or cuts with it; with Mask
  latched a held Ctrl unmasks. The cursor label names the shape (e.g. "Cut Line").
- SpaceMouse (#104, reported by fenixpoints): Preferences > Input > SpaceMouse lists each joystick SDL reports (name,
  USB ids, and why it was or wasn't taken: not a SpaceMouse, cannot open, fewer than six axes), warns when the
  SpaceMouse is open but no axis has moved (another program such as 3DxWare holds it), and **Copy device report**
  copies that list with the version, platform and SDL version. Skipped devices are also logged.
- **Close Holes** (Tool > Geometry, `close_holes`, #103): caps every open boundary of the active Part (a hole, an open
  bottom, the rim of an extracted shell) with new triangles sized like the ones around it. The patch is faired into
  the curvature around the hole, so a sphere closes round, not flat. The rest of the Part is not changed, and the
  patches get a face group of their own. `max_edges` closes only the small holes. Thanks to the reporter of #103.
- **Clip** mode for Cut (Tool > Cut, `cut mode=clip`, #102): a line or open polyline presses its shaded side flat onto
  itself, and a lasso, rectangle or closed polyline presses everything outside it onto its outline. Points keep their
  depth and nothing is removed. The cut goes through the whole Part, follows symmetry, and the mask holds points back.
  Thanks to the reporter of #102.
- **X-Ray** (`xray`, #80, #91): a button next to Solo in Tool > Parts, and a View checkbox. The other Parts turn
  see-through and only their nearest surface shows, so the active Part shows through them. When only the active Part
  is shown, it is see-through itself.
- **Light controls** (Render > Materials > Light, `light`, #80): drag on the pad to turn the light of the Lit Sphere
  and the studio shading. Intensity and Ambient sliders sit beside it, and Reset goes back to the material's own
  light. The light is saved in the project.
- **View resolution** (View and Preferences > Performance, `view_resolution`, #80): 100%, 75% or 50%. The 3D view is
  drawn smaller and scaled up, which helps slow or integrated GPUs. Save Render stays at full resolution.
- **Save Render** sizes, shapes and JPG (Render > Materials, `save_render`, #80): twice the view (as before) or 1080,
  1920, 2048 or 4096 pixels on the longer side. Shapes: the view's, 1:1, 16:9, 9:16 or 4:3 (a narrower shape widens
  the field of view, so the picture still shows what the view shows). Output is PNG or JPG.

### Fixed
- The active Part no longer switches by itself (Discord, Oslen): a background job (Quad Remesh, Voxel Remesh with Keep
  Texture, Auto UV, UV Editor Unwrap) that finished after another Part was picked made its own Part active again, and
  cut short a stroke in progress. Its result now goes onto its own Part, waits for a stroke to end, and the status bar
  says where it went.
- A triple-click no longer switches the Part while a pen is in range: a Wacom side button set to Double Click (the
  Intuos 3 default) followed by a tap counted as one (Discord, Oslen).
- With two screens, an unfocused window kept drawing only what a background job asked for: a Sculpt Timeline timelapse
  render and the hardware calibration stopped until the pointer came back (Discord, Tiagoffcruz). Full screen no
  longer minimises when you click on another monitor.
- A Smooth, Mask or Select latch from the stroke popup came back after picking another brush and then the first one
  again.
- The Library's close button works on small screens at a large UI scale (#80). When the tabs don't fit beside it, they
  fold into a drop-down, and the Sculpt Timeline no longer covers the Library's header.

## [0.10.9] - 2026-11-02

### Added
- Alpha options in the Alpha palette (#76, requested by rodrigomoretto): **Follow stroke direction** turns the alpha
  along the stroke with Blender's Rake smoothing (the direction is taken again every 20 pixels), **Angle** and **Angle
  jitter** turn it by a fixed or a random amount per dab, and **Tile across / down** repeat it 1 to 8 times inside
  the brush. They work with custom and Community brushes, are kept per brush, and the preview shows the tiling.
  Scripts: `brush_alpha_options`.
- **View > Lock camera** (Shift+L, the same switch as Preferences > Input > Start strokes off the model) and a
  **CAMERA LOCKED** chip in the status bar while it is on (#62 follow-up).
- Blender style: **Preferences > Input > Emulate 3 Button Mouse** (off by default): Alt+left-drag turns the view
  everywhere, as before.
- Each brush keeps its own settings (#78, requested by dCo3lh0 and on Discord): Size, Intensity, Falloff, Colour
  intensity, the stroke (with Spray's scatter and Lazy Stroke's radius) and the alpha follow the brush, custom and
  Community brushes included. Switching back brings back the values that brush had this session; a brush not used yet
  starts from the current values. **Brush > Save Brush Settings** keeps them for every start (`brush-settings.json`
  in the user data folder), **Brush > Reset Brush to Default** restores the shipped values, and **Preferences >
  Input > Share size and intensity across brushes** (off by default) keeps Size and Intensity shared as before.
- **Gizmo size** (Draw > Camera): the corner axes come in Small, Medium (as before, the default) and Large, on top
  of the UI scale; clicking an end still snaps to that side at every size. Saved in the preferences.
- **View Figure** (Draw > Camera, beside Axes, off by default): a small shaded head in the canvas corner that turns
  with the camera; click it to snap to the nearest side view. Requested on Discord.
- Starter meshes (Discord #ideas): a human head with eye and mouth loops, a bust, a full body in A-pose, a hand, a
  foot, an ear, an eye, a stylised head, a creature head blockout (skull, jaw and horns as Parts), a torso, and
  bevelled rounded box, cylinder, capsule and ring primitives. All quads, centimetres, centred, standing on the
  ground and symmetric where they should be; they open with subdivision levels over a small base cage (the whole
  set adds 0.8 MB). **File > New from Starter Mesh**, a **Starter meshes** folder with thumbnails in Library > Tool,
  **Start from a head** in the welcome, and `new_starter_mesh` for scripts. The human meshes are cut from the
  MakeHuman base mesh (CC0); the rest are generated by `tools/starter/build_starter_meshes.py`
  (docs/starter-meshes.md).
- "Your first sculpt in 10 minutes" (docs/guide/first-sculpt.md): a twelve-step lesson for complete beginners that
  turns the default sphere into a mushroom house: masked Move strokes for the walls and an overhanging cap, colour
  filled through the mask, Divide twice, a door and framed windows carved with Crease and Clay, raised spots
  painted with a hard-edged Paint brush, PBR shading and a saved render. The welcome has a big **Start the 10-minute
  lesson** button, and **Help > Your first sculpt (10 min)** opens it. Its screenshots come from
  tests/ui/first_sculpt.sfui, which ctest runs so the lesson keeps working. The website links it from the homepage
  and the guide index.
- **Render > Materials > Save Render**: the view without the interface at twice its size, saved as a PNG in the
  library's Images folder (Library > Image).
- Upstream watch for studios: a weekly check of OpenUSD, MaterialX, OpenColorIO, OpenEXR, OpenVDB, OpenSubdiv,
  OpenTimelineIO, OpenAssetIO, the VFX Reference Platform, Blender, Unreal Engine, Godot, SDL3, Dear ImGui and Python
  opens a tracking issue for each new release, plus a monthly "Upstream digest" (targets in `packaging/upstream.json`).
- Insider daily drops: almost every day a new alpha, brush, VDM or Kelvinlet preset, made by SculptForge's own
  procedural tools and checked on a test sculpt (too faint, seams, torn surfaces and near-duplicates are left out),
  posted to Insiders as an .sfpack. Supporters get the month's drops as one Insider Pack at month end, and everyone
  gets that pack free three months later (`tools/drops/`, `.github/workflows/insider-drop.yml`).
- Focus Detail (experimental, off by default): a billion-micro-triangle asset streamed from one compressed file.
  Detail loads in under and ahead of the brush and wherever the camera would show it; everything else shows a
  coarse version. Saving writes only the changed tiles. Try `sculptforge --benchmark standard-1b` or
  `sculptforge --focus-detail FILE.sfdetail`; design and numbers in docs/design/focus-detail.md.
- Precision work (guide: Sculpting > Precision work). **Steady Stroke** (<kbd>L</kbd>) now works with every stroke
  type (Dots, Drag Stamp, Slide Stamp, Spray), with **Catch up on release** (finish the line to the cursor) and the
  string drawn to the lagging brush; the Lazy Stroke type is Dots with it on. **World Size** (Draw palette) keeps
  Size in model units, so the brush stays the same size on the surface as you zoom; every Size slider
  and the cursor show px and mm. Size goes down to 0.5 px; <kbd>Shift</kbd>+<kbd>[</kbd> /
  <kbd>Shift</kbd>+<kbd>]</kbd> step 1 px, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+right-drag scrubs the size finely,
  and small brushes space their dabs closer so tiny strokes stay continuous. **Precision drag**: hold
  <kbd>Shift</kbd>+<kbd>Alt</kbd> while drawing and the brush moves at a quarter of the pen's speed, at
  **Precision Intensity**. **A pressure curve per brush**: 2 to 5 points with Soft, Linear, Firm and Fine detail
  presets, in Brush Lab and Preferences > Tablet.
- Smooth face group borders only (Brush palette): Smooth straightens the borders between face groups along the
  surface and leaves the rest alone, for clean clothing edges (#81, thanks bossian0220).
- Colour tints the material (Color palette, on by default): the current colour tints the Lit Sphere material of
  Parts with no vertex paint; grey leaves it as it is (thanks ThinkingPolygons).
- Cut (#101, #91): one tool that cuts the active Part with a Line, Lasso, Rectangle or Polyline drawn on the screen,
  in the Ctrl+Shift brush picker and in Tool > Cut. Away removes what the shape covers through the whole Part and
  closes the cut with a flat cap (a face group of its own), In cuts a flat-bottomed pocket Depth deep, Add extrudes
  the shape out of the surface (boolean union). An exact boolean in screen space: the uncut surface and its mask,
  colours, UVs and layers are kept, and the result is watertight. Follows X/Y/Z and radial symmetry; one undo step.
  Scriptable as the `cut` command.
- Lasso Mask and Rectangle Mask: Front faces only (#91), also `front_faces_only` on mask_lasso / mask_rect.
- Duplicate Part shortcut: Ctrl+Shift+D (also Duplicate Part in the command palette).
- Stroke popup: press G (Z in the Blender style; any key in Edit > Preferences > Hotkeys) or the new button under the Stroke
  tile for a grid at the cursor with the stroke types and the modifiers Add / Sub, Smooth, Mask and Select. Hold the
  key, point and let go (a hovering pen works too), or tap it and click. Smooth, Mask and Select stay on without
  holding Shift or Ctrl until you pick them again or another brush; a label at the cursor shows which. A plain grid,
  not a ring. (Suggested on Discord.)

### Changed
- What's New is a window now: after an update (and from **Help > What's New**) a centred card shows the version's main
  features, each with a picture, one line and a **Try it** button that opens it (switch the look, open the membership
  card, select the brush, open the benchmark...), plus the full release notes, the Discord and Close. It takes the
  look's colours, fades in, follows the UI scale and works from the keyboard (Esc, Tab, Enter). The guide chapter and
  the website page show the last three versions in full (a band with the date, the feature cards, "All changes"
  folded) and the older ones as short groups (0.10.1 to 0.10.5, 0.9.x, 0.8 and earlier); the contents list follows.
  The data is a ```features block per version in docs/guide/whats-new.md, read by the app and build_site.py alike.
- Image and text to 3D work out of the box with no token: the free Hugging Face default is now **Hunyuan3D 2.1**,
  for new installs and for everyone who never picked a model (an explicit choice is kept; a saved TRELLIS.2 is
  kept when a token is saved too). TRELLIS.2 shows as "Best detail (needs token)".
- TRELLIS.2 without a Hugging Face token no longer fails: Hunyuan3D 2.1 runs instead and the status line says
  "TRELLIS.2 needs a free Hugging Face token (Preferences > AI); used Hunyuan3D 2.1 instead." The token can now be
  set in the new **Preferences > AI** too.
- Generated models get **Quad Remesh** by default instead of voxel remesh: clean quads (about resolution² / 2, 32768
  at the default 256) with one subdivision level projected onto the model, ready to sculpt and subdivide. A voxel
  pass cleans up holes and overlapping pieces first. It runs in the background with the progress bar (about 20 s
  for a typical Hunyuan3D model). **Remesh** in Generate 3D also offers Voxel remesh and Off; the CLI takes
  `remesh=quad|voxel|false` (`true` means quad) and `levels=0-2`. The status line says "remeshed to N quads".
- The Hunyuan3D licence notice (the Tencent Hunyuan Community License doesn't cover use in the EU, UK or South
  Korea) is shown once, the first time a Hunyuan3D model runs, and stays in the Model picker's tooltip.
- Hover in every look is calm and clear: buttons, menu items, shelf buttons and palette headers brighten a little in
  about 100 ms (no accent tint), pressed is a touch darker, and brush, alpha, material and look tiles (shelves,
  pickers, Library) lift slightly without moving anything; the active tile keeps its orange outline. Sliders
  brighten, grow a knob and show their exact value. Hover and keyboard-focus rings follow **Outlines** (Off: none;
  Subtle; On) and **Outline opacity**. New **Preferences > Interface > Reduce motion**: colour changes only, no easing
  or lifting. Animations draw frames only while they run.
- Large brushes on dense meshes no longer stall the view: from 2 million vertices a stroke's dabs are placed on the
  stroke thread (the same dabs, in the same order, as the frame loop places them; no preview), so a frame keeps
  drawing while a 240 px dab moves millions of vertices, and a light batch is still waited for within the frame.
  On the standard scene's 240 px brush the slowest frames drop from 139 ms (15M) and 306 ms (33M) to about 30 ms in
  first runs, and lifting the pen no longer waits (162 ms at 33M). `SF_BACKGROUND_DABS=0` turns it off.
- Move on dense meshes no longer stalls: it worked out its whole region's normals and bounds again for every pointer
  sample (several times a frame with a 240 Hz pen, more the slower the frame got: seconds at 33M under load). The
  positions still follow every sample; the normals and bounds now follow once a frame, and the mesh comes out the
  same, bit for bit.
- The first orbit after a long stroke no longer stalls (180-300 ms at 33M): the moving view's lighter stand-ins for
  the changed areas are rebuilt a few milliseconds a frame, the rest drawn in full detail until then.

### Fixed
- Blender style: Alt+left-drag on the model masks and Ctrl+Alt+left-drag unmasks, as Blender 5.1+'s default
  sculpt keymap does, instead of turning the view (#77, reported by TheRedWaxPolice). Beside the model Alt+drag still
  turns the view, and Alt+Shift+drag pans anywhere. Shift+F (Strength) sets the brush's Intensity instead of Smooth's.
- UI test runs remove their temporary settings folder when they pass (thousands of runs had filled `/tmp`).
- Tablets (a Wacom Intuos 3 report): a tablet plugged in after launch works without a restart ("Tablet connected" /
  "Tablet disconnected" in the status bar), also through Wacom Bridge; on Windows, pressure now also comes from the
  Wacom driver with "Use Windows Ink" off (Wintab); a pen without pressure draws at full strength instead of not at
  all, and Preferences > Input and > Tablet say "Pressure: on" or "not detected" with a troubleshooting link. The
  view no longer jumps when the pen comes back into range elsewhere or leaves mid-drag. The active Part changes only
  with the keyboard's Alt and a quick click (not with a pen button set to Alt, a long pen press or quick pen taps),
  and the status bar says "Active Part: <name> (Alt+click)".
- The enlarged brush or alpha thumbnail you point at in a picker is drawn over its neighbours again, not
  under them (#86, thanks tristratos).
- The brush centre now sits exactly under the cursor at high zoom and on HiDPI screens: the brush's ray is worked
  out from the camera directly, and the view is drawn on whole pixels where the rays are cast (it could be up to
  half a pixel off). The surface point under the brush is lightly filtered, so strokes on dense meshes no longer
  wobble.
- Preferences > Interface > UI scale applies at once: text, controls and panels resize live, no restart (#87).
  Before, the choice waited for the next start and was lost unless the interface was saved. Automatic now follows
  the monitor the window is on: on Windows (per-monitor DPI v2, now declared in the app's manifest) dragging the
  window between a 100 % and a 150 % monitor, or changing Settings > Display > Scale, rescales the UI right away.
- The pen picks a brush, then stops clicking the panels and the canvas until the mouse is used (#95; Huion Kamvas,
  display tablets, Wacom with Windows Ink on or off): SDL 3.2 keeps one "a pen is touching" flag for all pens, and
  once Windows took a pen away while it touched (a drag capturing the mouse), it made no mouse events for any pen
  again. SculptForge now makes the pen tip's mouse events from the pen's own and ignores SDL's, lets go of the
  button whenever the pen leaves, and ignores the mouse presses some drivers (Huion) send beside the pen's, whose
  release may never come. A replay test covers Windows Ink, a Huion-style pen + mouse stream, SDL's stuck state and
  Wintab-style input.
- Tablet Mouse mode (#72, 0.10.8): it followed the system cursor, which with the tablet driver in pen mode is where
  the pen is, so it felt the same as Pen mode. Now only the pen's movement moves the cursor, with no jump when the
  pen comes back elsewhere, and while the pen is in range the arrow is drawn at the brush (the system's, at the
  pen's place on the tablet, is hidden), so arrow, brush and clicks agree.
- With two screens, moving the pointer to the other screen made SculptForge draw as if it were in the background
  until the pointer came back (Windows 11). It now keeps drawing after any input while its window has focus,
  wherever the pointer is, and rests only when it loses focus.
- Turn AI off: **Preferences > AI > Enable AI features** (also asked in the first-run welcome). Off hides AI Brush,
  Generate 3D and their menu, shelf and command palette entries, removes the AI commands from scripts, the CLI and
  the MCP tool list, and stops every AI network call; the MCP server and the offline Relief / Inflate / Stamp stay.
  Saved at once. Studios: `SF_AI=off` (or `--no-ai`) turns it off and locks it. About and the website now say:
  "Your art is never uploaded or used for training. AI features run only when you use them and can be turned off."
- Help > Benchmark (and `--benchmark`) checks the memory free now before each scene instead of trusting the
  computer's total: a scene that doesn't fit (15M needs about 2.2 GB free, 33M about 5 GB, plus a margin) is skipped
  with a clear message ("33M skipped: needs ~5 GB free, 1.3 GB available. Close other apps and run again") rather
  than running the system out of memory. The result lists skipped scenes; the command line exits with code 2.
- Subdivide, Divide, Divide to a micro level and Voxel Remesh ask first ("Not enough free memory", Continue anyway or
  Cancel) when the mesh they would build doesn't fit the memory free now.
- Runs started by scripts and agents (`SDL_APP_ID=sculptforge-agent`, or `SF_ISOLATE=1`) use their own settings and
  log folder for every mode (renders, `--bench`, `--calibrate` too), so they no longer write into your
  sculptforge.log or move your session's log aside. Isolated runs remove their temporary folder when they pass.
- Clay and Clay Block follow Intensity and pen pressure: below the default Intensity a dab no longer planes the
  surface all the way to its plane, so light strokes and low Intensity build gently instead of looking the same as
  full strength (#83, thanks NimeNayota). Custom clay brushes and micro levels too.
- Alphas work on micro levels: Drag Stamp and every other stroke stamp the alpha instead of a round dab (#92,
  thanks Devon0209).
- Strokes keep sculpting across a silhouette: when the cursor slides just past the edge of a form seen in profile
  and the brush still covers it, the dab lands on the edge instead of stopping (#84, thanks Bro-Code-99).
- Brush shelf previews: Tendril and Elastic Tendril pull a long horn out past the ball (Tendril's preview re-aimed
  every dab at the ball and dropped the dabs past its edge; scripted Tendril strokes now drag in the view plane as
  the viewport does), and Layer and Nudge draw clean shapes instead of a pinched plateau and a torn ridge. Previews
  are re-rendered once (thumbnail version 4). A new test sculpts every built-in brush's preview and checks its shape.
- Library > Projects listed each project twice on Windows: recent files and the User folder spelled the same path
  with different slashes (#90). Paths are now compared normalised, and without case on Windows.
- The Library's close button stays on screen on small displays (1280x720), and Esc closes the Library (#80).
- The right shelf's region outline is no longer drawn under its buttons, and the "x" buttons (Library, console,
  window, palettes) are centred (#98).
- macOS: a window closed in full screen reopens maximised instead of full screen, so the traffic lights and menu
  bar stay reachable (#85, again after #41).

## [0.10.8] - 2026-11-02

### Fixed
- Tablet Mouse mode (Preferences > Tablet): on some Windows tablets the brush stopped following the pen in 0.10.7,
  because the system cursor reading stayed put while the pen moved. A reading that doesn't move with the pen is now
  ignored and the pen moves the brush relatively, from where it was, with no jump (#72).
## [0.10.7] - 2026-11-01

### Changed
- The membership card at launch is redesigned: the cube mark and the promise up top, the three memberships side by
  side with their perks checked off and one bold button each (Insider highlighted), and the members' Discord in the
  footer. It takes the current look's colours and opens in 150 ms. "Maybe later" now hides it for 30 days, updates
  included; the welcome's Support button and the ten-hour note open it too.
- New memberships; nothing in the app is ever paywalled. **Supporter** ($5 / month): your name in the credits (in
  the app and on the new [supporters page](https://sculptforge.app/supporters.html)), #members, a vote in the monthly
  feature vote and the monthly pack. **Insider** ($20 / month): also proposing vote options, a daily drop (a brush,
  alpha or VDM), preview builds of the next release about a week early and the monthly dev Q&A. "Every release
  stays free for everyone; bug-fix and security releases go out to everyone the same day." **Studio** ($300 /
  month) covers the whole company with no seat count: Insider for every artist, a custom studio build, up to 5
  custom items a month, guided development, priority triage, a private team channel and the logo on the website
  and README; **Founding Studio**: the first 10 studios lock in $150 / month for good. The card, Help > About, the
  welcome, the website and the guide (with a new Studio onboarding section) say so; the monthly pack and Insider
  drops show as **Member pack** in Library > Packs (was Supporter Pack).
- Insider preview builds: a workflow publishes the tested packages of a commit as a prerelease in the private
  Insiders repository and posts to #insiders (`.github/workflows/insider-preview.yml`).

### Fixed
- Custom and Community brushes (Clay Slab and other presets) show the whole alpha at Falloff -100, as the
  built-in brushes do since #60: the stamp reaches its square's corners instead of being cut by the preset's round
  tip (#75, reported by rodrigomoretto). At the default Falloff presets look as before; saved presets are unchanged.
### Added
- A new default look: new installs start in **Forge Terminal** with the **Tokyo Night** colours and the
  **SculptForge layout** (menus by task: File, Edit, Sculpt, Paint, Mesh, View, Render, Help, whose sections are the
  palettes; a status bar; a one-row tool shelf). Existing installs keep their look and layout, and see a one-time card,
  "SculptForge has a new look", with **Try it** and **Keep my layout**. The welcome offers **Experienced sculptor? Use the
  Classic layout** in one click, and Preferences > Interface > Look switches any time. The look is remembered
  on its own. Also in Preferences > Interface > Look: the **Classic** look, **Forge Terminal** with Tokyo
  Night, Catppuccin Mocha, Gruvbox Dark, Nord, Rosé Pine, Kanagawa or Everforest colours or **Match my desktop
  theme** (on Linux, the Omarchy theme's colours when Omarchy is installed), **Phosphor** (green or amber, optional scanlines and glow on the interface only) and **Blueprint**.
  Scripts: `ui_look`.

### Fixed
- Switching looks no longer grows the interface's minimum window size each time (the Sculpt Timeline strip covered
  the axis gizmo at 2400x1350).
- A stored SculptForge-layout UI configuration without shelves gets the SculptForge layout's shelves, not the
  classic ones.
### Changed
- Linux: the native Wayland support is now documented (installing guide, site, README): every Linux package opens
  without XWayland, stays sharp at fractional scales and reads pen pressure through tablet-v2. A test checks the
  Linux build's SDL has both the Wayland and X11 drivers, and the RHEL/Rocky builds now include libdecor (title
  bars on GNOME Wayland).
### Added
- Standard benchmark scenes: `sculptforge --benchmark standard-15m` (and `standard-33m`, or `standard` for both;
  also Help > Benchmark) sculpts a procedural 15 or 33 million triangle head with Clay at 25, 60 and 120 px, Move,
  Smooth and a 240 px brush, and reports frame time p50/p95/p99, stroke lag in pixels, the worst stall, peak RAM and
  graphics memory. The [benchmarks page](https://sculptforge.app/benchmarks/) lists results, explains how to run it
  in three steps and how to submit yours (`tools/benchmark/submit.py` or the new **Benchmark result** issue form).

## [0.10.6] - 2026-11-01

### Added
- Packs and macros (#20): `.sfpack` files bring brushes, alphas, Lit Spheres, Material Recipes, VDMs, keymaps, pie
  menus and macros in one file (format: `docs/pack-format.md`). **File > Install Pack...**, a dropped file or
  **Library > Packs** shows a review first (counts by type, every item, the license text, the author; a Supporter
  Pack is marked by its `"channel": "insider"`, display only). Installed packs live in their own folders in the app
  data folder, can be switched off or removed, never replace built-in or user items (a clash gets the pack's name),
  and their items appear in the brush, alpha, Lit Sphere, Material Recipe, keymap, pie menu and macro lists tagged
  with the pack. **New Pack** exports your own items; `pack_install`, `pack_list`, `pack_remove`, `pack_enable`,
  `pack_disable` and `pack_create` work in `sculptforge-cli` and Python; `--no-packs` is a safe mode. Packs are data
  only and every one is treated as hostile: a strict zip reader refuses absolute paths, `..`, symbolic links,
  devices, encryption, ZIP64, overlapping entries and duplicate or Windows-reserved names, caps sizes, file counts
  and the unpacking ratio, accepts only the listed file types and parses every file (JSON strictly).
  Macros (`.sfmacro`): **Record Macro** keeps the commands you run, the **Macro Editor** checks the text as you type,
  and every macro is an action for F3, keys and pie menus; edits on one Part undo as one step. A macro
  shows its whole command list and asks before it first runs, only runs SculptForge's own commands (nothing that
  reaches the network or another program), and uses files only inside the project folder or picked when it runs
  (`macro_run`, `macro_list`, `macro_check`).
- PBR viewport: **Material > Viewport Shading** switches between **Lit Sphere** (the default) and **PBR**, which
  shows the texture's material channels as a game engine or renderer does: GGX reflections from a prefiltered
  environment, diffuse light from its spherical harmonics, multiple-scattering energy for rough metals, exposure
  and an ACES, AgX or Reinhard tone map. Painted Normal and Height channels bend the shading through MikkTSpace
  tangents. Four environments: **Studio** (generated) and three CC0 HDRIs from Poly Haven (**Photo Studio**,
  **Small Studio**, **Outdoor Sky**), plus `.hdr` / `.exr` files from `SCULPTFORGE_HDRI_PATH`; rotation, the
  environment behind the model and a cavity term that keeps sculpted detail readable. Parts without a material
  texture use a roughness and metallic set there. A new environment is prepared on a worker thread, and a
  20.9-million-triangle mesh draws in about 8 ms in PBR next to about 6 ms with Lit Sphere (Apple M1 Pro).
  Saved with the project (`viewport_shading mode=pbr environment=outdoor_sky exposure=0.5 tone_map=agx`).
- Mesh Maps bake: **Tool > Bake** adds **Curvature** (convex edges bright, creases dark, from the surface's mean
  curvature at a set radius), **Thickness** (how far rays travel inside), **Position** (16-bit) and **ID** (a colour
  per face group or per Part) to the normal, world normal, displacement, AO, cavity and colour maps. The source can
  be the Part's own **top subdivision level** (baked onto its lowest level) or its **micro level** (onto its base),
  besides other Parts. UVs outside 0..1 bake as **UDIM tiles** (`name_normal.1001.png`, `1002`, ...). The bake runs
  on a worker thread with progress and Cancel (`bake source=subdivision maps=normal,ao,curvature,thickness,position,id`).
- Mesh Maps in the texture (`mesh_maps`): AO, curvature, thickness, position, world normal and ID baked on the
  texture's own UVs and size, tile by tile on every core, for Fill layers to read. After sculpting, only the tiles
  near the change are baked again, from a compact copy of the nearby triangles, and the result matches a full bake
  texel for texel (2K maps of an 82k-triangle Part: about 10 s in full, about 0.5 s for a local change).
- Fill layers: **Tool > Paint Layers > + Fill Layer** lays a material (colour, roughness, metallic, height) over the
  whole Part through a mask built from effects: **Edge Wear**, **Dirt**, **Gradient**, **Thickness** and **Light
  Direction** generators reading the Mesh Maps, **Noise** in 3D (no UV seams), a **Paint** mask painted with the
  Paint brush, **Levels**, **Blur**, **Invert** and **Fill**, each with its own settings and blend. Fill layers sit
  in the layer stack with paint layers, re-evaluate tile by tile when their settings, painted mask or the Mesh Maps
  change, undo like any edit and are saved with the project (format 7). With **Live** on, sculpting re-bakes only
  the Mesh Maps tiles near the stroke on a worker thread and the Fill layers follow the new form
  (`fill_layer generator=edge_wear color=#e0e0e8 metallic=1`, `mesh_maps action=live live=true`).
- Material Recipes: **Worn Metal**, **Painted Edges**, **Dirt and Dust** and **Rusted Iron**, stacks of Fill layers
  kept as small `.sfmaterial` text files, laid on a Part in one undoable step from **Tool > Paint Layers >
  Material Recipe**. Save your own from a Part's Fill layers (`material_recipe name=worn_metal`,
  `material_recipe action=save path=mine.sfmaterial`).
- Forge Spheres with the pointer only (#8): **Drag places child** (or <kbd>Ctrl</kbd>+drag) moves a new sphere away
  from its parent in one Draw drag; the sphere last clicked or added is selected (outlined in orange) for **Delete
  Selected**, **Insert Between** and **Set as Root**, and a right click on a sphere opens a menu with the same.
  Set as Root keeps the armature's shape and links (`forgesphere_set_root index=3`).
- Maya input style (#32): <kbd>Alt</kbd>+right drag zooms with a sideways drag (right zooms in), as Maya's own
  dolly; **Preferences > Input > Zoom drag** switches it back to up and down, and **Invert zoom** turns zoom drags
  around in every style.
- Along the normal (from the discussions): Move, Tendril, Elastic Tendril and KelvinGrab started with the invert
  key held (<kbd>Alt</kbd>; <kbd>Ctrl</kbd> in the Blender and Maya styles) move the surface along the average
  normal under the brush instead of the view plane. These brushes had no use for the key; **Preferences > Input >
  Along the normal** makes it Always or Never.
- Whole-Part deformations (from the discussions): **Tool > Deformation > Polish** (bilateral normal filtering:
  noise goes, planes and edges stay), **Relax** (even spacing along the surface), **Smooth** (Laplacian, or Taubin
  with Smooth keeps volume) and **Balloon** (fills the form out; negative lets the air out), each with an amount,
  mask-aware, open borders and hidden faces kept, one undo step, and as commands (`deform_polish`, `deform_relax`,
  `deform_smooth`, `deform_balloon`). 10.5M vertices at amount 50: Smooth 0.5 s, Balloon 0.7 s, Relax 0.9 s,
  Polish 2 s (Apple M1 Pro).
- Dynamic topology's **Smooth remesh** (from the discussions): how much Shift-smoothing remeshes while Dynamic is
  on, from **Smooth only** (the topology stays) to **Simplify** (the default, as before); a preference, saved with
  the project, `dyntopo smooth_remesh=0`.
- Brush falloff options (from the discussions): **Connected only** keeps a dab to the surface joined to the point
  under the cursor, **View falloff** measures the falloff on screen (a cylinder through the model along the view),
  as Blender's proportional editing. For every sculpt brush, recipes, VDM stamps, masking and vertex paint; saved
  with the preferences, the project and custom brushes. A dab moving 470k of 10.5M vertices: 9.6 ms, 16 ms with
  Connected only.
- The official community Discord (https://discord.gg/gWrYkEmQhH): **Help > Join the Discord...**, a button on the
  support card at launch (it opens the invite and leaves the card open), a link in the first-run welcome, the top
  bar and footer of every website page, the membership page (members get private channels), the README and
  CONTRIBUTING.md (questions on Discord; bugs still go to GitHub issues).

### Changed
- The website's membership page is now `support.html` (titled Membership; the old address redirects there) and lists
  what each membership includes. Help > Support SculptForge, the welcome, the reminder and the launch card open it
  and describe the membership benefits; the one-time option is now the One-time Supporter (pay what you want).
- Backface culling (#68): **Tool > Display > Backface culling** skips the back faces of every Part, closed or open,
  from any view: you see through the openings of open meshes, and brushes and picking work only on the faces drawn.
  Off by default; saved automatically, projects keep their own. An 8M-triangle open mesh seen from behind: 3.1 ms
  -> 0.7 ms a frame; a closed 20M sculpt is unchanged (its hidden back faces were already skipped).
- Viewport cursor (#73): a crosshair over the 3D view where brushes act; the arrow (or the resize, text and link
  cursors) over panels, menus, popups and gizmo handles. **Preferences > Interface > Viewport cursor**: Crosshair
  (default) or Arrow.
- A radial menu on the right mouse button (#74): **Preferences > Input > Radial Menus** offers **Right mouse
  button**; a plain right-drag then opens the pie (as Maya's marking menus) and the view turns with
  <kbd>Shift</kbd>+right-drag or the style's other ways (the panel says which). Off by default.

### Fixed
- Maya input style (#32): <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+drag erases the mask instead of starting
  the <kbd>Ctrl</kbd>+<kbd>Shift</kbd> visibility selection.
- Smooth on Quad Remesh results (#69): smoothing averaged every neighbour along a triangle edge, quad diagonals
  included, so a vertex at one end of a diagonal was pulled along it and the quads sheared into zigzags. Smooth (the
  brush, Shift, recipes' smoothing and Tool > Deformation > Smooth) now averages the polygon's own edges. 30 smooth
  strokes on a remeshed sphere: corner angle error 3.9 -> 3.3 degrees (it was 8.8); on the lion head 8.7 -> 9.7
  (was 19.5). Triangle meshes are unchanged.
- Keep Masked remeshing (#67): Voxel Remesh with Keep Masked failed on real models ("Couldn't cut the remeshed
  surface cleanly") at every resolution, because the voxel surface touches itself where parts of a model meet (a
  lion's mane) and the join refused any such surface. Those spots are now fine; a mask edge that touches itself is
  tidied at those corners, masked spots smaller than the new polygons are remeshed, narrow gaps between masked areas
  are kept, and the strip is widened where the join folds (the status line says when the mask was tidied). When it
  still can't be joined, the message says what to change (blur or grow the mask, by how much, or unmask where parts
  overlap). Lion head and marble bust, four masks, two resolutions: 0 of 16 joined before, 16 of 16 now.
- Tablet mouse mode (#72): after lifting the pen and putting it down elsewhere, the brush and the system arrow moved
  together but apart, and clicks landed at the brush. In mouse mode the system cursor is now the one cursor for the
  brush and the panels (read with every pen position; the pen adds pressure and tilt).
- Radial menus on mouse buttons open only on a plain press (#74): <kbd>Alt</kbd>+middle pans, <kbd>Alt</kbd>+right
  zooms (Maya style) and <kbd>Shift</kbd>+right turns the view, whatever menu is on the button.
- glTF import rejects malformed accessor and buffer sizes instead of crashing or reading out of bounds (reported by
  @Agapenordev in #70).
- Paint Layers: picking a Material Recipe, or changing a Fill layer, from the panel could crash: the panel kept
  reading the texture the command had just replaced. Its commands now run once the panel is drawn.

## [0.10.5] - 2026-11-01
### Added
- **Radial symmetry** (#63): **R** in Transform > Symmetry and on the top shelf repeats strokes around the X, Y or Z
  axis (Radial Copies 2-64), combined with the X / Y / Z mirrors (mirror + radial = 2N copies), and **Local**
  symmetry about the active Part's centre. Applies to every brush and stroke, mask and smooth strokes, vertex paint
  and texture painting, Mask / Select Rectangle and Lasso, Dynamic Topology (new topology on every copy) and Insert;
  the brush cursor shows on every copy. Saved in projects; `radial`, `radial_axis` and `local` arguments on
  `stroke`, `vdm_stamp`, `mask_rect`, `mask_lasso`, `hide_rect`, `hide_lasso`, `insert_primitive` and `insert_mesh`.
- **Preferences > Interface > Timeline position** (#63): the Sculpt Timeline strip at the top of the viewport (the
  new default, away from the brush shelf) or at the bottom; saved automatically.
- **UV Editor** (Tool > Texture Map > UV Editor, Alt+U): a window with the active Part's UV layout over the UDIM
  tile grid, drawn on the GPU (pan, zoom, a checker or the painted texture behind). Select vertices, edges, faces or
  islands (1-4) by click or box, or on the model with **Select in 3D**; move, rotate and scale with the selection's
  handles or G / R / S (X / Y lock an axis, Ctrl snaps). **Seam Tool**: drag on the model to mark seams along the
  shortest edge paths (Alt clears); seams also from face groups, island borders or sharp edges, and show in red on
  the model. **Unwrap**: Least Stretch (LSCM, then as-rigid-as-possible), Conformal (LSCM), By Face Groups, Planar,
  Unfold Islands (keeps pins) and Auto Seams (xatlas), solved with a multigrid preconditioner (a 1.3M-triangle
  sphere unwraps in seconds); closed, looped and tangled pieces get cut automatically; selected faces unwrap in
  place. **Pack** into one tile or across UDIM tiles with a margin, rotation and equal texel density, by island
  outline or box (whichever fits larger), or at a fixed texel density spilling into more tiles. **Texel density**:
  set, match and pick, with a density heat map. **Trim sheets**: lay islands into horizontal strips (count, heights
  or v ranges), fitted to the width or keeping proportions. **Pin**, **Weld**, **Split**, **Straighten**, **Flip**,
  **Rotate 90**. The model shows islands, stretch or density colours and a UDIM-tinted checker live during edits, and
  **Paint stays on the model** re-projects a painted texture (every layer) onto each new layout in the same undo
  step. Seams and pins are saved in projects. Commands: `uv_unwrap`, `uv_pack`, `uv_seam`, `uv_select`,
  `uv_transform`, `uv_edit`, `uv_density`, `uv_trim`, `uv_info` (scripts, CLI, Python, MCP); guide chapter UV
  Editor.
- **Preferences > Input > Start strokes off the model** (#62, off by default) and **Toggle strokes off the model**
  (<kbd>Shift</kbd>+<kbd>L</kbd>): a left-drag that starts on the background begins a stroke, which touches the model
  once the cursor reaches it (Lazy Stroke and the stroke path start there, so nothing jumps). Ctrl (mask) and Shift
  (smooth) strokes work the same way; a Ctrl+click on the background still inverts the mask. The view still turns
  with a right-drag (or a pen button set to Right click), pans with Alt+drag or a middle-drag, moves with Space held
  when that option is on, and Blender and Maya keys keep Alt+left-drag. A label at the top of the canvas shows while
  it is on. Lasso Mask, Rectangle Mask and the hide / show shapes are unchanged.

### Changed
- Features use SculptForge's own names (Parts, Forge Spheres, Vertex Paint, Face Groups, Library, Lit Spheres,
  Quad Remesh, Voxel Remesh, Dynamic Topology, Snapshot, Gizmo, Lazy Stroke, Wireframe, Bridge, Sharp
  Crease, Clay Block, Tendril...); the command palette finds them by what they do. The user guide's chapters follow
  (Parts, Forge Spheres, Vertex Paint, Face Groups; the old web addresses redirect), and
  Help > User Guide > Quick tour for experienced sculptors shows where everything is. The
  plug-in is now called SculptForge ZTL Export (same file, `SculptForgeZToolExport.txt`).
- New installs default to the SculptForge-style keys (Preferences > Input); a first-start "Coming from: Classic sculpting keys /
  Blender / Maya / New to sculpting" choice sets the key style, and existing users keep their saved choice.
  The Classic sculpting keys are the **Classic** option.
- Contributions are signed off under the Developer Certificate of Origin (`git commit -s`; see CONTRIBUTING.md).
- Faster CI and releases with the same tests (docs/releasing.md):
  - **Compiler caching:** compiles go through ccache, cached only by pushes to main and maintainers' own runs.
    Pull requests only read the cache, and tags never save one.
  - **Parallel tests:** `sculptforge_tests` runs as eight parallel shards (`sculptforge_tests --shard K/N`; each test
    now prints its time). ctest runs in parallel, with timed measurements and clipboard tests alone. The slowest
    test sets are spread over several free runners.
  - **Skipped work:** website-only pushes skip the build and test jobs.
  - **Build once, promote:** the release packages are built and tested on every push to main (the new Packages
    workflow). A tagged release promotes the packages of the commit it tags instead of building them again.
  - **Smoke tests before publishing:** every package (Windows installer and .zip, .dmg, .deb and AppImage on x86_64
    and arm64, .rpm) is installed on a fresh runner, opens a model, sculpts and saves.
- `scripts/release.sh X.Y.Z` tags, waits and brings the site, update feed, guide, AUR, Homebrew and Omarchy to the
  release, and drafts the issue replies. It is resumable, and stops with the fix when something is wrong.
### Added
- Selection Sets (**Tool > Selection Sets**, `selection_set_*`): any number of named snapshots per Part of the mask,
  face groups, visibility, vertex paint, UVs and shape, each holding the channels you tick. Store, restore, rename,
  delete, combine (union / intersect / subtract, also as restore modes onto the current mask and visibility) and a
  quick set from the visible faces, each one undo step. A set restores at any subdivision level of its Part (values
  carried through the levels as the stack carries attributes; shapes as offsets, so in-between detail stays). Stored
  compactly (runs of masked / painted / hidden elements, run-length face groups) in projects under the optional
  `selection_sets` key (no format version bump; older versions skip it), in `.sfsets` files
  (`selection_sets_export` / `selection_sets_import`) and in USD exports as GeomSubsets and primvars, which USD import
  reads back. A set with its shape is a morph target: the new **Morph** brush pulls toward it (Alt pushes away) and
  `selection_set_morph` (Morph to Set) blends the whole Part toward it by an amount, within the mask. Python:
  `Document.selection_sets`, `selection_set(name)` and `set_selection_set(name, mask=..., ...)` push and pull sets as
  NumPy arrays.

### Fixed
- The viewport now appears on Windows setups whose window uses another colour format (seen on NVIDIA with high-resolution displays): the view was drawn but never reached the window, so the cursor left trails and the model never showed (#29).

## [0.10.4] - 2026-11-01

### Added
- The interface in Spanish (#44): **Preferences > Interface > Language** switches between **English** (the default)
  and **Español** at once, saved automatically. The palette bar, the palettes and their sub-palettes
  (Preferences, Part, Geometry, Remesh, Masking, Face groups and more), the top, left and right shelves, the
  Library tabs, the command palette (which also finds commands by their Spanish names), Ctrl-help titles and many
  messages are translated; tool and brush names stay as Spanish-speaking experienced sculptors use them (Part, QRemesh,
  Forge Spheres, Library, Lit Sphere, Clay Block). The translations are written by hand and compiled into the program;
  anything not yet translated shows in English.
- Maya bridge (#42): **File > Maya** and a SculptForge shelf for Maya (`integrations/maya/sculptforge_maya.py`,
  plain `maya.cmds`, released as `sculptforge_maya.zip`). Send the selected meshes from Maya, sculpt, then
  **Send to Maya**: each object gets its new mesh by name, keeping its transform, parent and shading groups (one
  undo step in Maya). Meshes travel as OBJ (quads, UVs) through one exchange folder you pick in both apps, with
  a small, strictly checked manifest; no ports or connections, nothing in the folder is ever run, and the bridge
  is off until you choose the folder. **Get automatically** (off by default) checks that folder once a second
  (`maya_send` / `maya_receive folder=<folder>`; Help > User Guide > Working with Maya).
- Mask from the mesh border (#61): **Tool > Masking > By border** masks the open edges of the active Part and
  **Rings** (1-20) of edges inside them, so sculpting doesn't move the border (Blender: Mask from mesh
  boundary). **Face group borders** also masks where face groups meet, **Soft**
  fades the mask inwards; one undo step. A closed mesh masks nothing and the status bar says it has no open edges
  (`mask_border rings=2 facegroups=true soft=true`).
- The support card at launch: a full-window card over a bronze sculpt rendered in SculptForge, with the Supporter
  ($5 / month), Insider ($20 / month, "Insiders will get each release about two weeks early, starting with
  SculptForge 1.0.") and Studio ($150 / month for up to 10 artists) memberships and a one-time gift, each opening its Stripe checkout in
  the browser. It shows only while starting: the first time, after each update (with a link to What's New), and
  otherwise at most once every 7 days; never mid-session, in tests, renders, demos or CI (`SF_NO_SUPPORT_CARD=1`
  also turns it off). Maybe later, Esc or a click outside closes it; "I'm already a supporter" hides it for good
  (stored in project.json; there are no accounts). Preferences > Support turns it off. Its picture is decoded on a
  worker thread only when it shows. The donate page's first screen is redesigned to match (light and dark).
- Radial (pie) menus (#45): up to 8 menus of up to 8 items, each item any action the command palette (F3) finds
  (brushes, tools, commands, toggles). Hold a menu's key, flick toward an item and let go to run it; tap the key and
  the pie stays open for a click. A menu can also open from a mouse button (middle, back, forward) or a pen side
  button while it is held. Two built in: **Brushes** on <kbd>T</kbd> and **Masking** on <kbd>Y</kbd> (free in
  every input style). Edit them in Preferences > Input > Radial Menus: add, remove and reorder items, pick each
  action with the palette's search, give it your own label and icon, set the menu's key and button. ui.json
  keeps them; Export / Import shares one menu as a small JSON file holding only action names (unknown
  actions are left out, counts and text lengths are capped).
- Insert your own mesh (#34): **Append > Insert on the model > Active Part's copy...** (another Part) or
  **From file...** (`.obj`, `.ply`, `.glb`), dragged onto the surface like the shapes: centred, sized by the drag,
  its up axis along the normal, turned by the drag and mirrored by symmetry, one undo step
  (`insert_mesh part=<name>|file=<path>`).
- **Keep Masked** for Voxel Remesh, Decimate and QRemesh (#43, off by default): masked areas keep their
  triangles exactly; the rest is rebuilt and, for the remeshers, joined to them along the mask's edge with a
  strip of triangles so the model stays closed (`keep_masked=true` on `remesh`, `decimate`, `qremesh`).
- Triple-click another Part in the viewport to make it the active one and frame it (#43).
- **Preferences > Input > Hold Space and drag to move the camera**: with Space held, left-drag rotates, right- or middle-drag pans and Ctrl+drag zooms, anywhere on
  the canvas; never while typing in a text field.
- Twelve more built-in alphas (#43): Pores, Cracks, Scales, Rivets, Stitches, Fabric Weave, Bark, Rock Strata,
  Wrinkles, Skin Folds, Hair Strands and Dot Grid; and six more VDM brushes: Rivet, Stitches, Skin Fold, Wrinkle
  Line, Crack and Rock Ledge. All are generated in code the first time they're used (no downloaded or bundled
  image files), with thumbnails in the pickers and the Library (whose Brush tab now lists the built-in VDMs too).

### Fixed
- Falloff now shapes brushes that use an alpha (#60): the alpha is multiplied by the round
  falloff Falloff controls, so at -100 the whole alpha shows hard to its edges, at 0 a Square alpha's corners
  round off into a soft mound, and at +100 only a soft point in the middle is left. Before, alpha brushes always
  behaved as at -100. Falloff also reaches the built-in Clay Block's square tip and custom (Brush Lab)
  brushes now, which ignored it; at 0 they keep their own tip exactly as before.
- SelectRect and SelectLasso (hide / show) ignored symmetry (#51 follow-up): with X / Y / Z on, the mirror image of
  the shape counts as inside too, as with MaskRect and MaskLasso (`hide_rect` / `hide_lasso symmetry=x`).

## [0.10.3] - 2026-11-01

### Added
- Preferences > Tablet > Mouse mode (relative) (#54): for tablets whose driver is set to mouse mode, the pen moves
  the cursor by how far it moves, like a mouse.
- Alternate smooth: start a stroke with Shift held, then let go of Shift while still pressing,
  and the rest of the stroke smooths without shrinking the form (a Taubin smooth). Brush > Smooth keeps volume
  makes that the Shift / Smooth brush smooth (letting go of Shift then switches to the plain one).
- Ctrl+drag on the background with MaskPen draws a mask rectangle while dragging (#48) and masks what it covers;
  one that covers none of the model clears the mask, as before.
- Axis gizmo (#43): click an end of the X / Y / Z gizmo in the canvas corner to snap to that side view
  (orthographic), as in Blender; the negative ends are drawn too and the end under the pointer is ringed.
- Undo and Redo on the right shelf (#43); stored layouts get them at the top once (layout version 3).
- **Continuous** is also in the left shelf's stroke picker, under the stroke tiles (#43).
- Insert on the model respects X / Y / Z symmetry (#43): a mirrored copy on the other side, one undo step
  (`insert_primitive symmetry=x`).
- Part thumbnails in the Part list (#43), drawn from each Part's existing GPU buffers (no upload, the
  motion proxy on dense meshes), one per frame at most and only once a change has settled, never during a stroke.

- Ways to support SculptForge (memberships on the website; the app stays free and behaves the same for
  everyone). Each one only opens https://sculptforge.app/donate.html in the browser: **Help > Support
  SculptForge...**, a line in the first-run welcome (new: a small note on the very first start, also Help > Welcome
  in the command palette), a line at the end of **What's New**, and a **Supporters** section in **Help > About**
  listing the names in `docs/supporters.txt`, compiled in and shown as plain text (control and invisible formatting
  characters removed, 40 characters and 2000 names at most). After about ten hours of sculpting (counted locally
  in project.json) one dismissible note shows along the bottom of the canvas, never at launch and never again.
  No payment code, network calls, tracking or licence checks.

### Changed
- The Mask display toggle (right shelf, <kbd>Ctrl</kbd>+<kbd>H</kbd>) says in its help and the status bar that a
  hidden mask still masks (#43).

### Fixed
- Pen tablets in mouse (relative) mode (#54): lifting the pen and putting it down elsewhere made the cursor jump
  back to where the pen was on the tablet. Turn on Preferences > Tablet > Mouse mode.
- MaskLasso and MaskRect ignored symmetry (#51): the mirror image of the shape is masked too now, as with mask
  strokes. A Ctrl+click on the model with MaskLasso or MaskRect blurs the mask (Ctrl+Alt+click sharpens it), as
  with MaskPen; it did nothing. A lasso or rectangle that covers none of the model clears the mask.
- macOS (#41): reopening in full screen (restored from the last session, #35) left the menu bar and window buttons
  unreachable until another display was clicked; full screen is now entered after the window is on screen.

## [0.10.2] - 2026-11-01

### Removed
- The local API for other programs and its `mesh_read` / `mesh_set_positions` commands (added in 0.10.1, off by
  default). SculptForge no longer listens on any port for other programs.

### Changed
- Predictive strokes stay an experiment, off by default (Preferences > Performance): a surface-cursor race that
  could change a stroke is fixed, but a stroke is not yet identical to one placed without them in every run.
- Draft strokes are no longer chosen by any hardware tier or on battery (they measured no faster and aren't yet
  identical to a full stroke); the Draft Strokes setting still turns them on.

### Added
- Insert on the model (#34): the drag's direction also turns the inserted shape about the surface normal.

### Fixed
- Security: the timelapse, proof-mode and --record video exports passed the file name to ffmpeg's command line in
  double quotes, so a crafted name (for example a project called `x$(...)`) could run a command. Names are now
  quoted so nothing in them is interpreted (and refused on Windows when they contain shell characters).
- The brush ring that follows the surface (#25) read the mesh while a predictive stroke's thread was changing it,
  which could change where that stroke's dabs landed; it now shows the flat ring during such strokes.

### Added
- **QRemesh 2**, the new default quad retopology (**Tool > Remesh > QRemesh**, also under **Tool > Geometry**;
  `qremesh method=v2`). The layout is solved over the whole model at once as an integer grid: a smooth cross field
  that follows the principal curvature (smoothed over about one quad) and the feature lines, then a seamless
  parametrization whose grid lines are the edge loops, with feature lines and guides on grid lines and the poles
  on grid points, extracted at the target density. Poles appear only where the form needs them (a sphere gets
  exactly eight, a box one per corner) and loops run unbroken; open QRemesh Guides are exact edge loops; hard
  edges and face group borders (Keep Groups) are kept on their exact place; vertex paint density adds the pole pairs a
  change of density needs; symmetry mirrors the field and the result exactly; the result is deterministic.
  Loop-aware relaxation lets vertices slide along loops on ridges but not off them. Against the legacy method on
  the lion scan, the ICT head, a creature sculpted from a sphere and a hard-surface part at 2k / 5k / 10k quads:
  a quarter to a fiftieth of the poles at the same distance to the sculpt (lion 5k: 1,028 → 199 poles, corners
  18.2° → 7.9° off square; ICT head 5k: 511 → 114; creature 10k: 308 → 37; a 1.3M-triangle sculpt at 10k:
  401 → 8; boxes: one pole per corner and 0.000% distance). The full table is in the guide (Quad retopology >
  How it works). A 1.3M-triangle sculpt takes about 4 s to 5,000 quads.
- **Quality report** after QRemesh, in the status line, the QRemesh panel and the `qremesh` command's message:
  poles (by valence), how far quad corners are from square, and the distance to the sculpt (mean and Hausdorff).
  `quality=false` skips the measuring.
- Dense sculpts are decimated in parallel blocks to a working resolution first. A sculpted sphere at 2k / 5k /
  10k quads (M1 Pro, 10 cores, shared with other jobs): 1.3M triangles 3.1-6.4 s (peak 0.36 GB), 5.2M 6.6-8.6 s
  (1.3 GB), 21M about 20 s (4.7 GB); eight poles each time. The quality report adds a few seconds at 21M.
- Tests: test_qremesh2.cpp (eight poles on a sphere, determinism, exact symmetry, hard edges and groups, the
  command); `SF_QREMESH_BENCH=<dir>` runs the comparison with the legacy method and writes the results as OBJ,
  `SF_BENCH_LIMITS=1` times 1.3M / 5.2M / 21M-triangle sculpts with their peak memory.

- **Kelvinlet brushes**: physically based elastic brushes from the regularized Kelvinlets of de Goes & James
  (SIGGRAPH 2017): the exact displacement of an elastic solid under a softened point load, so the surface moves
  like clay pushed with a thumb, with no falloff edge. **KelvinGrab** (an elastic Move: tri-scale kernel, 1/r^5
  falloff), **KelvinTwist** (press and drag right to turn clockwise around that point; every ring turns rigidly,
  so volume is kept), **KelvinScale** (drag left to pinch, right to swell; integrated as a radial flow, so it never
  folds through the centre) and **KelvinSnake** (a multi-scale SnakeHook whose pull narrows as it travels, so
  horns taper by themselves). **Brush > Kelvinlet & Capture**: **Squash** is Poisson's ratio, from rubbery (0) to
  incompressible clay that bulges around the thumb (100); **Sharpness** a tighter, cusped peak with the same
  reach. Symmetry (the twist mirrors as a pseudovector), masks, hidden parts, sculpt layers, pressure and undo work
  as with every brush. Each move touches only the points inside an adaptive cutoff (where the move is still more
  than 4% of its largest, the last part faded smoothly to 0), evaluates the kernel from a radial table, works out a
  fast drag in pieces in one pass, and passes on only what moved to undo, normals and the BVH. KelvinGrab and
  DetailClone are on the bottom shelf; all five are in the brush picker, the Library and the `stroke` command.
  On dense meshes a long move is applied at most about a brush radius per frame (the rest in the next frames),
  the far field is evaluated on a coarse lattice and, over 400k vertices, deferred to a world lattice that a
  sixth of the vertices take each frame (all of it when the stroke ends), and the cutoff tightens up to 10% of
  the peak over a vertex budget. Twist and Scale at 33M / 50 px: 57 ms frames (was 110-280); KelvinGrab still
  about 190 ms there (renderer sync), see docs/performance.md.
- **Capture from view** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>C</kbd>, askNK's request): drag a square (Shift: a
  circle) over any sculpted detail and get, side by side, a **grayscale height alpha** (height toward the viewer
  over a fitted plane, normalized, optionally detail-only) and a **vector displacement map**: each point's true 3D
  offset from a smooth base (the broad form as a fitted quadric, the rest by implicit fairing with the rim held),
  in the base's own tangent frame, so overhangs and sideways leans come along. Options: Base Smoothing, Edge Fade,
  Resolution, Fit plane, Invert. **Use as Alpha**, **Use as VDM**, **Detail Clone** or **Save to Library** (the
  alpha as a PNG in the Library Alphas folder, the VDM in the brush library; VDMs now show in the Library Brush
  tab with their thumbnails). Commands `capture_alpha` and `capture_vdm`.
- **DetailClone** brush: stamps the VDM brush's map along the stroke, one copy every stamp width (Clone Spacing),
  each turned to follow the stroke (Clone follows stroke) and shaped to the surface under it; a click stamps one.
- `new_bench_sphere millions=N` makes the benchmark's sphere of N million triangles (with subdivision levels),
  and `SF_MEASURE_BRUSH=<brush>` makes `--measure-stroke` measure another built-in brush (`SF_KELVIN_LOG=1` logs
  each Kelvinlet move: pieces, vertices, kernel / normals / BVH milliseconds).
- Tests: tests/test_kelvinlet.cpp (kernel normalization and far-field cancellation, divergence against Squash,
  bulge, exact twist, pinch without folding, symmetry, mask, undo, sculpt layers, threads, Detail Clone, the
  captures with a VDM round trip within 9% RMS and a curved-surface capture; a dense-mesh timing check with
  SF_BENCH_LIMITS=1) and the UI test kelvinlet_capture.

### Changed
- Make sculptable still remeshes with the legacy method for now (QRemesh 2 flattened part of a thin handle on x86).
- The 0.9 QRemesh is kept as **Method > Legacy** (`qremesh method=legacy`). Closed QRemesh Guide loops (round an
  eye, or across the symmetry plane) still use it for that piece for now, and a piece QRemesh 2 can't do cleanly
  falls back to it; the status line says so.

## [0.10.1] - 2026-11-01

0.10.0 was tagged but never published (its Windows build failed); everything in it ships here.

### Added
- **Local API for other programs** (#36): Preferences > Input > Local API (or `SF_LOCAL_API=1`) serves commands to
  programs on this computer on 127.0.0.1 with a token in `api.json`; new `mesh_read` and `mesh_set_positions`
  read the active Part's coordinates and write them back as one undoable step (topology checked).
- **Maya navigation style** (#32): Preferences > Input > Maya: Alt+left orbits, Alt+middle pans, Alt+right zooms;
  Ctrl inverts, Ctrl+Alt masks.
- **Focal length** (#26): Draw > Camera > Focal Length (15-300 mm, full-frame) with 35 / 50 / 85 / 135 presets,
  saved with the project.
- **More primitives** (#31): Append > Cylinder, Cone, Torus, Capsule, Icosphere, Quad Sphere (quads, one face group
  per part); **Insert on the model** (#34): pick a primitive in Append > Insert and drag on the surface to add it
  along the normal, sized by the drag (`insert_primitive`); **Continuous** stroke option (keeps dabbing while the
  brush rests); an **X / Y / Z axis gizmo** in the canvas corner (Draw > Camera > Axes).
- SculptForge remembers its window size, position, maximised and full-screen state (#35).

### Fixed
- The viewport could keep old frames on screen (cursor trails, popups burned in, the mesh not drawn) on some
  drivers, NVIDIA on Windows among them (#29): the view now resets the scissor, colour and depth write state before
  it clears.
- Windows builds: proof mode's `small` / `near` names clashed with Windows headers.

- **Micro levels: hundreds of millions of sculptable polygons** (**Tool > Geometry > Micro**, `micro_*`
  commands). Above the highest subdivision level a Part can carry a micro level: per triangle a regular grid of
  16-bit heights along the interpolated normal of the smooth (Phong-tessellated) base, instead of a mesh.
  300 million micro triangles take 374 MB (1.25 bytes each; the whole app about 0.8 GB), where a mesh of that size
  would need about 27 GB. Unlike high-detail geometry the whole model is sculptable at once.
  - Brushes: Standard, Clay, Clay Block, Smooth, Inflate, Flatten, Trim, Layer, Pinch / Crease / Sharp Crease
    (as height sharpening), Mask; symmetry, Falloff, pressure and the mask apply. Big brushes are evaluated on
    a coarser grid and interpolated (scale-adaptive). Move reshapes the base below the micro level and the detail
    rides along (dual scale); "Move along normals" moves the micro surface itself.
  - Viewport: the micro surface is evaluated on the GPU per frame, with a level of detail per cluster and a
    triangle budget (2M on a laptop profile, 8M on a workstation, `RenderSettings::renderTriangleBudget`), crack
    free; the shading keeps every micro detail. 300M micro triangles orbit at 70+ fps with 4x antialiasing (M1 Pro).
  - Bake Micro (to a mesh), Export Displacement (32-bit EXR or 16-bit PNG, scalar or vector, over the base UVs).
  - Undo: a packed journal per stroke (tiles in Morton order, each tile's change predicted and packed).
  - Very large levels stay out of core in a sparse memory-mapped file (only what you touch is in memory).
  - Projects: format 7 when a Part has a micro level (others stay format 6 for older versions).
  - `sculptforge --bench-micro --bench-sizes 33,100,300` measures it. Guide chapter "Micro levels".
- Automatic hardware detection and calibration. On first launch, and again when the graphics card, its driver or
  the version changes, SculptForge spends about a second or two (under a small overlay; Esc skips) measuring
  triangle throughput, upload bandwidth, brush speed and memory bandwidth, and probes the GPU (OpenGL strings,
  video memory through GL_NVX_gpu_memory_info, GL_ATI_meminfo, GLX_MESA_query_renderer, amdgpu sysfs, DXGI or
  CGL), RAM, threads, SIMD and OS. The result picks a profile: **Laptop / Budget**, **Workstation** (64 GB+ RAM
  with a 16 GB+ discrete GPU, or Apple Silicon Max / Ultra and big APUs, that measure fast) or a **Software** safe
  mode for llvmpipe, with a render triangle budget, motion proxy
  threshold, GPU memory and upload budgets, texture, subdivision and micro-triangle limits and the thread count.
  Cached in the settings folder; no network use. A one-line status bar message says what was found.
- Preferences > Performance shows what was detected and measured ("Detected <GPU>, <memory> -> Workstation"),
  the settings in effect, **Re-calibrate** and manual overrides (kept across re-calibrations).
- An adaptive frame governor holds the view at 60 fps (120 / 144 on a fast display) while it moves, by switching
  big scenes to their motion proxy sooner or later, with hysteresis; it changes only what is drawn, never the
  model. Fast Navigation's 10 million triangle threshold now comes from the profile.
- On battery (laptops), brushes switch to draft mode, uploads per frame halve and the target is 60 Hz.
- `sculptforge --calibrate` prints the profile as JSON (schema sculptforge.calibration/1); commands
  `performance_profile`, `calibrate` and `set_performance_override` for the console, scripts and MCP.
- For developers: `core/TierConfig.h` (resident cache cap, active-triangle budget, upload budget, draft brush
  mode, zero-copy UMA, target Hz) and `renderTriangleBudget()` for the renderer and micro-displacement
  (docs/performance.md, "Hardware profile").
- Material painting (PBR): a painted texture can carry Roughness, Metallic, Normal, Height (16-bit), Ambient
  Occlusion, Emissive and Opacity besides its colour, all painted in the same strokes. Turn channels on in
  **Tool > Texture Map > Material Channels** (`texture_channels`); a channel costs no memory until it is painted
  (64x64 copy-on-write tiles; untouched tiles aren't stored). **Color > Material** sets what the brush lays on
  each channel (material swatches, per-channel value swatches and sliders; `paint_material`), and **C** picks the
  whole material under the cursor. Paint layers hold every channel: channel chips on each layer row turn a
  channel on or off for that layer, and give it its own blend mode and opacity, including **Normal Combine** for
  normals (`texture_layer_channel`). Height also yields a normal combined onto the Normal channel. One undo step
  per stroke, kept per layer and channel. Auto UV, Voxel Remesh, QRemesh and merging Parts carry every channel.
  See the guide's Material painting page.
- Solo channel view: **Tool > Texture Map** shows one channel on the model (roughness and the other scalars in
  grey, the normal as colours, emission in colour), live while painting (`channel_view`).
- Export Maps with presets (`export_maps`): Unreal (`T_*_BC`, `T_*_N` in DirectX, `T_*_ORM`), Unity URP and HDRP
  (mask map), glTF, Godot, Blender (one file per channel) and USD, plus your own JSON presets (the guide's Export
  presets page). Height maps are 16-bit PNG.
- Model exports wire the channels into their materials: glTF metal-rough slots (packed occlusion / roughness /
  metallic, normal, emissive, alpha from opacity), UsdPreviewSurface inputs, the MTL PBR keys and FBX material
  maps. Send to Unreal imports `T_<Name>_BC / _N / _ORM` and builds an `M_<Name>` material from them.
- Preferences > Performance and `perf_info` show the memory of textures and their material channels.

### Changed
- Texture painting on dense meshes costs the texels a dab reaches instead of the triangles under it: the brush
  finds the texture tiles under it from the BVH and paints each texel from a cached triangle-and-barycentrics
  map, tiles in parallel. A 0.35-radius dab with X symmetry on a 21M-triangle sphere: 4K 150-360 ms -> 24-39 ms,
  8K 240-350 ms -> 39-55 ms (M1 Pro, ranges over a busy and a quiet machine); results match the old dab within
  1/255. Lighter meshes keep the per-triangle dab. No first-dab stutter: the UV bins are shared by every texture on
  the same UVs (undo steps, copies) and built in parallel when painting starts (about 45 ms at 21M), the area under
  the hovering brush is readied before it lands, and a stroke into a fresh area stays under 20 ms at 4K.
  Preferences > Performance counts these caches.
- Project format 7 stores material channels. It is written only when a texture has them: other projects are
  still saved as format 6, and every older project opens as before.
- **Sculpt Timeline**: every stroke and operation is a tick on a strip along the bottom of the canvas. Drag the
  scrubber to see the model at any point of the session, click a step to go there. Sculpting from an earlier
  point no longer throws the later steps away: they stay as a branch (a lane above the track) you can switch
  back to, with undo / redo working on the active branch as before. Right-click a stroke to re-tune its strength
  (0 removes it, 2 doubles it) without redoing the strokes after it; where later strokes overlap it they keep
  their own displacement on top (they aren't sculpted again; see the guide's Sculpt Timeline chapter). Seeking
  reuses undo's per-stroke deltas, swaps runs of strokes at once with one normal / BVH / GPU update, and jumps
  from full checkpoints every 16 steps (within 1 GB; fewer on huge meshes) for long seeks. Each Part keeps
  its own timeline. Commands `timeline_info`, `timeline_seek`, `timeline_select`, `timeline_retune`,
  `timeline_save`, `timeline_load`; UI test timeline.
- **Save with project** (in the timeline's header) also writes the history beside the project as
  `<name>.sfhist` (strokes, masks, vertex paint, visibility, face groups, branches, re-tuned strengths; a topology
  change cuts it). Opening the project brings it back when the mesh matches. The project format is unchanged.
- **Export timelapse**: the history rendered as a turntable video, the model building up stroke by stroke while
  the camera goes once around it (1080p or 4K MP4, no captions; needs ffmpeg). Also
  `sculptforge project.sfproj --timelapse out.mp4 [--size 3840x2160] [--timelapse-seconds 20]`.
- **Proof mode** (**Help > Benchmark**, `sculptforge --benchmark`): a scripted 240 Hz pen stroke through the
  same input handlers a real pen uses, with a 60 px and a 240 px brush and a turn of the view, on 5, 20 and 33
  million triangle meshes (100 million when free memory allows). A live HUD shows fps, frame time p50 / p95 /
  p99, how far the stroke trails the pointer (ms and px) and memory; the run is recorded to an MP4 with the HUD in
  it and written as a JSON result with the processor, graphics card, memory, OS, version and commit.
  `docs/benchmark.md` describes the format, `tools/benchmark/submit.py` checks results and adds them to
  `website/benchmarks/results.json`, which the new benchmarks page lists.
- The brush cursor follows the surface (#25): over the model the Size and Falloff rings
  lie on the surface under the pointer, turned to its (smoothed) direction and pressed onto its shape, at the
  brush's real size there, with a short normal line; with symmetry on, a fainter mirrored ring shows too. Every
  brush (sculpt, paint, mask, dynamic topology). A few dozen BVH ray casts per drawn frame, no pass over the mesh
  (0.09 ms on 5M triangles), and the idle view still draws nothing. **Preferences > Input > Cursor follows
  surface** (on by default) brings back the flat screen rings.

- **Predictive Strokes** (Preferences > Performance, on by default): a sculpt stroke's brush is worked out on its
  own thread while the view keeps drawing at the display's rate. The dabs not finished yet are drawn as a GPU
  preview on the surface (each brush's kernel in the vertex shader, with its shading and cavities), and the brush
  cursor and the stroke's head are drawn where the pen will be a frame later. When a dab's result reaches the GPU
  it replaces its preview in the same frame. Lifting the pen never waits. The finished sculpt is the same, bit for
  bit, as with it off (`--measure-stroke` with `SF_MEASURE_VERIFY=1` replays the stroke and checks undo).
  Painting, masking, dynamic topology, Move, Snake Hook and the drag strokes work as before.
- **Draft Strokes** (Preferences > Performance, off by default; meant for laptops and battery): with Predictive
  Strokes, the brush only moves the surface during the stroke; normals, cavities and culling bounds are worked out
  once when the pen lifts.
- `tools/bench_strokes.sh`: the big-brush stroke benchmark on 5M, 21M and 33M triangles, one build or two
  interleaved (docs/performance.md). `new_sphere triangles=N` makes a sphere of about N triangles.
  `--measure-stroke` reports frame p95 / p99, the preview's lag, and when the first and last dab reach the screen.

### Changed (predictive strokes)
- Big brushes on dense meshes no longer slow the view. `tools/bench_strokes.sh`, 200 px brush, M1 Pro shared
  with other builds (load 9-45, so both columns are slower than on an idle machine; A/B interleaved):

  | | 0.9.7 frame mean / p99 | Now | Brush on screen behind the pointer, 0.9.7 / now | Press | Release |
  |---|---|---|---|---|---|
  | 5.2M | 47 / 134 ms | 8.4 / 14 ms | 95 / 1 px | 98 -> 24 ms | 31 -> 0 ms |
  | 21M | 71 / 651 ms | 8.8 / 18 ms | 37 / 1.5 px | 623 -> 14 ms | 1 -> 0 ms |
  | 33.6M | 169 / 323 ms | 12.2 / 27 ms | 245 / 4 px | 315 -> 13 ms | 374 -> 0 ms |

  With 60 and 240 px brushes at 21M and 33.6M the p99 frame is 11-31 ms (0.9.7: 26-636 ms). The solved surface
  itself still trails as before; the preview covers it (docs/performance.md, "Predictive strokes").
- Packing a stroke's vertices for the GPU no longer walks every vertex's neighbours again: the cavity term comes
  from the normals pass. Clusters a stroke moved get quick culling bounds, made exact when it ends.
- `Mesh::recomputeAllNormals` (new meshes, undo of big changes, timeline seeks) runs in parallel.

### Fixed (predictive strokes)
- A big brush that raised the surface past the GPU's position grid stalled the view for seconds on dense meshes
  (the whole mesh was re-packed inside a frame); it is re-packed on the brush thread now.

## [0.9.7] - 2026-10-31

Speed first: big brushes on dense meshes take a third less time per frame, trail the pen by half as far and no
longer pause when you lift it. Also from your reports: a right click opens the QuickMenu (#22), dynamic topology
with symmetry remeshes both sides of an asymmetric mesh (#23) and the orthographic view shades correctly (#24).

### Added
- A right click on the canvas opens the QuickMenu under the cursor (#22). Pressed and let go
  without moving more than 4 px, beside the model or on it; the view waits until the press moves past that, so
  a click never nudges it, and a right drag turns, pans or zooms as before (by everything it moved, nothing
  lost). A pen button set to Right click works the same; SDL's own mouse click for another pen button doesn't
  open it. On by default in every input style (a right click without a drag did nothing before);
  **Preferences > Input > Right click opens the QuickMenu** turns it off (`rightClickQuickMenu` in ui.json).
  UI test right_click_menu (mouse, tremble, drag, pen button, Blender style, preference off).

### Changed
- Large brushes on dense meshes are faster and stay closer to the pointer. Measured on a 21M-triangle sphere
  with a 200 px brush (`--measure-stroke 3,200`, M1 Pro):

  | | 0.9.6 | Now |
  |---|---|---|
  | Frame | 98 ms | 65 ms |
  | Brush behind the pointer, mean / worst | 293 / 544 px | 137 / 189 px |
  | Press to first dab on screen | 361 ms | 157 ms |
  | Release | 990 ms | 67-130 ms |

  The 33M-triangle benchmark's large Standard dab goes from 44.6 to 41.8 ms; the other rows are unchanged.
  - The stroke queue keeps about 50 ms of dabs, at least 4 (it kept at least 24: a second of work behind the
    pointer at 21M, all placed on release).
  - The BVH refit of a big dab recomputes the ancestors of its leaves in parallel (13 ms on one thread before).
  - A dab's normals are recomputed for the corners of the BVH leaves its queries listed instead of every moved
    vertex's neighbour ring (7 ms a dab on levels made by Divide).
  - The GPU upload sends the changed blocks until three quarters of the mesh changed (it re-sent the whole
    mesh above a quarter: 78 ms a frame against 14-30), and finds them in parallel (3.7 -> 0.4 ms).
  - Starting a stroke no longer re-uploads the whole mesh when there are no LODs.

### Fixed
- The orthographic view (Numpad 5) shaded Lit Spheres and the lights as if inside out (#24): the shader worked out
  each pixel's view ray as in a perspective view, which in an orthographic one tilted it far off the axis
  towards the edges. An orthographic view now gives every pixel the axis; flat shading turns its facets
  towards the viewer by that ray too (the direction from the eye, which an orthographic view can put inside
  the model, turned them away). UI test ortho_shading (a new `luma@X,Y` value reads the last screenshot): the
  same sphere in both views matches within 1 brightness level, where 0.9.6 was up to 38 off.
- Dynamic topology with symmetry remeshes the mirrored side of a model whose sides aren't mirrored polygon for
  polygon (#23; a QRemeshed head in the report). Splits, collapses and flips are found under the brush and done
  on the mirror images of the same edges; without images (nothing to pair the vertices with) the other side only
  moved and kept its coarse triangles: a stroke left 2,199 triangles along its path and 33 along its mirror image.
  An edge under a mirror image of the brush whose image on the brush's side is missing is now remeshed where it
  is (2,260 there). Symmetric meshes still get exactly mirrored topology (the 1M dynamic topology benchmark's times
  are unchanged). Test `dyntopo_symmetry_remeshes_both_sides_of_an_asymmetric_mesh` (a turned sphere, then
  random strokes with X and Z symmetry, valid after every dab, undone exactly).

## [0.9.6] - 2026-10-31

The same evening as 0.9.5, the next batch from your reports and from askNK's review: Extract and Mirror And Weld,
a flat background, a Flatpak, strokes that keep their shape on dense meshes (built on a patch by @bribblej-web),
and a fix for Divide crashing on models from Blender, with a log file so a crash is never silent again.

### Added
- Extract (Tool > Part > Extract): the masked, visible part of a Part becomes a shell with Thick (a share of the Part's size; negative grows inward, 0 makes a single-sided surface), E Smt (smooths the stepped edge) and Double (both ways). Extract shows a preview below the source that follows the settings; Accept keeps it as a new Part with outer, inner and rim face groups and the source's vertex paint, as one undo step (undo removes the Part, redo puts it back). TCorne and TBorder make the rim triangles instead of quads at sharp corners or all along it. An unaccepted preview goes when the source changes and is never saved. Commands: extract, extract_accept, extract_cancel.
- Mirror And Weld (Tool > Geometry > Modify Topology, X / Y / Z): keeps the + side of the plane through the origin, cuts polygons crossing it, mirrors the half and welds the seam into one watertight mesh, keeping face groups, vertex paint, mask, UVs, quads and sculpt layers (`mirror_and_weld`, with `side=negative` for the other half). Undoable.
- A flat viewport background (#21): Preferences > Display > Background (also in the Render palette) switches between the gradient (the default) and Flat Colour with a colour picker. Saved automatically; projects keep their own, and `set_background` (mode, color) scripts it.
- Mirror (Tool > Deformation, X / Y / Z): flips the Part across the plane through the origin, or in place with `pivot=center` (`mirror`). Undoable.
- A log file, `sculptforge.log` in the user data folder (`%APPDATA%\SculptForge\SculptForge` on Windows,
  `~/Library/Application Support/SculptForge/SculptForge` on macOS, `~/.local/share/SculptForge/SculptForge` on
  Linux), so a crash is never silent: it holds the version, system and GPU, everything SDL_Log reports (the
  Windows build has no console), each command run (written before it runs), and on a crash a note with the
  cause, the last command and, on Linux and macOS, a stack trace. Each start keeps the previous session's log as
  `sculptforge-previous.log` and notes when that session didn't close normally; the restore dialog after a
  crash points to it. **Help > Open log folder** opens the folder; `--log-dir` writes the log elsewhere, and
  `--crash-test segv|abort|throw` crashes on purpose to test the note (ctest `crash_log`).
- A Flatpak (#18): `SculptForge-<version>-x86_64.flatpak` and `-aarch64.flatpak` on every release (with
  version-less copies, `SculptForge-linux-x86_64.flatpak` and `SculptForge-linux-arm64.flatpak`), built from
  source on the freedesktop 26.08 runtime by `packaging/flatpak/org.sculptforge.SculptForge.yml`
  (`packaging/flatpak/build.sh` builds it locally). Sandbox: Wayland or X11, OpenGL, devices (for the SpaceMouse),
  network and the home folder. In a Flatpak the Blender bridge uses the host's folder and tells the add-on to start
  the app through Flatpak's launcher; the add-on also looks for that launcher. A package for the FlatPark
  repository, wrapping the release tarballs, is ready in `packaging/flatpak/flatpark/`.

### Fixed
- Strokes on dense meshes keep their shape (#19). When one dab cost more than a frame, the app placed every dab
  of a long pointer movement before reading the pointer again, so the next movement was longer still and a fast
  curve came out as a straight line. Dabs now wait in a queue that the app works through for 10 ms a frame,
  between reading the pointer and drawing; a long gap between pointer samples is drawn as a curve through the
  last samples, not a chord; and when the queue holds more than 24 dabs (or more than 0.05 s of work, whichever
  is larger) every second waiting dab is dropped, so the brush stays near the pointer. Measured with the new
  `--measure-stroke 3,200` (a fast two-turn circle, 200 px brush) on a 21M-triangle sphere: dabs were up to
  69 px off the circle before and are within 4 px now, at 82 ms frames instead of 143 ms. Light meshes place
  the same dabs as before. Based on a patch by @bribblej-web in #19. `SF_STROKE_IMMEDIATE=1` places every dab
  at once as before; `SF_STROKE_BACKLOG=N` sets the queue limit in dabs (0: never drop).
- The AppImage's root icon and `.DirIcon` are 512 px PNG files, not links to the SVG (#17), so AppImage managers
  such as AppManager and Gear Lever show the icon when they integrate it, and the current appimagetool accepts
  the AppDir. The AppImage is now built with appimagetool directly (packaging/linux/build-appimage.sh), which
  checks the AppDir before packing it.
- Divide crashed on models with a vertex shared by 22 or more triangles (#16), such as a cylinder sent from Blender
  with Triangle Fan caps of 22 sides or more (21 worked) or Blender's UV sphere. The first subdivision level
  works out each vertex's neighbours from the base mesh; around a high-valence base vertex the list was gathered
  with every polygon twice before removing the repeats, which overran its 64 slots (3 x valence) and corrupted
  the stack when the viewport shaded the new level. The repeats are now removed before the list is joined, so
  it never holds more than 2 x valence (implicit adjacency is only used up to valence 32; above that the stored
  adjacency has no limit). Unit tests divide fan cylinders of 3 to 300 sides and UV spheres up to 200 segments,
  through OBJ and through the Blender Bridge, and compare every level's adjacency with a plain rebuild; a UI test
  receives the issue's meshes from the bridge and divides them in the app.
- Generating with a ComfyUI workflow and a seed read freed memory, which could crash at random or set the wrong
  seed: while setting a node's seeds, the node was replaced after its first seed but its inputs were still read
  for the second (a sampler with both `seed` and `noise_seed`). Found with AddressSanitizer; the keys are now
  chosen before any is set. The unit test now has a node with both seeds.

## [0.9.5] - 2026-10-31

The day after launch, people filed ten issues. 0.9.5 answers all of them: Forge Spheres for blocking out a base mesh,
a Blender input style, flat shading, your own Lit Spheres, SpaceMouse navigation (experimental), an idle app that no
longer keeps the graphics card busy, and fixes for pens on Windows and Linux, the S slider and the Blender Bridge.
Thank you for the reports.

### Added
- Forge Spheres (Tool > Forge Spheres, #8): block out a base mesh from connected spheres. Draw grows a sphere from another (the drag sets its size) or adds a joint on a link, Move and Scale set the proportions, Alt+click deletes, Symmetry mirrors every edit, Preview (A) shows the skin, and Make Mesh builds it as a new watertight quad Part with a face group per link, keeping the Forge Spheres hidden to edit again. Undoable, saved in projects, and scriptable (`forgesphere_new`, `forgesphere_add`, `forgesphere_set, forgesphere_delete, forgesphere_insert, forgesphere_preview, forgesphere_make_mesh, forgesphere_list`).
- A Blender input style (#12): **Preferences > Input > Style > Blender**, also at the top of the Controls window.
  Middle-drag orbits, Shift+middle pans, Ctrl+middle zooms, and Alt+left-drag does the same for pens and
  trackpads (Emulate 3 Button Mouse); Ctrl inverts a stroke, Shift smooths; F / Shift+F set the radius and
  strength; Blender 4.2+'s Sculpt Mode keys pick the nearest brushes (V Draw, S Smooth, P Pinch, I Inflate,
  G Grab, C Clay Strips, Shift+C Crease, K Snake Hook, Shift+T Scrape as Flatten) and M the Mask brush
  (Ctrl+drag unmasks); Alt+M / Ctrl+I clear / invert the mask; H / Shift+H / Alt+H hide, isolate and show
  face groups; Alt+1 / Alt+2 step the subdivision levels; Home frames everything. Choosing it sets Rotation to
  Turntable. **Emulate Numpad** makes the number row work as the numpad.
- **Import Blender keymap...**: reads a keymap exported from Blender (the .py file, as text: it is never run) and
  takes the 3D View and Sculpt keys SculptForge has an equivalent for. Also the `blender_keymap_import` command.
- Numpad views in every input style: Numpad 1 / 3 / 7 front, right, top (Ctrl: back, left, bottom), Numpad 5
  orthographic, Numpad 4 / 6 / 8 / 2 orbit in 15 degree steps, Numpad 9 the other side, Numpad . frames the
  active Part, Numpad Plus / Minus zoom.
- An orthographic view. The axis views turn it on until the view is rotated again (Blender's Auto Perspective).

- Custom Lit Spheres (#15): **Material > Load Lit Sphere...** loads a lit-sphere image (PNG, JPG or EXR; non-square images
  are cropped to the centre, large ones scaled to 1024) as a material and copies it into the new **Lit Spheres** folder
  of the library (Documents/SculptForge/Lit Spheres); images in that folder load at start-up and, when added later,
  the next time a material list opens (a cheap size and time check), and folders listed in `SCULPTFORGE_LITSPHERE_PATH`
  load at start-up. They show in the material picker, palette and Library. Right-click > **Remove** takes one out
  of the lists and moves its image to the trash (freedesktop trash on Linux, Recycle Bin on Windows; on macOS it
  is hidden for the session and the message says where the file is). A project
  using one embeds it as a PNG (an `attachments` entry; no format change), so it opens with the same look anywhere.
  Scripts: `load_litsphere path=...`, `remove_litsphere name=...`, `set_material name=<your LitSphere>`.
- Smooth / Flat shading (#11): **Material** palette (and **Tool > Display**) > **Shading** switches the whole
  scene to flat, faceted shading, where every polygon shows its own face; **This Part** keeps one Part smooth
  or flat whatever the scene says. Both are saved in projects (no format change; older files open with your
  preferred shading) and the scene's choice is a preference (saved automatically). Smooth stays the default. Scripts:
  `set_shading mode=flat`, `part_shading mode=smooth name=Eyes`.
- 3Dconnexion SpaceMouse navigation (#6, experimental): the cap's six axes pan, zoom and turn the view about the
  orbit pivot, continuously and at the same speed whatever the frame rate, in every input style and while a pen
  stroke is held. Read through SDL's joystick API (no 3Dconnexion SDK), hot-plug aware, recognised by USB id
  (vendor 256F, and the older Logitech-id SpaceNavigator / SpacePilot / SpaceExplorer / SpaceMouse Pro).
  **Preferences > Input > SpaceMouse**: on/off, speed, dead zone, per-axis invert, Lock rotation to turntable,
  Dominant axis, live axis bars, and an action per button (the two side buttons default to Frame and Front view);
  ui.json keeps them (`spaceMouse`). Untested on real hardware; the guide explains what to do when
  3DxWare (Windows, macOS) or spacenavd (Linux) holds the device. UI tests drive a virtual joystick with a
  SpaceMouse Compact's ids (`spacemouse`, `spacemouse_hold`, `spacemouse_button`, `spacemouse_unplug` steps).
### Fixed
- The Blender Bridge add-on zip was missing its classic keys module, so Blender refused to install it (#10). The zip now carries every module, and the packaging check fails if one is left out.
- Rotating, panning and zooming with the pen tip on the background only shook the view with some tablets (a Ugee
  M708 on Linux, #7): their drivers send stray mouse events between the pen's, and the view followed both. The
  pen now moves the view by its own motion only, and lifting the pen ends a view drag as it ends a stroke.
- The S / U / I / O slider closed as soon as you grabbed it, and could open away from the cursor (#9, seen on
  Windows). It now opens with its knob (the current value) right under the brush cursor, at any UI scale and
  with a pen, and stays open while you drag until you let go, click elsewhere or press Esc.
- B, M, S and the Library key followed the built-in key even after you gave the action another one.
- Pens through Windows Ink (a Wacom Intuos Pro, #13): taps on menus and buttons often missed, Alt+pen drag and the
  Move button threw the model off the canvas, and the S / U / O slider could not be dragged. Windows Ink and some
  drivers send mouse events of their own beside the pen's, at an old position; the interface followed them as
  well as the pen. While the pen is in range the interface now follows only the pen (as the canvas already
  did), and the Move / Zoom / Rotate buttons move the view by the cursor's own steps.
- SculptForge kept the graphics card and a little of the processor busy while it sat idle (#14): it drew a new
  frame 60 or more times a second whatever happened. It now draws only when something changes: your input,
  a stroke or view move, a background job (progress bars, splat training, the update check), a message from the
  phone or the Blender Bridge, or an animation. Idle, it draws nothing (measured: 120 frames a second and 8% of
  a processor core before, 0 frames and 0.1% after). UI tests and `--record` still draw every frame.
  `sculptforge --measure-idle 5` prints the idle frame rate; `SF_CONTINUOUS_REDRAW=1` draws every frame as before.

### Changed
- The release builds use SDL 3.2.30 (was 3.2.10), which fixes Wacom tablets on Windows: clicks dropped with
  Windows Ink off, and the barrel button pressed while hovering arriving as a left click. On Linux (X11) it also
  stops the pen's own events from arriving a second time as mouse events.

## [0.9.4] - 2026-10-29

### Fixed
- Builds at other optimisation levels (Arch and Omarchy packages build at -O2) failed two face-shape tests: the core library no longer fuses multiply-adds, so results are the same at any level.

### Changed
- Linux AppImages are named the AppImage way (`SculptForge-<version>-x86_64.AppImage`, `-aarch64`) and carry update information, so AppImageUpdate can update them; the version-less download links are unchanged.
- Exports without a material of their own (FBX, USD) and splats made from an unpainted sculpt use the Soft Clay grey instead of the old peach.

## [0.9.3] - 2026-10-28

A cleaner look: the default material is a matte Soft Clay on a charcoal background, orange marks only the
current tool, and the bottom shelf's brush tiles are bigger, with a size you can choose.

### Added
- Bottom shelf tile sizes: Small, Medium (the new default, bigger than before) and Large. Right-click a brush on
  the shelf and choose **Tile size**, or use **Preferences > Interface > Shelf tiles**.
- **File > Export STL...** as its own menu item (it was inside the Bridge submenu).

### Changed
- Soft Clay is the default material: matte, with a clear light and shadow side and a soft rim, so sculpted
  detail reads at a glance. It is first in the material picker; a material you picked before is kept.
- Orange now marks only the current tool or mode (Draw, Sculpt...). Options that are on (Add, symmetry X,
  Pressure > Strength, Mask, Paint...) are a lighter grey with a thin orange line.
- Every brush icon is drawn the same way, with a mark showing what the brush does.
- The right tray's sections, the selected Part and the colour picker have a calmer, tidier style.

### Fixed
- The Alpha palette showed **Load image...** and **Invert loaded image** twice.
- The update check also asks the website's GitHub copy (islandmed.github.io), so it works on networks that
  block sculptforge.app.

## [0.9.2] - 2026-10-28

View in AR on Android works again, through the website instead of Google's viewer being handed a home-network
address.

### Fixed
- Android View in AR: it handed Google's Scene Viewer the model's address on your computer, which Scene Viewer
  can't load (it only takes public HTTPS addresses), so the phone just downloaded the file. It now opens an AR page
  on sculptforge.app in Chrome. The model goes there encrypted through SculptForge's relay (link.sculptforge.app;
  the key is only in the link, the copy is deleted after 10 minutes), so it also works on phones whose Chrome can't
  reach the home network at all; without internet it comes straight over the Wi-Fi link instead. Show it with
  Chrome's AR, or with Google's AR viewer, which needs a readable copy on the relay (only when you choose it, also
  deleted after 10 minutes). It updates by itself as you sculpt. From Firefox, the page offers Open in Chrome
  (Firefox has no AR).
- Phone link (face capture, AR): one network port for everything (the page's port, now for UDP too), so a firewall
  needs one rule: allow the port for TCP and UDP from your home network (the window and the guide say how).
  `SCULPTFORGE_RTC_LOG=1` (or `2` for every packet) logs the connection for troubleshooting.


## [0.9.1] - 2026-10-27

Help for phone capture when the phone can't open the page, and bigger text in the guide and the update banner.

### Changed
- Easier reading: the User Guide (and its What's New chapter) and the update banner use 17 px text instead of
  the 15 px interface font, with larger headings.

### Fixed
- Capture from phone tells you what to do when the phone can't open the page: Android Chrome can refuse every
  home-network address ("This site can't be reached", ERR_TOO_MANY_RETRIES), so use Firefox on the phone; and Linux
  firewalls (ufw, firewalld) need the port allowed. In the window and in the guide's Phone capture chapter.
- Baking: Cancel is honoured on small bakes too.

## [0.9.0] - 2026-10-26

Maquette to performance: scan with your phone, make the scan sculptable, give a head its facial shapes and
drive them with your own face.

### Make sculptable (splat capture -> sculptable, animation-ready mesh)
- Splats palette > **From scan > Make sculptable**: one click turns the capture into a new Part next to the
  splats (which are hidden, not deleted): a watertight, manifold surface (opacity-weighted Gaussian density on a
  voxel grid, built in parallel z slabs with the same result on any thread count; gaps closed; floater shells
  dropped; space boxed in on 5 of 6 sides filled, which seals open bottoms; largest piece kept), QRemesh quads at
  a **Game** (4,000), **Film** (12,000) or **Sculpt** (2,000 + 2 subdivision levels with the scan projected onto
  the top) budget, and the capture's colours as vertex paint or an Auto UV texture (splats inside the subject are
  faded out). A hidden "<name> Scan" Part keeps the dense surface for ProjectAll.
- **Face loops**: a face is looked for on the scan from the current view (depth and colour, no ML model, no
  download): eye corners, nose tip, mouth corners and chin become QRemesh Guides (rings round the eyes and the
  orbits, the lips, the nasolabial loop), mirrored, with QRemesh symmetry. **Review** stops after the scan to
  drag the landmarks (the mirror follows) or **Place by hand**, then **Finish**.
- **Crop Box** with draggable face handles in the viewport; options for Detail, Min Opacity, Close Gaps, Fill
  holes, All pieces, Keep scan. Progress bar and Cancel; Ctrl-hover help; F3 entries.
- Commands: `splat_to_mesh` now runs the whole pipeline (`detail`, `preset`, `target`, `levels`, `face_guides`,
  `landmarks`, `front`, `up`, `color`, `texture_size`, `crop`, `fill_holes`, `keep_parts`, `keep_scan`, `stage`
  scan/finish, ...; `resolution` still works) and adds Parts instead of replacing the sculpt; `face_guides`;
  `splat_test_capture` (a built-in test head or mug capture, no download).
- 5 million splats to Film quads with face loops in about 9 s, 0.9 GB (M1 Pro).
- Guide chapter "From scan to sculptable mesh"; UI test `make_sculptable.sfui`; UI tests: `drag_landmark` verb,
  `hover` scrolls a panel to the item as `click` does.

### Added
- Facial shapes (Tool > Facial Shapes, `face_shapes_generate`): the 52 Apple ARKit blend shapes, exactly named, as
  sculpt layers on the active Part, from a built-in template head (ICT-FaceKit's ICT Face Model Light, MIT):
  automatic or landmark-guided fit (similarity + non-rigid ICP), deformation transfer (Sumner and Popovic 2004) so
  the shapes follow the sculpt's proportions, mirrored Left/Right on symmetric sculpts, made on a low subdivision
  level and carried up with the detail. Eye Parts get eyeLook rotations, teeth and tongue the lower jaw's motion,
  brows and lashes follow the skin. Face test presets and a slider per shape; `face_landmarks_detect`,
  `face_preset`.
- Live face: `face_weights json={...}` sets ARKit-named shape weights on every Part, re-evaluating only the
  vertices they move (no undo step, partial GPU upload; about 9 ms for all 52 on a 700k-vertex head on an M1 Pro).
- Face takes: play recorded face feeds (JSON frames, `face_take_play`), bake them (`face_take_bake`) into a glTF
  animation of the morph target weights (GLB) and USD blend shape weight time samples; saved with the project.
- `new_head`: a new scene with a life-size human head (with mouth and eye sockets) and its eyes.
- Blender bridge: sculpt layers arrive as shape keys with their names and weights (a `.shapes` file beside each
  Bridge file, which now holds the base shape when the mesh has layers).
- UI tests: `zoom` and `look` steps; `run` expands `$SCRIPT_DIR`.
- Splat training from captures (Splats > Train from capture): a phone video, a folder of photos, a phone-link
  capture or a COLMAP / nerfstudio dataset becomes a Gaussian splat that appears in the viewport after the first
  checkpoint and sharpens while it trains (worker thread; orbit freely). Progress over the canvas: step, PSNR,
  elapsed and remaining time, Good enough - stop, Cancel. Presets Fast preview and Quality. Uses Brush 0.3.0
  (Apache-2.0, WebGPU: NVIDIA, AMD, Intel, Apple), downloaded on request from its GitHub release with a pinned
  SHA-256, or a path / `SCULPTFORGE_BRUSH`; COLMAP (BSD; downloaded on request on Windows) for camera positions,
  skipped for captures with AR poses in `capture.json`; ffmpeg for video, keeping the sharpest frame of each
  stretch. The result is centred and levelled from the camera positions, saved next to the capture and copied to
  the Library Splats tab. Dropping a video onto the window trains it. Commands `splat_train` and
  `splat_train_cancel` (blocking in sculptforge-cli, Python and MCP). Guide chapter "Scan an object into a splat";
  a generated CC0 test capture (tools/splat/make_test_capture.py, tests/data/splat_capture).
- **Capture from phone** (File > Capture from phone, or the phone button in the Library; guide chapter "Phone capture
  and AR", design in docs/phone-link.md): scan the QR code and the phone's browser becomes SculptForge's camera,
  with no app and no account. Send a scan video or a photo burst (with coaching; for splat training), one photo
  (to Image to 3D), or a splat .ply from Scaniverse, Polycam or Luma; uploads are chunked and resume after Wi-Fi
  drops, and land in `<library>/Captures/<date_time>-<kind>/` with a `capture.json`. **View in AR** shows the
  active Part or the scene on your desk (USDZ for iPhone AR Quick Look, GLB for Android Scene Viewer; reduced to
  80k triangles, vertex paint and textures baked, re-exported after strokes). **Face capture** tracks 52 ARKit
  expressions and the head on the phone (MediaPipe Face Landmarker) and streams them to SculptForge at about 30 fps
  over a WebRTC data channel, for `face_weights`; **Record face take** saves them as JSON Lines. LAN only, a new
  token per session, size limits, safe file names; nothing listens while the window is closed.
- Build: WebRTC through libdatachannel (MPL-2.0) and Mbed TLS (Apache-2.0), fetched and linked statically;
  `-DSCULPTFORGE_WEBRTC=OFF` builds without the live face feed.
- Website: the face capture page (`website/link/`), which needs HTTPS for the phone's camera.
- Services: `services/link-worker`, a Cloudflare Worker (free plan) that relays the face page's one encrypted
  connection message to the desktop at `link.sculptforge.app`; ntfy.sh stays as the fallback. Not deployed yet.
- "Fixes & suggestions" in the Library header (shown at startup) and the Help menu: opens the bug report and
  feature request forms on GitHub.

### Changed
- The website moved to https://sculptforge.app (old islandmed.github.io/SculptForge links redirect). The
  app, update check, user guide, packages and Blender add-on link there.
- Website: the tagline now reads "the free, open-source sculpting app".

## [0.8.1] - 2026-10-25

Fixes from a professional sculptor's testing, and a way to bring a library over.

### Classic feel (tester feedback on 0.7)
- Classic input style: every zoom drag (Ctrl+right-drag, Alt-drag then release Alt,
  Ctrl+middle-drag) zooms out when dragged up; the SculptForge style keeps zooming in.
  Rotation and panning were checked and already follow the cursor.
- Ctrl brushes: while Ctrl is held the left shelf shows the mask brush (MaskPen,
  MaskLasso, MaskRect) and its stroke (Dots, Lasso, Rect); the Brush picker (Ctrl+B or a
  Ctrl+click on the brush tile) and the Stroke picker opened with Ctrl held pick among them.
  Ctrl+Shift does the same for SelectRect and SelectLasso. Tool > Masking and Tool >
  Visibility keep their buttons.
- Face groups > **Group by UVs**: one face group per UV island of the visible mesh, undoable;
  `facegroup_uv` command for the console, CLI, MCP and Python.

### Texture painting
- **New Texture** on the top shelf and in the Color palette whenever the Paint brush (or Rgb) is on and the
  Part has no texture: one click starts texture painting, running Auto UV first (with its progress in the
  status bar) when there are no UVs. Ctrl-hover help; also in F3 search.
- Faster texture strokes: a dab paints its texels in parallel over 16-row bands with the same result on any
  number of threads, and the view uploads only the 64x64 tiles that changed. A big-brush dab on a 4K texture
  went from 29 to 6 ms (1.3K triangles) and from 47 to 11 ms (328K triangles) on an M1 Pro
  (docs/performance.md).
- Checked that texture paint doesn't depend on the mesh density: a hard stamp gives the same texels on a
  2-triangle plane as on an 8K-triangle one (new unit test). Paint that looks as coarse as the mesh is vertex paint:
  the Vertex target, or vertex paint shown multiplied over the texture.

### Bring a library over
- A folder of numbered Bridge files (`<name>.sculptforge`) opens as Parts in order, with names, face groups,
  vertex paint, mask and UVs.

## [0.8.0] - 2026-10-25

Dynamic topology, vector displacement brushes, QRemesh Guides, sculpt layers on every
subdivision level, and the Classic sculpting keys in Blender's Sculpt Mode.

### Dynamic topology
Guide chapter docs/guide/dynamic-topology.md.
- **Dynamic** on the top shelf (and in the Draw palette): every brush except Move, masking and painting remeshes
  the surface under it each dab: splits long edges (on the curve the normals describe), collapses short ones,
  flips for even valences and relaxes the new vertices, only inside the brush.
- **Detail** slider (0-100): the target edge length relative to the brush radius (dynamic topology), or with the
  **Constant detail** preference one size for the whole mesh. Smooth remeshes towards twice the size, so it
  simplifies.
- Keeps the mesh manifold with no degenerate or flipped triangles; boundaries, face group borders and the
  symmetry plane only slide along themselves; fully masked and hidden parts are left alone. X/Y/Z symmetry
  remeshes mirror images together, so the topology is mirrored too.
- Carries mask, vertex paint, paint layers, face groups and sculpt layer offsets; quads under the brush become
  triangles; UVs are dropped (Undo restores them). Meshes with subdivision levels are refused.
- Undo keeps only the vertex and triangle slots the stroke touched. The BVH (incremental inserts, removals,
  leaf splits), the adjacency (editable per vertex during a stroke) and the GPU (per-cluster patches instead
  of a full upload) follow each dab.
- `dyntopo` command (enabled, detail, constant) for the console, CLI, MCP and Python; Dynamic and Detail are
  saved with project settings; stored UI layouts get the new top-shelf items on load.
- Benchmark: `--bench-dyntopo` (docs/performance.md). On an M1 Pro, a medium dab on a 1M-triangle sphere takes
  9-13 ms including the GPU update.

### Vector displacement brushes
Guide: docs/guide/sculpting.md, "Vector displacement brushes".
- **VDM brush** (new brush type): one DragRect stamps a whole 3D shape with overhangs. Each point under the
  stamp moves by its texel's vector in the brush's tangent frame (Z = the surface's area normal, Y = the drag
  direction, X = right), scaled by the radius and Intensity (50 = the map's own size, 100 = twice). Alt digs
  in (X, Y kept, Z flipped, as Blender). Symmetry mirrors the stamp exactly (the mirrored pass flips the frame's
  handedness); mask, Front faces only, sculpt layers and undo as every brush. Works with subdivision levels.
- **DragDot stroke**: one stamp at the brush size that slides under the cursor until release (any brush).
- **Built-in maps** (procedural, CC0): Horn, Ear Curl, Scales, Nostril.
- **Brush > Vector Displacement**: the maps with thumbnails, Load VDM (OpenEXR 16/32-bit, any compression, or
  .sfvdm; dropping an .exr loads it too) with Swap Y/Z, Flip X/Y/Z and Scale for maps from other apps, New VDM
  Plane, Create VDM, Export EXR. While the VDM brush is the brush, the Alpha tile and picker show the maps, and
  the stroke switches to DragRect (the previous stroke comes back with the next brush).
- **Create VDM from a plane**: a 2 x 2 VDM plane with UVs and
  a masked border as a new Part (the others hidden, top view); Create VDM rasterizes each point's offset
  through the UVs into a map saved in the brush library (Divide keeps the UVs).
- **Blender's convention**, checked against the VDM Brush Baker's source and in Blender 4.2 and 5.2: RGB = XYZ in
  brush radii over the brush's square, first scanline = top. Exported EXRs read in Blender with the same vectors
  the same way up; Blender's 32-bit ZIP/PIZ and 16-bit EXRs load unchanged.
- **Library**: VDMs live as `.sfvdm` (header, name, float32 XYZ) next to the `.sfbrush` files, user over studio
  over built-in by name; project settings remember the brush's map by name.
- Commands: `vdm_set`, `vdm_list`, `vdm_export`, `vdm_new_plane`, `vdm_capture`, `vdm_stamp`, `vdm_delete`; the
  `stroke` command and Python/MCP stamp the brush's map too. OpenEXR via vendored tinyexr 1.0.13 (BSD-3), built on
  stb's zlib (no new dependency).

### Blender add-on: the Classic sculpting keys in Sculpt Mode
Guide: docs/guide/blender.md, "the Classic sculpting keys in Sculpt Mode".
- Add-on preference **The Classic sculpting keys in Sculpt Mode** (off by default): S / U / O radial Size,
  Strength and Hardness (Falloff); Space the Sculpt context menu; B a brush list by name (type to
  filter; Blender 4.3+ brush assets, 4.2 brush tools); Alt+drag Sub; Ctrl(+Alt)+drag lasso mask / unmask;
  Ctrl+Shift(+Alt)+drag show only / hide inside; Ctrl+W group from mask and clear the mask (else group visible);
  Ctrl+D Divide (Multires), D / Shift+D SDiv up / down; X symmetry; Shift+F wireframe; F frame; P perspective;
  L Stabilize Stroke; W / E / R move / scale / rotate. Turning it off (or disabling the add-on) removes them.
  The preferences and the guide list the Classic sculpting keys Blender has no equivalent for.
- tests/blender/test_in_blender.py checks every key's operator and settings, the data paths (Blender 4.2's
  unified paint settings vs 5's per-mode ones), the brush picks, SDiv and Divide, on/off, and the VDM EXR
  round trips; run in real Blender 4.2.23 and 5.2.3.

### QRemesh Guides
- **QRemesh Guides brush** (Tool > Geometry > QRemesh > Guides, the brush picker, F3): drag on the model to draw
  curves for QRemesh's edge loops to follow, around the eyes, the mouth, a joint. Curves are snapped to the
  surface, closed into a loop when they end where they started, mirrored with symmetry (a curve that is its own
  mirror image is kept once), shown over the model (faint where the model hides them), Alt+click deletes one;
  **Delete Last** and **Clear**. Ctrl still masks and Shift still smooths while the brush is picked.
- QRemesh puts an edge loop on every curve (a lattice line along it; along a closed curve the corners are placed
  at an even number of even steps, so the loop closes without a kink) and turns the edge flow around it to
  follow; **Curves Strength** (0-100, default 50) sets how far. With symmetry, a curve on either side guides
  both, and a loop across the middle (the mouth) meets its mirror image at a corner on the seam. A result with a
  broken loop or a fan by a guide is tried again with another start for the field. On a sphere with a circular
  guide round an "eye", the edges near it are within 2-8 degrees of its direction (about 22 without), and the
  loop closes (6 of 6 test eyes, 4 of 4 mouths across the middle, 507 of 512 in random sweeps with and without
  FMA; the misses are loops about 20 quads round).
- Guides belong to each Part, are saved in projects (`qremesh_guides` per Part in the manifest, no format
  change: older projects open, older versions ignore them) and copied to the kept original. Guides left far from
  the surface (the Part moved) are ignored with a note.
- Scripting: `qremesh_guide` (points, view, symmetry), `qremesh_guide_delete_last`, `qremesh_guides_clear`,
  `qremesh_guides_info`; `qremesh` gains `guides` and `curves_strength`.
- Keep Groups (loops along face group borders) and Use Vertex Paint (density) were already there.

### Sculpt layers on subdivision levels
- Checked against 3D Layers and covered by tests: a layer recorded at the top survives Lower / Higher Level
  bit for bit, shows at lower levels as the same strokes in the base would, keeps one weight for every level, and
  survives Divide, Reconstruct Subdiv, Delete Lower / Delete Higher, undo, saving and loading at any level, and blend
  shape export from any level (`subdiv_export`). Layers added or sculpted at a low level show above. Nothing needed
  fixing; the Layers panel got Ctrl-hover help and the guide a "Layers and subdivision levels" section.

### Other
- UI tests: `camera YAW PITCH` step; the guide's screenshot script turns the mesh back on after the splats shot
  (the subdivision-level shots after it showed only splats).
- `sculptforge_tests NAME...` runs only the tests whose names contain one of the arguments.

## [0.7.0] - 2026-10-25

The Classic sculpting keys and gestures by default, and a bridge to Blender.

### Classic feel: the Classic sculpting keys and gestures by default
Guide chapter
docs/guide/quick-tour.md.
- **U / I / O**: Intensity, Colour intensity and Falloff sliders at the cursor (S stays
  Size). Values show 0-100 (Falloff -100..100); the top shelf and Draw palette say Intensity instead of
  Strength.
- **Colour intensity**: painting has its own strength (default 100), separate from Intensity.
- **Falloff**: the brush falloff, for every built-in brush, Move, SnakeHook, masking and painting (Brush Lab
  recipes keep their own tip). At 0 the maths is unchanged. The cursor's inner ring now shows Falloff
  (where the brush works at half strength) instead of the strength.
- **Smooth keeps its own Intensity** (default 50): Shift-smoothing no longer follows the active brush's
  strength. Holding Shift (or picking Smooth) shows and sets it.
- **Space**: the QuickMenu under the cursor (brush, stroke, alpha, material, Size, Falloff, the
  intensities, Add/Sub, Sculpt/Paint, Rgb, X). Hold and let go to close, or tap to keep it open.
- **Brush letters**: B C B Clay Block, B D S Sharp Crease, B M V Move, B I N Inflate, B P I Pinch, B S H
  SnakeHook, instead of the first free letter.
- **Mask keys**: Ctrl+A Mask All, Ctrl+Shift+A Clear Mask, Ctrl+I Invert, Ctrl+H View Mask; **Ctrl+W** Group
  from Mask (and clear the mask; with nothing masked, Group Visible).
- **Ctrl+click on the model blurs the mask, Ctrl+Alt+click sharpens it** (a Ctrl+drag still paints).
- **Shift pressed after a stroke starts** draws a straight line, snapped to 45 degree steps.
- **Shift+click the background, let go of Shift, drag** rolls the view about its axis.
- **Shift+Alt+click a Part** selects and frames it.
- **Ctrl+Shift+click a face group on a partly hidden mesh hides it** (it used to isolate it); a Ctrl+Shift
  rectangle around nothing reverses the visibility.
- **L** toggles LazyMouse, **V** swaps the main and secondary colour, **Ctrl+Shift+Alt+I** is Export Interface.
- **Preferences > Input**: Style **Classic** (default) or **SculptForge**, which keeps 0.6's keys: 1-9 and 0 pick
  brushes (classic: free, 9 = QuickSave), I = Intensity, Ctrl+drag on the background zooms when nothing
  is masked (classic: nothing). Rotation (free / turntable) is also here.
- Projects store Smooth's Intensity, Colour intensity and Falloff.

### Blender bridge
- **SculptForge Bridge, an add-on for Blender 4.2+** (`addons/blender`, GPL-3.0-or-later): select objects, click
  **Send to SculptForge** (the 3D View's SculptForge tab, the Object menu or File > Export), sculpt, then
  **File > Blender > Send to Blender** in SculptForge. The objects update in place in Blender, keeping their
  transform, materials and modifiers; new Parts become new objects. Works on Linux, Windows and macOS.
  - The two sides share a per-user folder (`<user data>/SculptForge/SculptForge/blender`) and exchange Bridge files
    plus a JSON manifest per direction (object names, `matrix_world`, a revision); each side checks the other's
    manifest once a second. No network. SculptForge records where it is installed and writes a heartbeat there, so
    the add-on starts it (`--blender`) only when it isn't running.
  - Identity: Blender object name <-> Part (its Bridge files name). Positions travel in object space with the matrix;
    Z-up <-> Y-up, metres both; mirrored objects keep their faces pointing out. A round trip is the identity
    (positions, faces and winding, UVs, vertex paint, mask, face groups): tested in C++, from the add-on's side
    against `sculptforge-cli`, and in a real Blender in CI.
  - Unchanged topology updates only positions and attributes, so Blender keeps n-gons, edges, seams and other
    data; changed topology rebuilds the mesh. Active UV map <-> UVs, active colour attribute <-> vertex paint, sculpt
    mask <-> mask, face sets <-> face groups. Modifiers can be kept live or applied (then turned off on return).
- Commands `blender_send` and `blender_receive` (CLI, MCP, Python), **File > Blender** (Send, Receive, Receive
  automatically), F3 entries and Ctrl-hover help. Bridge files receive and the bridge share the code that matches objects
  to Parts.
- Packaging: `build_addon.py` writes the extension zip and a legacy add-on zip (`cmake --build build --target
  blender_addon`); releases attach `sculptforge_bridge.zip`. Guide chapter **Working with Blender**.
- UI tests: `copy SRC DST` stages files another program would write.

## [0.6.0] - 2026-10-24

Performance release. Figures for a 33 million triangle model with 7 subdivision levels on an M1 Pro laptop,
measured on the merged release against 0.5 with the same benchmark (docs/performance.md): memory for the mesh,
BVH and levels 156 -> 83 bytes per triangle (whole process 5.6 -> 3.4 GB); GPU memory 46 -> 15 bytes per
triangle; orbit 20.6 -> 9.2 ms with 4x antialiasing, 14.8 -> 3.7 ms with Fast Navigation; a medium dab 11.9 ->
7.5 ms and a large one 78.5 -> 45 ms with the GPU upload; Mask / Paint dabs 5.6 / 7.0 -> 2.5 / 3.5 ms; opening
the project 4.7 -> 2.1 s. The sections below were measured on each part's own branch; the merged figures differ
a little (dabs on divided meshes are about 15% slower than on the brush branch alone, from the implicit
adjacency that saves the memory).

### Update check
- Once a day the app reads https://islandmed.github.io/SculptForge/latest.json (nothing about the user is sent)
  and, when a newer version is out, shows a banner: What's new, Download, Later, Skip this version.
  **Help > Check for updates** asks right away; **Preferences > Updates** turns it off. `SF_NO_UPDATE_CHECK=1`
  turns it off for a run, `-DSCULPTFORGE_UPDATE_CHECK=OFF` at build time (the AUR package uses it).

### Memory: sculpts with subdivision levels take half as much
- A divided sculpt takes about 90 bytes per triangle in memory instead of 160: a 33 million triangle model with
  its levels needs 3.0 GB instead of 5.3 GB, and a 100 million triangle one fits in 9 GB.
  - Levels made by Divide store no triangles, quad pairs or vertex maps: they follow from the level below, and are
    generated when a level is entered. A level also no longer keeps its detail next to its positions (the detail
    is fitted to the positions when a lower level changes). The level stack went from 75 to 29 bytes per triangle.
  - The mesh of such a level works out which vertices and triangles surround each vertex from the level's
    topology instead of storing those lists (62 -> 39 bytes per triangle for the sculpting mesh). Brushes, normals,
    undo and the renderer get exactly the same lists, so sculpting gives bit-identical results; working them out
    costs some speed: dabs on divided meshes are up to about 15% slower (large brushes at 33M: 3%).
- **Divide goes to 100 million triangles and beyond**: the limit follows the computer's memory (about 160 million
  on 32 GB) instead of a fixed 50 million.
- **Opening a project** no longer needs twice the scene's memory (the file is read straight into the meshes) and
  is about twice as fast (33M: 4.7 -> 2.1 s; the BVH is built while the levels load). Saving no longer copies the
  meshes first, and project files of divided sculpts are about 30% smaller.
- Lower Level after sculpting at the top level is faster (33M: 1.5 -> 0.9 s), and so is Divide (2.4 -> 1.4 s).
- Project format 6. Projects from 0.5 and earlier open as before (and get the savings); 0.5 can't open format 6
  projects.

### Fixed
- New Cube at some resolutions (13, 26 and others) left a few vertices along the cube's edges unwelded, so the
  mesh had small cracks there (the 33 million triangle benchmark sphere among them).

### Performance
- Brushes on huge meshes are 2 to 3 times faster on the CPU: a radius 0.4 dab (a million vertices with X
  symmetry) on a 33M triangle sphere takes about 21 ms instead of 49 (39 instead of 63 with the GPU upload),
  Smooth and Move at that size 10 and 5 ms; a medium dab about 3.6 ms instead of 9 (6.5 instead of 11 with the
  upload), and undo of a big stroke about twice as fast. Results are still exactly the same
  with multithreading on or off; compared with 0.5 they differ only by rounding (a brush's area normal is summed
  over its vertices in index order) and where the two halves of a symmetric dab overlap (both now see the
  normals from before the dab).
- How: the BVH vertex query, the normal update and the refit keep their vertex and leaf sets as bitmaps listed
  in index order (the mesh's arrays are then read front to back), each thread marks a bitmap of its own, a dab
  refits the BVH leaves its query already found, normal updates prefetch the faces ahead, the undo record of a
  stroke is written in parallel, the Move brush runs each mirror in parallel, symmetric dabs update normals once
  where the mirrors overlap, and undo swaps positions in parallel. Small dabs stay on one thread.
- Less memory while sculpting: the per-vertex stamps of the BVH, the normal update and the stroke recorder
  (20 bytes per vertex, about 330 MB at 33M triangles) are bits now.
- `--bench-brushes TEXT` runs only some benchmark rows; the benchmark measures Smooth and Move at a large radius.

### Faster viewport
- Meshes are drawn in clusters of up to 4096 triangles (64-triangle runs sorted along a Morton curve). Each
  frame skips the clusters outside the view and, on closed meshes seen from outside, the clusters facing away
  (the GPU also skips the back faces left in the others); the rest go in one multi-draw with 16-bit indices.
  Orbiting a 33M triangle sphere with 4x antialiasing: 20.6 -> 8.7 ms; 104M: 19.6 ms.
- Vertices take 12 bytes on the GPU (28 before): positions on a 21-bit grid over the mesh, octahedral normals,
  square-root companded curvature. Vertex Paint is 8-bit (gamma encoded), the mask 8-bit, face groups 16-bit. With
  the index and wireframe savings a mesh needs about 15 bytes per triangle on the GPU (46 before on meshes with
  quads, whose wireframe edges were always kept).
- Fast Navigation also draws big meshes as a motion proxy while the view moves: per cluster, the vertices
  snapped to a grid a few edges wide (seam vertices kept, so the proxy of a closed mesh is closed too), about a
  fifth of the triangles. 104M triangles orbit at about 100 fps. Built only for clusters that changed, when the
  view starts moving.
- The wireframe's edges are built when the wireframe is turned on, each edge once (they were always kept,
  every edge twice).
- First upload of a 33M mesh 701 -> 422 ms (indices and vertices written straight into mapped buffers while
  the layout is built); paint and mask dabs no longer re-send positions and normals.
- `sculptforge --bench` reports GPU memory, triangles drawn after culling, the motion proxy, and orbiting with and
  without antialiasing; `--bench-view` runs only the viewport's part.

### Changed
- STL export is now in millimetres with Z up, as slicers expect: models used to arrive 1000 times too small
  (0.44 mm instead of 44 cm) and lying on their back. `export_stl` takes `height_mm=` to scale to a print height,
  plus `scale=` and `z_up=`, and the status line gives the printed size. Opening an STL converts back, so
  downloaded print models come in upright and at their real size; STL files from 0.5 and earlier still open as
  saved.

## [0.5.0] - 2026-10-23

### Subdivision levels (multiresolution)
- Divide (Tool > Geometry, Ctrl+D, `subdiv_divide`) adds a subdivision level per Part and keeps the lower
  ones: Catmull-Clark on meshes with quads (quads stay quads), Loop on triangle meshes, flat with Smt off.
- Lower Level / Higher Level (Shift+D / D, `subdiv_lower`, `subdiv_higher`) and the SDiv slider (`subdiv_level`)
  step between levels. Detail is stored per level as a displacement in the local frame of the smoothed level
  below, so reshaping a low level carries it along; edits at a high level are restricted (full weighting) to
  the lower ones. Round trips without edits are bit-for-bit.
- Mask, vertex paint, paint layers, face groups, hidden faces and UVs follow the levels (copied down, interpolated or
  inherited up where they changed); sculpt layers have their own detail per level; the texture is shared.
- Delete Lower, Delete Higher (`subdiv_delete_lower`, `subdiv_delete_higher`) and Reconstruct Subdiv
  (`subdiv_reconstruct`: finds the lower level of a regular Catmull-Clark or Loop subdivision, keeping the mesh
  and its vertex order). `subdiv_info` lists the levels; `subdiv_export` writes the visible Parts at another
  level (e.g. the base cage) in any export format.
- Each level change is one undo step, as is each stroke at any level; level edits share unchanged levels
  copy-on-write. The two levels left last keep their topology and BVH, so stepping back and forth is instant
  (1-45 ms at 6.5M triangles).
- Projects save every level (project format 4). The Part list shows "SDiv n/m".
- Ctrl+D now runs Divide; the one-way Subdivide stays in Tool > Geometry and as `subdivide`.

### Transform gizmo (moving, scaling, rotating and posing)
- Transform gizmo modes: W Move, E Scale, R Rotate, Q back to Draw (top shelf, Transform palette, F3).
  Move has axis arrows, plane squares and a view-plane centre; Rotate has axis rings and a view ring; Scale has
  axis boxes and a uniform centre. Shift snaps (15 degrees, a round grid step, 10%). The amount shows beside the
  gizmo while dragging. Dragging off the handles still navigates; Ctrl still masks.
- Transforms move the active Part, or only its unmasked part: partly masked vertices move partly, hidden ones
  never. Falloff eases the movement in over a distance from the mask's edge (measured along the surface, as a
  share of the size) for soft joints when posing. With symmetry on, a masked transform is mirrored across the
  symmetry plane.
- Pivot: follows the centre of what moves; Alt+drag the gizmo to move it (or turn its axes) without the model,
  Alt+click the model or To Surface to put it on the surface, Reset; World or Surface axes (Y along the normal,
  turning with rotations). The gizmo follows undo / redo of its own steps and is saved in projects (`gizmo` key,
  no format bump).
- Transform gizmo panel: pivot, options and numeric Move / Rotate / Scale with Apply; All Parts (move, rotate and scale
  all Parts together, one undo step per Part).
- `transform` command (translate, rotate, scale, pivot, orient, masked, falloff, symmetry, all); each gizmo drag
  is echoed to the console as the command that repeats it.
- One undo step per drag that restores positions bit for bit. Sculpt layers move with the surface (the base and
  every layer's offsets). Topology, quads, UVs, textures, vertex paint and face groups are untouched.
- Fast on big meshes: a drag recomputes from the drag's start (no drift), transforms only the moving vertices
  through a per-weight lookup table on several threads, turns normals of rigid parts and recomputes only where
  the movement varies, uploads only the changed vertex range and refits the BVH once on release. On 1.3M
  triangles: about 2 ms to transform and 7 ms to upload per drag frame, 20-40 ms on release.

### Normal, displacement and AO map baking
- Tool > Bake (and the `bake` command) bakes a high-poly source (one Part, or every visible one but the
  active) onto the active Part's UVs: tangent-space normal, object-space normal, displacement, ambient
  occlusion, cavity and colour (vertex paint or painted texture) maps, 1K to 8K.
- Tangent-space maps use MikkTSpace tangents (vendored, zlib), as Blender, Unreal, Unity and Godot compute them;
  OpenGL or DirectX green convention. Unit-tested to decode to the high-poly's normals within 0.2 degrees on
  average.
- Rays along the low-poly's smoothed normals through a front / rear cage, measured automatically by default or
  set by hand, keeping the high-poly hit nearest the low-poly; 1-16 rays per texel of anti-aliasing; padding
  around UV islands; tile-parallel on every core over a dedicated thread-safe BVH, in the background with
  progress and Cancel.
- Displacement as a 16-bit PNG (mid-grey = 0, scale reported) and a 32-bit float OpenEXR in world units.
- The baked normal map shows on the low-poly in the viewport (new normal-map shader with per-corner MikkTSpace
  tangents), with a Preview Normal Map toggle.
- Send to Unreal imports the baked normal map too (`T_<Name>_N`, normal-map compression, green flipped for
  OpenGL maps) when the editor runs on the same computer.
- Maps are written as `<Part>_normal.png`, `_ao.png`, `_displacement.png` / `.exr` and so on, beside the
  last export by default.

### Quad retopology and detail projection
- QRemesh (Tool > Geometry > QRemesh, `qremesh`): automatic quad retopology. A
  field-aligned method after Instant Meshes (Jakob et al. 2015; field formulas follow its BSD-3 reference code,
  credited in THIRD_PARTY_NOTICES.md): a multi-resolution 4-RoSy orientation field and position field, polygon
  extraction, then every polygon split into quads and relaxed onto the sculpt. The result is 100% quads,
  2-manifold and closed where the sculpt is, within about 5% of the target count.
- Options: Target Polygons with Half / Same / Double, Adaptive Size (curvature-driven quad size), Keep Groups
  (edge loops along face group borders, groups kept), Detect Edges (loops on hard edges over 50 degrees),
  Use Vertex Paint (red = up to 4x the density, blue = down to a quarter), Symmetry (one half remeshed and mirrored,
  welded on the seam), Keep Original (the old Part kept hidden below, for ProjectAll).
- Runs in the background in the app with a progress bar and Cancel; one undo step. Mask and vertex paint are
  interpolated onto the new mesh; sculpt layers are baked.
- ProjectAll (Tool > Part > Project, `project_all`): moves the active Part onto the other visible
  Parts (rays both ways along the normal, nearest hit within Dist, else the closest point) and takes their
  vertex paint. Dist, Mean, Blur, Geometry / Vertex Paint toggles, an optional single `source`; respects the mask
  and hidden faces; shape goes into the active sculpt layer, colour into the active paint layer; one undo step.
- Fast on dense meshes (all threads, a read-only BVH): 1.3M triangles to 5,000 quads in 1.7 s, to 100,000 in
  6 s; ProjectAll onto 1.25M vertices in 0.6 s (10-core desktop).
- New guide chapter "Quad retopology and detail projection", glossary entries; UI test `tests/ui/retopo.sfui`,
  unit tests `tests/test_retopo.cpp` (manifold, quad %, target count, Hausdorff distance, projection error).

### Performance at tens of millions of polygons
- Benchmark: `sculptforge --bench` (with GPU upload and orbit timings) and `sculptforge-cli --bench` build a
  sphere divided to 4M / 16M / 32M triangles and measure every brush per dab (kernel, normals, BVH refit, upload),
  undo, level stepping, project save / open and orbit. Numbers before and after in docs/performance.md.
- A persistent thread pool (core/ThreadPool) runs every brush kernel (all brush types, recipes, mask and paint),
  normal updates, BVH queries, refits and builds, and project CRCs. Work is split into fixed blocks with partial
  sums added in order, so results are identical to one thread (`set_multithreading`).
- Partial GPU updates: meshes track dirty vertices (a bit each, plus 2048-vertex blocks), and the renderer keeps
  vertices in first-use order of the triangle list (core/UploadOrder), so a dab uploads a few short ranges of
  what it changed instead of most of a subdivided mesh. A medium dab at 33M triangles: 398 ms -> 10 ms.
- Memory: the per-triangle face normal cache and its stamps are gone (normals are summed per vertex, same
  result), the adjacency list has no spare capacity, the renderer keeps no CPU copy of the curvature: 90 -> 62
  bytes per triangle for a sculpting mesh.
- The first vertex paint stroke on an unpainted mesh keeps the (empty) paint stack for undo instead of a copy of
  the whole mesh.
- Projects: archives stream to disk (no in-memory copy), entries are read in place, CRCs are computed in
  parallel, arrays are copied rather than converted byte by byte; ZIP64 past 4 GB (smaller archives are
  byte-identical to before).
- The window has no multisampling; the 3D view antialiases in its own target (Renderer::drawView), so it can
  use fewer samples when the GPU has no room, and Fast Navigation draws scenes of 10M+ triangles without
  antialiasing while the view moves. A Part that runs the GPU out of memory is not drawn (status message).
- Preferences > Performance: Antialiasing (Off / 2x / 4x / 8x), Fast Navigation, Multithreading, the last
  stroke's cost per dab and memory; `perf_info` and `set_multithreading` commands.

### Polish of the v0.5 features (known issues fixed)
- Auto UV on a Part with a painted texture now moves the paint and every texture layer onto the new UVs
  instead of scrambling it (each texel of a new UV triangle samples the old texture at the same point of the same
  triangle, islands padded 8 texels); one undo step restores the old UVs and texture (`uv_auto
  keep_texture=false` for the old behaviour). 1.05M triangles, 4K texture with a layer: 0.2-0.3 s.
- Keep Texture (Tool > Remesh, Tool > Geometry > QRemesh; `keep_texture` on `remesh` and `qremesh`, default on):
  the rebuilt surface gets Auto UVs and the texture, all layers, is projected onto it from the old surface, in the
  same undo step. Voxel Remesh of a painted Part runs in the background with progress and Cancel. Merging
  Parts (`part_merge_down`, `part_merge_visible`) keeps their textures in one texture on new Auto UVs.
- Decimate no longer scrambles a texture at UV seams (seam vertices stay in place).
- Auto UV no longer crashes or folds UVs on dense smooth meshes (e.g. the default 82k-triangle sphere): fixed in
  third_party/xatlas. UV islands are capped at about 4000 triangles, so a 1.3M-triangle sculpt unwraps in about
  16 s instead of minutes.
- Subdivision levels: Voxel Remesh, Decimate, QRemesh, Subdivide, Delete Hidden and merging or splitting Parts
  say in the status line that they removed the levels and which one became the new base ("Subdivision levels
  removed: SDiv 3 of 5 kept as the new base"); undo restores every level (now covered by tests).
- Subdivision levels: a sculpt layer's fine detail now turns and bends with a low level rotated with the Transform gizmo
  or reshaped (layer details are stored relative to the surface). Project format 5; format 4 projects are
  converted when they open.
- Subdivision levels: only the active Part keeps its recently visited levels cached; selecting another Part
  frees 50-170 MB per Part with 1.8M triangles (the first level step after coming back takes about 0.1 s).
- QRemesh: Symmetry mirrors across the model's own middle plane, so models away from the world centre work. A
  model that isn't mirror-symmetric is remeshed without symmetry and the status line says so; a nearly symmetric
  one keeps mirrored topology on each side's own surface. Symmetry is on by default in the panel; new
  `symmetry_center` argument for a fixed plane.
- QRemesh: small separate pieces (eyes, rivets) are no longer dropped: they are remeshed on their own with at
  least 32 quads, or kept as they were. Parts thinner than a quad (blades, fins, slabs) keep both sides and their
  hard edges (thin slab: mean error 2.2% -> 0.05% of its size, Hausdorff 8.8% -> 2.6%).
- Top shelf: the Vertex / Texture paint target is hidden while the Part has no texture, instead of sitting
  there greyed out (docs/ui-design.md now states the rule for contextual shelf buttons); Size and Strength
  shrink to fit when the row is full.
- Exports: the `.mtl` and PNGs written beside a model whose file name has spaces use `_` instead (OBJ's `mtllib`
  and MTL's `map_Kd` split names at spaces). Textured exports were validated with other tools: Khronos glTF
  validator (no errors or warnings), OpenUSD's validators (no issues), ufbx strict mode, and a texture
  orientation check per format; `tools/export_check/run.sh` repeats it.
- UI tests: `wait_idle` waits for several idle frames in a row, which fixes the flaky `ui_library` under a
  loaded parallel ctest.

### Changed
- Wireframe (the polygon frame overlay) is Shift+F only: W is now the Transform gizmo's Move.

## [0.4.2] - 2026-10-23

### Texture painting on UV maps
- Paint into a texture on the model's UVs instead of its vertices, so the colour detail comes from the texture
  (1K, 2K, 4K or 8K), not from the mesh density (Tool > Texture Map > New Texture, `texture_create`). The Paint
  brush, Rgb sculpting, fills, cavity paint, colour picking (C), mask, hidden faces, symmetry and pen pressure
  work as for vertex paint; texture strokes never paint through to the far side. A few texels of padding around
  every UV island keep seams invisible.
- Vertex / Texture toggle on the top shelf (and in the Color, Paint Layers and Texture Map palettes) chooses
  where paint goes per Part (`paint_target`). The viewport multiplies the texture with the vertex paint over
  the material; Show texture hides it.
- The Paint Layers palette works on the texture's own layer stack: blend modes, opacity, visibility, lock,
  duplicate, merge down, flatten, clear and fill, composited only where something changed.
- One undo step per stroke, storing just the 64x64 tiles it touched (counted in the undo memory budget). Layer
  edits (add, duplicate, clear, fill, merge, flatten, delete, settings) store only the tiles they changed, and a
  tile of one colour costs 4 bytes: a stroke on a new 4K layer takes under a kilobyte of undo, not 64 MB.
- Interactive at 4K on a 1.3M-triangle mesh: a dab visits only the triangles under the brush and uploads only
  the changed rectangle of the texture.
- Auto UV (Tool > Texture Map, `uv_auto`): automatic UV unwrapping with xatlas (MIT, vendored in
  third_party/xatlas), running in the background with progress. Show UVs draws the texture with the UV layout.
- Vertex Paint > Texture and Texture > Vertex Paint (`texture_from_vertexpaint`, `vertexpaint_from_texture`), Import and
  Export Texture (PNG; `texture_import`, `texture_export`), Delete Texture (`texture_delete`).
- Projects save textures and their layers (project format 3; version 1 and 2 projects still open, and older
  SculptForge versions refuse format 3 files instead of dropping the paint).
- Exports write the texture next to the model: OBJ with a `.mtl` (`map_Kd`), glTF/GLB with an embedded base
  colour texture, USD with a preview material, FBX and Bridge files with the PNG beside the file.
- Subdivide, decimate, split and duplicate keep the texture with the UVs.

## [0.4.1] - 2026-10-23

### Fixed
- Graphics tablets: the brush ring no longer flickers between two places, which looked like a second brush
  blinking on the model (most noticeable while masking with Ctrl or smoothing with Shift). Windows Ink and some
  tablet drivers send mouse events of their own beside the pen's, often at an old position; while the pen is
  in range only the pen now moves the cursor, and the mouse takes over again once the pen goes quiet.

## [0.4.0] - 2026-10-22

Shaped by feedback from a working professional modeler and a survey of sculpting forums.

### Classic feel
- Free rotation by default: the model follows the cursor and can roll over the top. Hold Shift
  while rotating to snap to the nearest front, side, top or bottom view. Preferences > Interface > Rotation:
  Turntable keeps the model upright (Blender / Maya style).
- Alt+drag pans and letting go of Alt mid-drag zooms; right-drag follows right-click navigation
  (Alt pans, Ctrl zooms, Shift snaps). Projects remember the view's roll.
- MaskLasso and MaskRect (Tool > Masking): Ctrl+drag a lasso or rectangle to mask what's inside, Ctrl+Alt to
  unmask, starting on the model or the background.
- SelectLasso (Tool > Visibility): Ctrl+Shift+drag a free-hand lasso to keep what's inside (Alt: hide it),
  alongside SelectRect. New script commands `mask_rect`, `mask_lasso` and `hide_lasso`.
- New Sculpt material, the default: neutral clay under a strong key light with crisp highlights and deep
  cavity shading, so small strokes stay readable. Projects keep the material they were saved with.

### Quads
- Models opened through the Bridge, OBJ, FBX, PLY or USD keep their quads: the wireframe overlay shows them as quads and
  OBJ / FBX / PLY / USD / Bridge files export writes them back as quads, through sculpting, layers, undo, Parts and
  projects.
- Subdivide turns each quad into four quads, voxel remesh produces an even quad grid, and the
  Plane and Cube primitives are quads. Decimate still produces triangles.

### Paint layers
- Photoshop-style vertex paint layers per Part (Tool > Paint Layers): name, visibility, opacity, lock and ten
  blend modes (Multiply, Screen, Overlay, Soft Light, Add, ...). Painting is non-destructive and undoable;
  merge down, flatten, duplicate and reorder work as in Photoshop.
- Exports use the composited colours; projects keep the layers (older versions open them flattened). New
  `paint_layer_*` script commands.

### Documentation
- What's New: a new user-guide chapter listing what changed in each version, under **Help > What's New**. It
  opens by itself the first time a new version starts. The website shows "What's new in 0.4" up front.
- The guide, glossary, roadmap and install chapter cover everything above, with direct download links.

### Install
- `install/`: direct download links that always get the newest build, what to do after downloading, and
  one-command installers (`install.sh` for macOS / Linux, `install.ps1` for Windows) that verify checksums.
  Releases also carry version-less copies of each file (e.g. `SculptForge-macos.dmg`).

### Development
- Windows testing: `tools/wintest.sh` cross-compiles for Windows and runs every test under Wine; CI now runs
  the UI tests on Windows too (Mesa's software OpenGL). Release builds run the UI tests under Xvfb.

## [0.3.0] - 2026-10-22

### User guide
- A complete user guide (21 chapters, from the interface and every brush to Send to Unreal, scripting and
  troubleshooting, with screenshots) built into the app: Help > User Guide (Shift+F1), with contents, full-text
  search, links, back / forward and copyable code. Hold Ctrl over a control and press F1 to open its chapter.
- The same guide on the website (guide/), generated from docs/guide by tools/guide/build_site.py, with search,
  a contents drawer on phones and swipe / arrow keys between chapters.
- Website motion: page transitions (View Transitions), scroll-linked effects, a progress bar and staggered
  reveals tuned for phones, tablets and monitors; all off with "reduce motion".

### A classic interface
- Laptop camera notches: when SculptForge fills the screen, the interface stays below the camera notch of
  MacBooks (macOS reports the safe area; on Linux a notched MacBook display is recognised), with a plain band
  beside the notch. Preferences > Interface > Camera notch: Automatic / Always reserve / Off.
- Fullscreen toggle (F11, Render palette; `--fullscreen` on the command line), and minimise / close buttons at
  the far right of the title strip (close asks to save changes; Ctrl+Q quits).
- The canvas is framed by shelves: a top shelf (Library, Sculpt/Paint, Add/Sub, Size,
  Strength, symmetry, pressure), a left shelf of Brush / Stroke / Alpha / Material pickers with the colour picker,
  a right shelf of navigation (drag Move / Zoom / Rotate) and display toggles, and a bottom shelf of your most-used
  brushes. 16 alphabetical palettes hold every tool; the Tool palette (Part, Layers, Geometry, Remesh,
  Masking, Face groups, Visibility, Export, Display) sits in the right tray, and any palette can move into a tray.
- Library (`,`): projects, models, brushes, alphas, materials, images, splats, QuickSaves and recent files as
  thumbnails across the top of the canvas; brush thumbnails are sculpted for real; projects save a thumbnail.
- Pop-up pickers: Brush (`B`, then type letters: B S T = Standard), Material (`M`, previews on the model as you
  hover), Size / Strength at the cursor (`S` / `I`).
- Hold Ctrl over any button for what it does, its shortcut and its command (Ctrl+Alt for scripting parameters).
- Command palette (F3 / Ctrl+Shift+P) finds and runs anything; the console (`` ` ``) shows every command as a
  script line, with completion and history, and copies the session as a macro.
- Customize: Preferences > Config > Customize Shelves, then Ctrl+Alt+drag to add, move or remove shelf items;
  saving the interface (Ctrl+Shift+I), Reset Layout and Shortcuts, Export / Import Interface, three layout presets, and reassignable
  shortcuts (Ctrl+Alt+click a button, then press a key). Tab hides the shelves.
- Lucide icons (ISC/MIT). Brush Lab now opens from the "AI Brush" button instead of covering the model at start.

### Graphics tablets
- Preferences > Tablet: the pen in use, a live pressure meter, a draw-test area that shows the pressure along
  your stroke, a global pressure curve (Soft / Linear / Firm, curve, min / max, threshold, full-pressure point)
  applied before each brush's own curve, Pressure > Strength / Size, and the pen's tilt when reported.
- The eraser end inverts the brush (Ctrl + eraser erases mask); it can be set to Smooth, Erase mask or Nothing.
- Pen side buttons are mapped by SculptForge instead of SDL's version-dependent clicks: lower = right-click
  (drag to rotate, context menus), upper = Alt (invert), third = pan; each can be Invert, Smooth, Rotate view,
  Pan view, Right-click or Nothing.
- No pressure spike at the start of a stroke (the first dab waits for the touch's own pressure sample), every
  pen sample is used with the pressure interpolated between samples, the mouse always presses fully, and a pen
  that leaves the tablet's range mid-stroke ends the stroke cleanly.
- UI tests drive a simulated pen (`pen_stroke`, `pen_button`, `pen_hover`, ...), see tests/ui/tablet.sfui.
  Guide: docs/tablet.md.

### ComfyUI
- Generate 3D can run on your own ComfyUI (local or a studio GPU server): any API-format workflow, with
  ready-made Hunyuan3D 2.1 image-to-3D and FLUX + Hunyuan3D text-to-3D workflows, a connection test, queue
  progress, cancel, and clear errors (missing nodes or models, UI-format files). `image_to_3d` / `text_to_3d`
  take `service=comfyui workflow= url=`; new `comfyui_check`. See docs/comfyui.md.
- ComfyUI-SculptForge node pack (integrations/comfyui): load, inflate image, relief, voxel remesh, decimate,
  subdivide, LOD chain, export (GLB, FBX, USDA/USDZ, OBJ, STL, PLY), mesh info and open in SculptForge; works
  with ComfyUI's core MESH type.
- tools/comfyui-validate-workflow.py and tools/comfyui-smoke.sh check and run workflows against a server.

### Parts
- A scene can hold several meshes (Parts): each keeps its own attributes, sculpt layers and undo
  history. Only the active one is sculpted; the others are drawn slightly dimmed.
- Parts panel: select, rename (double-click), show/hide, Solo, Unhide all, reorder, Append (sphere, cube,
  plane or a model file), Duplicate, Delete, Merge down, Merge visible, Split by face groups, Split into parts.
  Alt+click a Part in the viewport to select it. New `part_*` commands for scripts, Python and MCP agents.
- Files: projects save every Part (format version 2; version 1 projects still open). OBJ (`o` objects), glTF,
  FBX (an LOD group per Part), USD and Bridge files export each visible Part as its own object, and multi-object
  files open as Parts. PLY, STL and splat conversion use the visible Parts merged.
- OBJ import: `g` groups now become face groups and `o` objects become Parts.
- New cube primitive.

### Brush library and sharing
- Custom brushes persist: Brush Lab saves every brush made or refined there to your brush library (a moment
  after the last change), and it loads at every start in the app, `sculptforge-cli` and Python.
- Brush codes: right-click a brush > Copy brush code gives one line of text (`sfbrush1:...`) to share in chat
  or a forum. Ctrl+V in the viewport, or Paste brush in Brush Lab, installs a pasted code or `.sfbrush` text.
  Dropping or opening a `.sfbrush` file installs it too.
- The Custom brushes list shows where each brush comes from (studio, gallery, project, unsaved, or `yours` when
  your version replaces a built-in or studio brush) and has a right-click menu: copy code, save, rename,
  delete (with confirmation), reveal in folder.
- Studio brush folders: list read-only shared folders in `SCULPTFORGE_BRUSH_PATH`; the Rez package adds
  `SCULPTFORGE_STUDIO_BRUSHES` when set. Precedence: yours > studio > built-in.
- New commands `brush_install`, `brush_code`, `brush_delete`, `brush_rename` and `brush_library`;
  `brush_list` shows each brush's source.
- Community brushes (View > Community brushes): browse, search and install from the CC0 gallery at
  github.com/islandmed/sculptforge-brushes (21 brushes to start), with checksums, an offline cache and a
  "Share yours" link; also on the website (brushes.html) and as `brush_gallery_search` / `brush_gallery_install`.
- New `stroke` command sculpts a stroke along points headlessly (scripts, MCP agents, brush previews).

### Fixed
- Voxel remesh no longer leaves concentric voxel rings on curved surfaces and lands exactly on the original
  surface (it meshes the signed distance instead of blurred voxels).
- Side panels (Brush Lab, Tools, Generate 3D...) never grow past the bottom of the window. They move up
  and scroll instead, so expanded sections such as AI provider are always reachable.

### Development
- UI tests: `sculptforge --ui-test script.sfui` drives the real interface (clicks, typing, strokes,
  shortcuts), checks what it shows and saves screenshots. They run in CI on Linux.

### AI
- Brush Lab is free out of the box: the new default **Free** provider uses an open model hosted by
  Pollinations.ai with no account or key, and falls back to a running local Ollama. Key-less Anthropic
  settings from earlier versions switch to Free automatically.
- Local models made easy: Ollama and LM Studio presets, **Find models** (checks the connection and lists
  installed models), and setup steps for your OS in the AI provider panel. New guide:
  [Run AI on your own computer](docs/local-ai.md). The default local model is now `qwen3:4b-instruct`.
- Switching the Brush Lab provider no longer resets your image-to-3D settings.

## [0.2.0] - 2026-10-21

### Sculpting
- New brushes: Sharp Crease, Clay Block, Tendril, Trim (dynamic; Alt fills), Layer and Nudge. Keys 1-9 and 0
  select the first ten brushes.
- Stroke types: dots, drag-rect, spray and lazy mouse.
- Brush alphas: round, square, noise or any grayscale image (`brush_set_alpha`).
- Mesh attributes shared by every tool: mask, vertex paint, face groups, hidden faces and UVs.

### Masking
- Hold Ctrl while stroking to paint the mask, Ctrl+Alt to erase it. Ctrl+click the background inverts it and
  Ctrl+drag there clears it. Every brush and vertex paint leave masked areas alone.
- Commands `mask_clear`, `mask_all`, `mask_invert`, `mask_blur`, `mask_sharpen`, `mask_grow`, `mask_shrink`,
  `mask_by_cavity` and `mask_by_facegroup`, plus a Masking section in the Tools panel.

### Face groups and visibility
- `facegroup_auto_by_normals`, `facegroup_by_connectivity`, `facegroup_from_mask`, `facegroup_clear`,
  `facegroup_merge_visible`.
- classic visibility: Ctrl+Shift+click isolates a face group (click again to invert), Ctrl+Shift+Alt+click
  hides it, Ctrl+Shift+click on the background shows everything, Ctrl+Shift+drag hides outside a rectangle (Alt:
  inside). Commands `hide_facegroup`, `isolate_facegroup`, `show_all`, `invert_visibility`, `hide_rect` and
  `delete_hidden`. Brushes, vertex paint and mask operations skip hidden faces.

### Vertex Paint and materials
- Paint brush (P), "RGB while sculpting", colour pick (C), and the `set_color`, `paint_fill`, `paint_clear` and
  `paint_from_cavity` commands. Fills respect the mask by default.
- Six built-in Lit Sphere materials (Red Wax, Clay, Skin, Chrome, White, Bronze) in a new Vertex Paint & Materials panel.
- Vertex colours are exported to PLY (`export_ply`), USD (`displayColor`), glTF, OBJ and Bridge files.

### Projects
- Native `.sfproj` project files (`save_project`, `open_project`, and `open`) hold the mesh, sculpt layers, mask,
  vertex paint, face groups, visibility, UVs, splats, the brush library and view settings.
- Ctrl+S / Ctrl+Shift+S save a project; OBJ export moved to File > Export OBJ.
- Save / Don't Save / Cancel prompt before losing unsaved changes, unsaved marker in the title bar, recent files,
  and autosave with an offer to restore after a crash.

### Interchange
- Bridge: Bridge file import and export with face groups, mask, vertex paint and UVs (quads are kept), and the
  shared-folder exchange (`bridge_send`, `bridge_receive`, File > Bridge) on Windows and macOS. The format follows
  the open-source GoB add-on. See [docs/bridge-format.md](docs/bridge-format.md).
- STL import (binary and ASCII) and binary STL export.

### Security and robustness
- File loaders were fuzzed with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer (about 8 million
  inputs). Malformed PLY headers could request huge allocations or cast invalid values, and NaN or far-off
  coordinates could overflow the vertex-welding grid; all are fixed, with regression tests.
- The Bridge refuses shell-unsafe paths before launching the other application.

## [0.1.0] - 2026-10-20

First public release.

### Sculpting
- Brushes: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch; X/Y/Z symmetry; tablet pressure;
  classic modifiers and navigation.
- BVH-accelerated picking with incremental refit; memory-budgeted unlimited undo.
- Loop subdivision, voxel remesh, quadric decimation.
- Sculpt layers (blend shapes) with live weights and playback.

### AI (free built in, or bring your own)
- Brush Lab: describe a brush, get a procedural brush recipe; Anthropic or any OpenAI-compatible endpoint,
  including local models through Ollama / LM Studio.
- Generate 3D from an image or from text: free via Hugging Face Spaces (Hunyuan3D, TRELLIS.2, Stable Fast 3D),
  or paid via fal.ai / Meshy, or your own generator command; results are remeshed for sculpting.
- Offline image tools: silhouette blockout, relief, stamp-from-view.

### Pipelines and formats
- FBX export (binary 7.4, LOD groups, blend shapes) and import; glTF/GLB (morph targets); OpenUSD USDA/USDZ
  (UsdSkel blend shapes, UsdPreviewSurface); OBJ; PLY.
- Gaussian splats: PLY, .splat, glTF `KHR_gaussian_splatting`, OpenUSD `ParticleField3DGaussianSplat`; splat
  rendering; splats to mesh; sculpt to splats.
- Game-ready presets: one-click LOD chains; Unreal Engine preset and **Send to Unreal** over Python remote
  execution (same machine or local network).
- `sculptforge-cli` for scripting and pipelines, with an MCP server (`--mcp`) for AI agents.

### Studios
- VFX Reference Platform CY2026 / CY2027 builds (Rocky Linux 8/9, GCC 14, C++20), verified in CI.
- Python 3.13 module (`import sculptforge`): every command, NumPy mesh access, layers, scripts.
- Rez package (`packaging/rez`).
- Headless thumbnail/turntable rendering and scripted demo recording.

[Unreleased]: https://github.com/islandmed/SculptForge/compare/v0.9.2...HEAD
[0.9.2]: https://github.com/islandmed/SculptForge/compare/v0.9.1...v0.9.2
[0.9.1]: https://github.com/islandmed/SculptForge/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/islandmed/SculptForge/compare/v0.8.1...v0.9.0
[0.8.1]: https://github.com/islandmed/SculptForge/compare/v0.8.0...v0.8.1
[0.8.0]: https://github.com/islandmed/SculptForge/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/islandmed/SculptForge/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/islandmed/SculptForge/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/islandmed/SculptForge/compare/v0.4.2...v0.5.0
[0.4.2]: https://github.com/islandmed/SculptForge/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/islandmed/SculptForge/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/islandmed/SculptForge/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/islandmed/SculptForge/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/islandmed/SculptForge/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/islandmed/SculptForge/releases/tag/v0.1.0
