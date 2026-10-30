# Changelog

All notable changes to SculptForge are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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
- **File > Export STL...** as its own menu item (it was inside the ZBrush (GoZ) submenu).

### Changed
- Soft Clay is the default material: matte, with a clear light and shadow side and a soft rim, so sculpted
  detail reads at a glance. It is first in the material picker; a material you picked before is kept.
- Orange now marks only the current tool or mode (Draw, Sculpt...). Options that are on (Zadd, symmetry X,
  Pressure > Strength, Mask, Paint...) are a lighter grey with a thin orange line.
- Every brush icon is drawn the same way, with a mark showing what the brush does.
- The right tray's sections, the selected SubTool and the colour picker have a calmer, tidier style.

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
- Splats palette > **From scan > Make sculptable**: one click turns the capture into a new SubTool next to the
  splats (which are hidden, not deleted): a watertight, manifold surface (opacity-weighted Gaussian density on a
  voxel grid, built in parallel z slabs with the same result on any thread count; gaps closed; floater shells
  dropped; space boxed in on 5 of 6 sides filled, which seals open bottoms; largest piece kept), QRemesh quads at
  a **Game** (4,000), **Film** (12,000) or **Sculpt** (2,000 + 2 subdivision levels with the scan projected onto
  the top) budget, and the capture's colours as polypaint or an Auto UV texture (splats inside the subject are
  faded out). A hidden "<name> Scan" SubTool keeps the dense surface for ProjectAll.
- **Face loops**: a face is looked for on the scan from the current view (depth and colour, no ML model, no
  download): eye corners, nose tip, mouth corners and chin become QRemesh Guides (rings round the eyes and the
  orbits, the lips, the nasolabial loop), mirrored, with QRemesh symmetry. **Review** stops after the scan to
  drag the landmarks (the mirror follows) or **Place by hand**, then **Finish**.
- **Crop Box** with draggable face handles in the viewport; options for Detail, Min Opacity, Close Gaps, Fill
  holes, All pieces, Keep scan. Progress bar and Cancel; Ctrl-hover help; F3 entries.
- Commands: `splat_to_mesh` now runs the whole pipeline (`detail`, `preset`, `target`, `levels`, `face_guides`,
  `landmarks`, `front`, `up`, `color`, `texture_size`, `crop`, `fill_holes`, `keep_parts`, `keep_scan`, `stage`
  scan/finish, ...; `resolution` still works) and adds SubTools instead of replacing the sculpt; `face_guides`;
  `splat_test_capture` (a built-in test head or mug capture, no download).
- 5 million splats to Film quads with face loops in about 9 s, 0.9 GB (M1 Pro).
- Guide chapter "From scan to sculptable mesh"; UI test `make_sculptable.sfui`; UI tests: `drag_landmark` verb,
  `hover` scrolls a panel to the item as `click` does.

### Added
- Facial shapes (Tool > Facial Shapes, `face_shapes_generate`): the 52 Apple ARKit blend shapes, exactly named, as
  sculpt layers on the active SubTool, from a built-in template head (ICT-FaceKit's ICT Face Model Light, MIT):
  automatic or landmark-guided fit (similarity + non-rigid ICP), deformation transfer (Sumner and Popovic 2004) so
  the shapes follow the sculpt's proportions, mirrored Left/Right on symmetric sculpts, made on a low subdivision
  level and carried up with the detail. Eye SubTools get eyeLook rotations, teeth and tongue the lower jaw's motion,
  brows and lashes follow the skin. Face test presets and a slider per shape; `face_landmarks_detect`,
  `face_preset`.
- Live face: `face_weights json={...}` sets ARKit-named shape weights on every SubTool, re-evaluating only the
  vertices they move (no undo step, partial GPU upload; about 9 ms for all 52 on a 700k-vertex head on an M1 Pro).
- Face takes: play recorded face feeds (JSON frames, `face_take_play`), bake them (`face_take_bake`) into a glTF
  animation of the morph target weights (GLB) and USD blend shape weight time samples; saved with the project.
- `new_head`: a new scene with a life-size human head (with mouth and eye sockets) and its eyes.
- Blender bridge: sculpt layers arrive as shape keys with their names and weights (a `.shapes` file beside each
  .GoZ, which now holds the base shape when the mesh has layers).
- UI tests: `zoom` and `look` steps; `run` expands `$SCRIPT_DIR`.
- Splat training from captures (Splats > Train from capture): a phone video, a folder of photos, a phone-link
  capture or a COLMAP / nerfstudio dataset becomes a Gaussian splat that appears in the viewport after the first
  checkpoint and sharpens while it trains (worker thread; orbit freely). Progress over the canvas: step, PSNR,
  elapsed and remaining time, Good enough - stop, Cancel. Presets Fast preview and Quality. Uses Brush 0.3.0
  (Apache-2.0, WebGPU: NVIDIA, AMD, Intel, Apple), downloaded on request from its GitHub release with a pinned
  SHA-256, or a path / `SCULPTFORGE_BRUSH`; COLMAP (BSD; downloaded on request on Windows) for camera positions,
  skipped for captures with AR poses in `capture.json`; ffmpeg for video, keeping the sharpest frame of each
  stretch. The result is centred and levelled from the camera positions, saved next to the capture and copied to
  the LightBox Splats tab. Dropping a video onto the window trains it. Commands `splat_train` and
  `splat_train_cancel` (blocking in sculptforge-cli, Python and MCP). Guide chapter "Scan an object into a splat";
  a generated CC0 test capture (tools/splat/make_test_capture.py, tests/data/splat_capture).
- **Capture from phone** (File > Capture from phone, or the phone button in the LightBox; guide chapter "Phone capture
  and AR", design in docs/phone-link.md): scan the QR code and the phone's browser becomes SculptForge's camera,
  with no app and no account. Send a scan video or a photo burst (with coaching; for splat training), one photo
  (to Image to 3D), or a splat .ply from Scaniverse, Polycam or Luma; uploads are chunked and resume after Wi-Fi
  drops, and land in `<library>/Captures/<date_time>-<kind>/` with a `capture.json`. **View in AR** shows the
  active SubTool or the scene on your desk (USDZ for iPhone AR Quick Look, GLB for Android Scene Viewer; reduced to
  80k triangles, polypaint and textures baked, re-exported after strokes). **Face capture** tracks 52 ARKit
  expressions and the head on the phone (MediaPipe Face Landmarker) and streams them to SculptForge at about 30 fps
  over a WebRTC data channel, for `face_weights`; **Record face take** saves them as JSON Lines. LAN only, a new
  token per session, size limits, safe file names; nothing listens while the window is closed.
- Build: WebRTC through libdatachannel (MPL-2.0) and Mbed TLS (Apache-2.0), fetched and linked statically;
  `-DSCULPTFORGE_WEBRTC=OFF` builds without the live face feed.
- Website: the face capture page (`website/link/`), which needs HTTPS for the phone's camera.
- Services: `services/link-worker`, a Cloudflare Worker (free plan) that relays the face page's one encrypted
  connection message to the desktop at `link.sculptforge.app`; ntfy.sh stays as the fallback. Not deployed yet.
- "Fixes & suggestions" in the LightBox header (shown at startup) and the Help menu: opens the bug report and
  feature request forms on GitHub.

### Changed
- The website moved to https://sculptforge.app (old islandmed.github.io/SculptForge links redirect). The
  app, update check, user guide, packages and Blender add-on link there.
- Website: the tagline now reads "the free, open-source alternative to ZBrush and Mudbox".

## [0.8.1] - 2026-10-25

Fixes from a ZBrush artist's testing, and ZTools from ZBrush.

### ZBrush feel (tester feedback on 0.7)
- ZBrush input style: every zoom drag (Ctrl+right-drag, Alt-drag then release Alt,
  Ctrl+middle-drag) zooms out when dragged up, as in ZBrush; the SculptForge style keeps zooming in.
  Rotation and panning were checked and already follow the cursor as in ZBrush.
- Ctrl brushes, as in ZBrush: while Ctrl is held the left shelf shows the mask brush (MaskPen,
  MaskLasso, MaskRect) and its stroke (Dots, Lasso, Rect); the Brush picker (Ctrl+B or a
  Ctrl+click on the brush tile) and the Stroke picker opened with Ctrl held pick among them.
  Ctrl+Shift does the same for SelectRect and SelectLasso. Tool > Masking and Tool >
  Visibility keep their buttons.
- Polygroups > **Group by UVs** (ZBrush UV Groups): one polygroup per UV island of the visible mesh, undoable;
  `polygroup_uv` command for the console, CLI, MCP and Python.

### Texture painting (ZBrush tester feedback)
- **New Texture** on the top shelf and in the Color palette whenever the Paint brush (or Rgb) is on and the
  SubTool has no texture: one click starts texture painting, running Auto UV first (with its progress in the
  status bar) when there are no UVs. Ctrl-hover help; also in F3 search.
- Faster texture strokes: a dab paints its texels in parallel over 16-row bands with the same result on any
  number of threads, and the view uploads only the 64x64 tiles that changed. A big-brush dab on a 4K texture
  went from 29 to 6 ms (1.3K triangles) and from 47 to 11 ms (328K triangles) on an M1 Pro
  (docs/performance.md).
- Checked that texture paint doesn't depend on the mesh density: a hard stamp gives the same texels on a
  2-triangle plane as on an 8K-triangle one (new unit test). Paint that looks as coarse as the mesh is polypaint:
  the Vertex target, or polypaint shown multiplied over the texture.

### ZTools from ZBrush
- **SculptForge ZTool Export**, a ZBrush plug-in (`integrations/zbrush`, ZScript, GPL-3.0-or-later, attached to
  releases): ZPlugin > SculptForge > **Convert ZTL** / **Save for SculptForge** writes every SubTool of a ZTool at
  its highest subdivision level as `<name>.sf<NNN>_<SubTool>.GoZ` next to the `.ZTL`.
- Opening a `.ZTL` (File > Open, drag and drop, `open`) opens that conversion as SubTools in order, with names,
  polygroups, polypaint, mask and UVs, and warns when the `.ZTL` is newer than the conversion; without one the
  message points to the plug-in. A folder of numbered `.GoZ` files (`<name>.sculptforge`) opens the same way.

## [0.8.0] - 2026-10-25

Dynamic topology (Sculptris Pro), vector displacement brushes, QRemesh Guides, sculpt layers on every
subdivision level, and ZBrush-style keys in Blender's Sculpt Mode.

### Dynamic topology (ZBrush's Sculptris Pro)
Guide chapter docs/guide/dynamic-topology.md.
- **Dynamic** on the top shelf (and in the Draw palette): every brush except Move, masking and painting remeshes
  the surface under it each dab: splits long edges (on the curve the normals describe), collapses short ones,
  flips for even valences and relaxes the new vertices, only inside the brush.
- **Detail** slider (0-100): the target edge length relative to the brush radius (Sculptris Pro), or with the
  **Constant detail** preference one size for the whole mesh. Smooth remeshes towards twice the size, so it
  simplifies.
- Keeps the mesh manifold with no degenerate or flipped triangles; boundaries, polygroup borders and the
  symmetry plane only slide along themselves; fully masked and hidden parts are left alone. X/Y/Z symmetry
  remeshes mirror images together, so the topology is mirrored too.
- Carries mask, polypaint, paint layers, polygroups and sculpt layer offsets; quads under the brush become
  triangles; UVs are dropped (Undo restores them). Meshes with subdivision levels are refused, as in ZBrush.
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
  direction, X = right), scaled by the radius and Z Intensity (50 = the map's own size, 100 = twice). Alt digs
  in (X, Y kept, Z flipped, as Blender). Symmetry mirrors the stamp exactly (the mirrored pass flips the frame's
  handedness); mask, Front faces only, sculpt layers and undo as every brush. Works with subdivision levels.
- **DragDot stroke**: one stamp of the Draw Size that slides under the cursor until release (any brush).
- **Built-in maps** (procedural, CC0): Horn, Ear Curl, Scales, Nostril.
- **Brush > Vector Displacement**: the maps with thumbnails, Load VDM (OpenEXR 16/32-bit, any compression, or
  .sfvdm; dropping an .exr loads it too) with Swap Y/Z, Flip X/Y/Z and Scale for maps from other apps, New VDM
  Plane, Create VDM, Export EXR. While the VDM brush is the brush, the Alpha tile and picker show the maps, and
  the stroke switches to DragRect (the previous stroke comes back with the next brush).
- **Create VDM from a plane** (ZBrush's VDM plane, Blender's VDM Brush Baker): a 2 x 2 VDM plane with UVs and
  a masked border as a new SubTool (the others hidden, top view); Create VDM rasterizes each point's offset
  through the UVs into a map saved in the brush library (Divide keeps the UVs).
- **Blender's convention**, checked against the VDM Brush Baker's source and in Blender 4.2 and 5.2: RGB = XYZ in
  brush radii over the brush's square, first scanline = top. Exported EXRs read in Blender with the same vectors
  the same way up; Blender's 32-bit ZIP/PIZ and 16-bit EXRs load unchanged.
- **Library**: VDMs live as `.sfvdm` (header, name, float32 XYZ) next to the `.sfbrush` files, user over studio
  over built-in by name; project settings remember the brush's map by name.
- Commands: `vdm_set`, `vdm_list`, `vdm_export`, `vdm_new_plane`, `vdm_capture`, `vdm_stamp`, `vdm_delete`; the
  `stroke` command and Python/MCP stamp the brush's map too. OpenEXR via vendored tinyexr 1.0.13 (BSD-3), built on
  stb's zlib (no new dependency).

### Blender add-on: ZBrush-style keys in Sculpt Mode
Guide: docs/guide/blender.md, "ZBrush-style keys in Sculpt Mode".
- Add-on preference **ZBrush-style keys in Sculpt Mode** (off by default): S / U / O radial Draw Size,
  Strength and Hardness (Focal Shift); Space the Sculpt context menu; B a brush list by ZBrush name (type to
  filter; Blender 4.3+ brush assets, 4.2 brush tools); Alt+drag Zsub; Ctrl(+Alt)+drag mask / unmask lasso;
  Ctrl+Shift(+Alt)+drag show only / hide inside; Ctrl+W group masked and clear the mask (else group visible);
  Ctrl+D Divide (Multires), D / Shift+D SDiv up / down; X symmetry; Shift+F wireframe; F frame; P perspective;
  L Stabilize Stroke; W / E / R move / scale / rotate. Turning it off (or disabling the add-on) removes them.
  The preferences and the guide list the ZBrush keys Blender has no equivalent for.
- tests/blender/test_in_blender.py checks every key's operator and settings, the data paths (Blender 4.2's
  unified paint settings vs 5's per-mode ones), the brush picks, SDiv and Divide, on/off, and the VDM EXR
  round trips; run in real Blender 4.2.23 and 5.2.3.

### QRemesh Guides (ZBrush's ZRemesher Guides)
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
- Guides belong to each SubTool, are saved in projects (`qremesh_guides` per SubTool in the manifest, no format
  change: older projects open, older versions ignore them) and copied to the kept original. Guides left far from
  the surface (the SubTool moved) are ignored with a note.
- Scripting: `qremesh_guide` (points, view, symmetry), `qremesh_guide_delete_last`, `qremesh_guides_clear`,
  `qremesh_guides_info`; `qremesh` gains `guides` and `curves_strength`.
- Keep Groups (loops along polygroup borders) and Use Polypaint (density) were already there.

### Sculpt layers on subdivision levels
- Checked against ZBrush's 3D Layers and covered by tests: a layer recorded at the top survives Lower / Higher Res
  bit for bit, shows at lower levels as the same strokes in the base would, keeps one weight for every level, and
  survives Divide, Reconstruct Subdiv, Del Lower / Del Higher, undo, saving and loading at any level, and blend
  shape export from any level (`subdiv_export`). Layers added or sculpted at a low level show above. Nothing needed
  fixing; the Layers panel got Ctrl-hover help and the guide a "Layers and subdivision levels" section.

### Other
- UI tests: `camera YAW PITCH` step; the guide's screenshot script turns the mesh back on after the splats shot
  (the subdivision-level shots after it showed only splats).
- `sculptforge_tests NAME...` runs only the tests whose names contain one of the arguments.

## [0.7.0] - 2026-10-25

ZBrush's keys and gestures by default, and a bridge to Blender.

### ZBrush feel: ZBrush's keys and gestures by default
Audit in docs/zbrush-parity.md (sources: Maxon's ZBrush shortcut sheet and documentation); guide chapter
docs/guide/coming-from-zbrush.md.
- **U / I / O**: Z Intensity, RGB Intensity and Focal Shift sliders at the cursor, as in ZBrush (S stays Draw
  Size). Values show 0-100 (Focal Shift -100..100); the top shelf and Draw palette say Z Intensity instead of
  Strength.
- **RGB Intensity**: painting has its own strength (default 100), separate from Z Intensity.
- **Focal Shift**: the brush falloff, for every built-in brush, Move, SnakeHook, masking and painting (Brush Lab
  recipes keep their own tip). At 0 the maths is unchanged. The cursor's inner ring now shows Focal Shift
  (where the brush works at half strength) instead of the strength.
- **Smooth keeps its own Z Intensity** (default 50): Shift-smoothing no longer follows the active brush's
  strength. Holding Shift (or picking Smooth) shows and sets it.
- **Space**: the QuickMenu under the cursor (brush, stroke, alpha, material, Draw Size, Focal Shift, the
  intensities, Zadd/Zsub, Sculpt/Paint, Rgb, X). Hold and let go to close, or tap to keep it open.
- **Brush letters**: B C B ClayBuildup, B D S DamStandard, B M V Move, B I N Inflate, B P I Pinch, B S H
  SnakeHook (ZBrush's), instead of the first free letter.
- **Mask keys**: Ctrl+A Mask All, Ctrl+Shift+A Clear Mask, Ctrl+I Invert, Ctrl+H View Mask; **Ctrl+W** Group
  Masked (and clear the mask; with nothing masked, Group Visible).
- **Ctrl+click on the model blurs the mask, Ctrl+Alt+click sharpens it** (a Ctrl+drag still paints).
- **Shift pressed after a stroke starts** draws a straight line, snapped to 45 degree steps.
- **Shift+click the background, let go of Shift, drag** rolls the view about its axis.
- **Shift+Alt+click a SubTool** selects and frames it.
- **Ctrl+Shift+click a polygroup on a partly hidden mesh hides it** (it used to isolate it); a Ctrl+Shift
  rectangle around nothing reverses the visibility.
- **L** toggles LazyMouse, **V** swaps the main and secondary colour, **Ctrl+Shift+Alt+I** is Save UI.
- **Preferences > Input**: Style **ZBrush** (default) or **SculptForge**, which keeps 0.6's keys: 1-9 and 0 pick
  brushes (ZBrush style: free, 9 = QuickSave), I = Z Intensity, Ctrl+drag on the background zooms when nothing
  is masked (ZBrush style: nothing). Rotation (free / turntable) is also here.
- Projects store Smooth's Z Intensity, RGB Intensity and Focal Shift.

### Blender bridge
- **SculptForge Bridge, an add-on for Blender 4.2+** (`addons/blender`, GPL-3.0-or-later): select objects, click
  **Send to SculptForge** (the 3D View's SculptForge tab, the Object menu or File > Export), sculpt, then
  **File > Blender > Send to Blender** in SculptForge. The objects update in place in Blender, keeping their
  transform, materials and modifiers; new SubTools become new objects. Works on Linux, Windows and macOS.
  - The two sides share a per-user folder (`<user data>/SculptForge/SculptForge/blender`) and exchange `.GoZ` files
    plus a JSON manifest per direction (object names, `matrix_world`, a revision); each side checks the other's
    manifest once a second. No network. SculptForge records where it is installed and writes a heartbeat there, so
    the add-on starts it (`--blender`) only when it isn't running.
  - Identity: Blender object name <-> SubTool (its GoZ name). Positions travel in object space with the matrix;
    Z-up <-> Y-up, metres both; mirrored objects keep their faces pointing out. A round trip is the identity
    (positions, faces and winding, UVs, polypaint, mask, polygroups): tested in C++, from the add-on's side
    against `sculptforge-cli`, and in a real Blender in CI.
  - Unchanged topology updates only positions and attributes, so Blender keeps n-gons, edges, seams and other
    data; changed topology rebuilds the mesh. Active UV map <-> UVs, active colour attribute <-> polypaint, sculpt
    mask <-> mask, face sets <-> polygroups. Modifiers can be kept live or applied (then turned off on return).
- Commands `blender_send` and `blender_receive` (CLI, MCP, Python), **File > Blender** (Send, Receive, Receive
  automatically), F3 entries and Ctrl-hover help. GoZ receive and the bridge share the code that matches objects
  to SubTools.
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
- Lower Res after sculpting at the top level is faster (33M: 1.5 -> 0.9 s), and so is Divide (2.4 -> 1.4 s).
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
  square-root companded curvature. Polypaint is 8-bit (gamma encoded), the mask 8-bit, polygroups 16-bit. With
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
- Divide (Tool > Geometry, Ctrl+D, `subdiv_divide`) adds a subdivision level per SubTool and keeps the lower
  ones: Catmull-Clark on meshes with quads (quads stay quads), Loop on triangle meshes, flat with Smt off.
- Lower Res / Higher Res (Shift+D / D, `subdiv_lower`, `subdiv_higher`) and the SDiv slider (`subdiv_level`)
  step between levels. Detail is stored per level as a displacement in the local frame of the smoothed level
  below, so reshaping a low level carries it along; edits at a high level are restricted (full weighting) to
  the lower ones. Round trips without edits are bit-for-bit.
- Mask, polypaint, paint layers, polygroups, hidden faces and UVs follow the levels (copied down, interpolated or
  inherited up where they changed); sculpt layers have their own detail per level; the texture is shared.
- Del Lower, Del Higher (`subdiv_delete_lower`, `subdiv_delete_higher`) and Reconstruct Subdiv
  (`subdiv_reconstruct`: finds the lower level of a regular Catmull-Clark or Loop subdivision, keeping the mesh
  and its vertex order). `subdiv_info` lists the levels; `subdiv_export` writes the visible SubTools at another
  level (e.g. the base cage) in any export format.
- Each level change is one undo step, as is each stroke at any level; level edits share unchanged levels
  copy-on-write. The two levels left last keep their topology and BVH, so stepping back and forth is instant
  (1-45 ms at 6.5M triangles).
- Projects save every level (project format 4). The SubTool list shows "SDiv n/m".
- Ctrl+D now runs Divide; the one-way Subdivide stays in Tool > Geometry and as `subdivide`.

### Gizmo 3D (moving, scaling, rotating and posing)
- Gizmo 3D modes, as in ZBrush: W Move, E Scale, R Rotate, Q back to Draw (top shelf, Transform palette, F3).
  Move has axis arrows, plane squares and a view-plane centre; Rotate has axis rings and a view ring; Scale has
  axis boxes and a uniform centre. Shift snaps (15 degrees, a round grid step, 10%). The amount shows beside the
  gizmo while dragging. Dragging off the handles still navigates; Ctrl still masks.
- Transforms move the active SubTool, or only its unmasked part: partly masked vertices move partly, hidden ones
  never. Falloff eases the movement in over a distance from the mask's edge (measured along the surface, as a
  share of the size) for soft joints when posing. With symmetry on, a masked transform is mirrored across the
  symmetry plane.
- Pivot: follows the centre of what moves; Alt+drag the gizmo to move it (or turn its axes) without the model,
  Alt+click the model or To Surface to put it on the surface, Reset; World or Surface axes (Y along the normal,
  turning with rotations). The gizmo follows undo / redo of its own steps and is saved in projects (`gizmo` key,
  no format bump).
- Gizmo 3D panel: pivot, options and numeric Move / Rotate / Scale with Apply; All SubTools (Transpose All, one
  undo step per SubTool).
- `transform` command (translate, rotate, scale, pivot, orient, masked, falloff, symmetry, all); each gizmo drag
  is echoed to the console as the command that repeats it.
- One undo step per drag that restores positions bit for bit. Sculpt layers move with the surface (the base and
  every layer's offsets). Topology, quads, UVs, textures, polypaint and polygroups are untouched.
- Fast on big meshes: a drag recomputes from the drag's start (no drift), transforms only the moving vertices
  through a per-weight lookup table on several threads, turns normals of rigid parts and recomputes only where
  the movement varies, uploads only the changed vertex range and refits the BVH once on release. On 1.3M
  triangles: about 2 ms to transform and 7 ms to upload per drag frame, 20-40 ms on release.

### Normal, displacement and AO map baking
- Tool > Bake (and the `bake` command) bakes a high-poly source (one SubTool, or every visible one but the
  active) onto the active SubTool's UVs: tangent-space normal, object-space normal, displacement, ambient
  occlusion, cavity and colour (polypaint or painted texture) maps, 1K to 8K.
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
- Maps are written as `<SubTool>_normal.png`, `_ao.png`, `_displacement.png` / `.exr` and so on, beside the
  last export by default.

### Quad retopology and detail projection
- QRemesh (Tool > Geometry > QRemesh, `qremesh`): automatic quad retopology like ZBrush's ZRemesher. A
  field-aligned method after Instant Meshes (Jakob et al. 2015; field formulas follow its BSD-3 reference code,
  credited in THIRD_PARTY_NOTICES.md): a multi-resolution 4-RoSy orientation field and position field, polygon
  extraction, then every polygon split into quads and relaxed onto the sculpt. The result is 100% quads,
  2-manifold and closed where the sculpt is, within about 5% of the target count.
- Options: Target Polygons with Half / Same / Double, Adaptive Size (curvature-driven quad size), Keep Groups
  (edge loops along polygroup borders, groups kept), Detect Edges (loops on hard edges over 50 degrees),
  Use Polypaint (red = up to 4x the density, blue = down to a quarter), Symmetry (one half remeshed and mirrored,
  welded on the seam), Keep Original (the old SubTool kept hidden below, for ProjectAll).
- Runs in the background in the app with a progress bar and Cancel; one undo step. Mask and polypaint are
  interpolated onto the new mesh; sculpt layers are baked.
- ProjectAll (Tool > SubTool > Project, `project_all`): moves the active SubTool onto the other visible
  SubTools (rays both ways along the normal, nearest hit within Dist, else the closest point) and takes their
  polypaint. Dist, Mean, PA Blur, Geometry / Polypaint toggles, an optional single `source`; respects the mask
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
- The first polypaint stroke on an unpainted mesh keeps the (empty) paint stack for undo instead of a copy of
  the whole mesh.
- Projects: archives stream to disk (no in-memory copy), entries are read in place, CRCs are computed in
  parallel, arrays are copied rather than converted byte by byte; ZIP64 past 4 GB (smaller archives are
  byte-identical to before).
- The window has no multisampling; the 3D view antialiases in its own target (Renderer::drawView), so it can
  use fewer samples when the GPU has no room, and Fast Navigation draws scenes of 10M+ triangles without
  antialiasing while the view moves. A SubTool that runs the GPU out of memory is not drawn (status message).
- Preferences > Performance: Antialiasing (Off / 2x / 4x / 8x), Fast Navigation, Multithreading, the last
  stroke's cost per dab and memory; `perf_info` and `set_multithreading` commands.

### Polish of the v0.5 features (known issues fixed)
- Auto UV on a SubTool with a painted texture now moves the paint and every texture layer onto the new UVs
  instead of scrambling it (each texel of a new UV triangle samples the old texture at the same point of the same
  triangle, islands padded 8 texels); one undo step restores the old UVs and texture (`uv_auto
  keep_texture=false` for the old behaviour). 1.05M triangles, 4K texture with a layer: 0.2-0.3 s.
- Keep Texture (Tool > Remesh, Tool > Geometry > QRemesh; `keep_texture` on `remesh` and `qremesh`, default on):
  the rebuilt surface gets Auto UVs and the texture, all layers, is projected onto it from the old surface, in the
  same undo step. Voxel Remesh of a painted SubTool runs in the background with progress and Cancel. Merging
  SubTools (`subtool_merge_down`, `subtool_merge_visible`) keeps their textures in one texture on new Auto UVs.
- Decimate no longer scrambles a texture at UV seams (seam vertices stay in place).
- Auto UV no longer crashes or folds UVs on dense smooth meshes (e.g. the default 82k-triangle sphere): fixed in
  third_party/xatlas. UV islands are capped at about 4000 triangles, so a 1.3M-triangle sculpt unwraps in about
  16 s instead of minutes.
- Subdivision levels: Voxel Remesh, Decimate, QRemesh, Subdivide, Delete Hidden and merging or splitting SubTools
  say in the status line that they removed the levels and which one became the new base ("Subdivision levels
  removed: SDiv 3 of 5 kept as the new base"); undo restores every level (now covered by tests).
- Subdivision levels: a sculpt layer's fine detail now turns and bends with a low level rotated with the Gizmo 3D
  or reshaped (layer details are stored relative to the surface). Project format 5; format 4 projects are
  converted when they open.
- Subdivision levels: only the active SubTool keeps its recently visited levels cached; selecting another SubTool
  frees 50-170 MB per SubTool with 1.8M triangles (the first level step after coming back takes about 0.1 s).
- QRemesh: Symmetry mirrors across the model's own middle plane, so models away from the world centre work. A
  model that isn't mirror-symmetric is remeshed without symmetry and the status line says so; a nearly symmetric
  one keeps mirrored topology on each side's own surface. Symmetry is on by default in the panel; new
  `symmetry_center` argument for a fixed plane.
- QRemesh: small separate pieces (eyes, rivets) are no longer dropped: they are remeshed on their own with at
  least 32 quads, or kept as they were. Parts thinner than a quad (blades, fins, slabs) keep both sides and their
  hard edges (thin slab: mean error 2.2% -> 0.05% of its size, Hausdorff 8.8% -> 2.6%).
- Top shelf: the Vertex / Texture paint target is hidden while the SubTool has no texture, instead of sitting
  there greyed out (docs/ui-design.md now states the rule for contextual shelf buttons); Draw Size and Strength
  shrink to fit when the row is full.
- Exports: the `.mtl` and PNGs written beside a model whose file name has spaces use `_` instead (OBJ's `mtllib`
  and MTL's `map_Kd` split names at spaces). Textured exports were validated with other tools: Khronos glTF
  validator (no errors or warnings), OpenUSD's validators (no issues), ufbx strict mode, and a texture
  orientation check per format; `tools/export_check/run.sh` repeats it.
- UI tests: `wait_idle` waits for several idle frames in a row, which fixes the flaky `ui_lightbox` under a
  loaded parallel ctest.

### Changed
- Wireframe (PolyF) is Shift+F only: W is now the Gizmo 3D's Move, as in ZBrush.

## [0.4.2] - 2026-10-23

### Texture painting on UV maps
- Paint into a texture on the model's UVs instead of its vertices, so the colour detail comes from the texture
  (1K, 2K, 4K or 8K), not from the mesh density (Tool > Texture Map > New Texture, `texture_create`). The Paint
  brush, Rgb sculpting, fills, cavity paint, colour picking (C), mask, hidden faces, symmetry and pen pressure
  work as for polypaint; texture strokes never paint through to the far side. A few texels of padding around
  every UV island keep seams invisible.
- Vertex / Texture toggle on the top shelf (and in the Color, Paint Layers and Texture Map palettes) chooses
  where paint goes per SubTool (`paint_target`). The viewport multiplies the texture with the polypaint over
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
- Polypaint > Texture and Texture > Polypaint (`texture_from_polypaint`, `polypaint_from_texture`), Import and
  Export Texture (PNG; `texture_import`, `texture_export`), Delete Texture (`texture_delete`).
- Projects save textures and their layers (project format 3; version 1 and 2 projects still open, and older
  SculptForge versions refuse format 3 files instead of dropping the paint).
- Exports write the texture next to the model: OBJ with a `.mtl` (`map_Kd`), glTF/GLB with an embedded base
  colour texture, USD with a preview material, FBX and GoZ with the PNG beside the file.
- Subdivide, decimate, split and duplicate keep the texture with the UVs.

## [0.4.1] - 2026-10-23

### Fixed
- Graphics tablets: the brush ring no longer flickers between two places, which looked like a second brush
  blinking on the model (most noticeable while masking with Ctrl or smoothing with Shift). Windows Ink and some
  tablet drivers send mouse events of their own beside the pen's, often at an old position; while the pen is
  in range only the pen now moves the cursor, and the mouse takes over again once the pen goes quiet.

## [0.4.0] - 2026-10-22

Shaped by feedback from a working ZBrush modeler and a survey of ZBrush and Mudbox forums.

### Feels like ZBrush
- Free rotation by default: the model follows the cursor and can roll over the top, as in ZBrush. Hold Shift
  while rotating to snap to the nearest front, side, top or bottom view. Preferences > Interface > Rotation:
  Turntable keeps the model upright (Blender / Maya style).
- Alt+drag pans and letting go of Alt mid-drag zooms; right-drag follows ZBrush's right-click navigation
  (Alt pans, Ctrl zooms, Shift snaps). Projects remember the view's roll.
- MaskLasso and MaskRect (Tool > Masking): Ctrl+drag a lasso or rectangle to mask what's inside, Ctrl+Alt to
  unmask, starting on the model or the background.
- SelectLasso (Tool > Visibility): Ctrl+Shift+drag a free-hand lasso to keep what's inside (Alt: hide it),
  alongside SelectRect. New script commands `mask_rect`, `mask_lasso` and `hide_lasso`.
- New Sculpt material, the default: neutral clay under a strong key light with crisp highlights and deep
  cavity shading, so small strokes stay readable. Projects keep the material they were saved with.

### Quads
- Models opened from ZBrush (GoZ), OBJ, FBX, PLY or USD keep their quads: PolyF shows them as quads and
  OBJ / FBX / PLY / USD / GoZ export writes them back as quads, through sculpting, layers, undo, SubTools and
  projects.
- Subdivide turns each quad into four quads, voxel remesh produces an even quad grid (like DynaMesh), and the
  Plane and Cube primitives are quads. Decimate still produces triangles.

### Paint layers
- Photoshop-style polypaint layers per SubTool (Tool > Paint Layers): name, visibility, opacity, lock and ten
  blend modes (Multiply, Screen, Overlay, Soft Light, Add, ...). Painting is non-destructive and undoable;
  merge down, flatten, duplicate and reorder work as in Photoshop. Something ZBrush never had.
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

### A ZBrush-style interface
- Laptop camera notches: when SculptForge fills the screen, the interface stays below the camera notch of
  MacBooks (macOS reports the safe area; on Linux a notched MacBook display is recognised), with a plain band
  beside the notch. Preferences > Interface > Camera notch: Automatic / Always reserve / Off.
- Fullscreen toggle (F11, Render palette; `--fullscreen` on the command line), and minimise / close buttons at
  the far right of the title strip (close asks to save changes; Ctrl+Q quits).
- The canvas is framed by shelves, as in ZBrush: a top shelf (LightBox, Sculpt/Paint, Zadd/Zsub, Draw Size,
  Strength, symmetry, pressure), a left shelf of Brush / Stroke / Alpha / Material pickers with the colour picker,
  a right shelf of navigation (drag Move / Zoom / Rotate) and display toggles, and a bottom shelf of your most-used
  brushes. 16 alphabetical palettes hold every tool; the Tool palette (SubTool, Layers, Geometry, Remesh,
  Masking, Polygroups, Visibility, Export, Display) sits in the right tray, and any palette can move into a tray.
- LightBox (`,`): projects, models, brushes, alphas, materials, images, splats, QuickSaves and recent files as
  thumbnails across the top of the canvas; brush thumbnails are sculpted for real; projects save a thumbnail.
- Pop-up pickers: Brush (`B`, then type letters: B S T = Standard), Material (`M`, previews on the model as you
  hover), Draw Size / Strength at the cursor (`S` / `I`).
- Hold Ctrl over any button for what it does, its shortcut and its command (Ctrl+Alt for scripting parameters).
- Command palette (F3 / Ctrl+Shift+P) finds and runs anything; the console (`` ` ``) shows every command as a
  script line, with completion and history, and copies the session as a macro.
- Customize: Preferences > Config > Enable Customize, then Ctrl+Alt+drag to add, move or remove shelf items;
  Store Config (Ctrl+Shift+I), Restore Standard UI, Save / Load UI, three layout presets, and reassignable
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

### SubTools
- A scene can hold several meshes (SubTools), like ZBrush: each keeps its own attributes, sculpt layers and undo
  history. Only the active one is sculpted; the others are drawn slightly dimmed.
- SubTools panel: select, rename (double-click), show/hide, Solo, Unhide all, reorder, Append (sphere, cube,
  plane or a model file), Duplicate, Delete, Merge down, Merge visible, Split by polygroups, Split into parts.
  Alt+click a SubTool in the viewport to select it. New `subtool_*` commands for scripts, Python and MCP agents.
- Files: projects save every SubTool (format version 2; version 1 projects still open). OBJ (`o` objects), glTF,
  FBX (an LOD group per SubTool), USD and GoZ export each visible SubTool as its own object, and multi-object
  files open as SubTools. PLY, STL and splat conversion use the visible SubTools merged.
- OBJ import: `g` groups now become polygroups (as ZBrush writes them) and `o` objects become SubTools.
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
- New brushes: Dam Standard, Clay Buildup, Snake Hook, Trim (dynamic; Alt fills), Layer and Nudge. Keys 1-9 and 0
  select the first ten brushes.
- Stroke types: dots, drag-rect, spray and lazy mouse.
- Brush alphas: round, square, noise or any grayscale image (`brush_set_alpha`).
- Mesh attributes shared by every tool: mask, polypaint, polygroups, hidden faces and UVs.

### Masking
- Hold Ctrl while stroking to paint the mask, Ctrl+Alt to erase it. Ctrl+click the background inverts it and
  Ctrl+drag there clears it. Every brush and polypaint leave masked areas alone.
- Commands `mask_clear`, `mask_all`, `mask_invert`, `mask_blur`, `mask_sharpen`, `mask_grow`, `mask_shrink`,
  `mask_by_cavity` and `mask_by_polygroup`, plus a Masking section in the Tools panel.

### Polygroups and visibility
- `polygroup_auto_by_normals`, `polygroup_by_connectivity`, `polygroup_from_mask`, `polygroup_clear`,
  `polygroup_merge_visible`.
- ZBrush-style visibility: Ctrl+Shift+click isolates a polygroup (click again to invert), Ctrl+Shift+Alt+click
  hides it, Ctrl+Shift+click on the background shows everything, Ctrl+Shift+drag hides outside a rectangle (Alt:
  inside). Commands `hide_polygroup`, `isolate_polygroup`, `show_all`, `invert_visibility`, `hide_rect` and
  `delete_hidden`. Brushes, polypaint and mask operations skip hidden faces.

### Polypaint and materials
- Paint brush (P), "RGB while sculpting", colour pick (C), and the `set_color`, `paint_fill`, `paint_clear` and
  `paint_from_cavity` commands. Fills respect the mask by default.
- Six built-in MatCap materials (Red Wax, Clay, Skin, Chrome, White, Bronze) in a new Polypaint & Materials panel.
- Vertex colours are exported to PLY (`export_ply`), USD (`displayColor`), glTF, OBJ and GoZ.

### Projects
- Native `.sfproj` project files (`save_project`, `open_project`, and `open`) hold the mesh, sculpt layers, mask,
  polypaint, polygroups, visibility, UVs, splats, the brush library and view settings.
- Ctrl+S / Ctrl+Shift+S save a project; OBJ export moved to File > Export OBJ.
- Save / Don't Save / Cancel prompt before losing unsaved changes, unsaved marker in the title bar, recent files,
  and autosave with an offer to restore after a crash.

### Interchange
- ZBrush GoZ: `.GoZ` import and export with polygroups, mask, polypaint and UVs (quads are kept), and the GoZ
  bridge (`goz_send`, `goz_receive`, File > ZBrush (GoZ)) on Windows and macOS. The format follows the open-source
  GoB add-on; the live bridge has not yet been tested against a running ZBrush. See [docs/goz.md](docs/goz.md).
- STL import (binary and ASCII) and binary STL export. Opening a `.ZTL`/`.ZPR` file now explains that the format is
  proprietary and suggests GoZ or OBJ/FBX.

### Security and robustness
- File loaders were fuzzed with libFuzzer, AddressSanitizer and UndefinedBehaviorSanitizer (about 8 million
  inputs). Malformed PLY headers could request huge allocations or cast invalid values, and NaN or far-off
  coordinates could overflow the vertex-welding grid; all are fixed, with regression tests.
- The GoZ bridge refuses shell-unsafe paths before launching ZBrush.

## [0.1.0] - 2026-10-20

First public release.

### Sculpting
- Brushes: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch; X/Y/Z symmetry; tablet pressure;
  ZBrush-style modifiers and navigation.
- BVH-accelerated picking with incremental refit; memory-budgeted unlimited undo.
- Loop subdivision, voxel remesh (DynaMesh-style), quadric decimation.
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
