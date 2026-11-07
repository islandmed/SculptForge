# SculptForge Roadmap

## Why SculptForge?

Where SculptForge aims to win:

1. **Focus and speed** — a sculpt-only tool that starts instantly and stays responsive at tens of millions of polygons.
2. **First-class Linux** alongside macOS and Windows.
3. **Familiar to experienced sculptors** — controls work the way they expect.
4. **Scriptable** — every action is a named command callable from UI, scripts or AI.
5. **Free AI built in, or bring your own:** free models out of the box, provider-agnostic, and fully offline with local models.

Where we deliberately do *not* compete yet: every feature of every sculpting app,
rendering/animation (use Blender).

## Status key

✅ done · 🚧 in progress · 📋 planned

## Phase 1 — Core sculpting (v0.1)

- ✅ Cross-platform app shell (SDL3, OpenGL 3.3, Dear ImGui)
- ✅ BVH-accelerated picking and brush queries with incremental refit
- ✅ Brushes: Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch
- ✅ Symmetry (X/Y/Z), pen pressure, stroke spacing
- ✅ Undo/redo (sparse per-stroke + full snapshots for topology changes)
- ✅ Loop subdivision, sphere/plane primitives
- ✅ OBJ import/export, native file dialogs, drag & drop
- ✅ Command registry with LLM tool-schema export
- 🚧 CI builds and tests on Linux, macOS, Windows (workflow written; verified locally on Linux only)

## Phase 1.5 — Studio pipeline & AI (done)

- ✅ Sculpt layers exported as glTF morph targets (blend shapes), expression preview
- ✅ Quadric LOD chain with preview; layers preserved on every LOD
- ✅ glTF/GLB export + import (seam welding, morph targets → layers)
- ✅ `sculptforge-cli` headless commands, `$variables`, LLM tool-schema export
- ✅ Headless thumbnail/turntable rendering
- ✅ Brush Lab: recipe brush engine, procedural patterns, `.sfbrush` files, AI brush design
  (Anthropic or any OpenAI-compatible/local model)
- ✅ Image to 3D: sketch inflation, relief, view projection, pluggable generator command

## Phase 1.6 — Open standards & capture (done)

- ✅ AI image-to-3D: free via Hugging Face Spaces (built-in Gradio client: Hunyuan3D 2.1 / 2 / Turbo,
  TRELLIS.2, Stable Fast 3D), or paid via fal.ai / Meshy; subject framing; auto voxel remesh on import
- ✅ Voxel remesh: watertight, even surface; merges parts; fixes scans
- ✅ OpenUSD: USDA/USDZ export (UsdSkel blend shapes, UsdPreviewSurface, splats as
  `ParticleField3DGaussianSplat`), USDA/USDZ import; validated with OpenUSD 26.08 `usdchecker`
- ✅ Gaussian splats: PLY (INRIA layout, SH up to degree 3), `.splat`, glTF `KHR_gaussian_splatting`,
  USD ParticleField; sorted EWA splat rendering; splats → watertight mesh; sculpt → splats
- ✅ PLY mesh / point cloud import
- ✅ Unreal Engine: binary FBX 7.4 export (LOD groups, blend shapes), FBX import (ufbx), Unreal preset,
  Send to Unreal over Python remote execution (same machine or LAN)
- ✅ Studio pipelines: VFX Reference Platform CY2026/CY2027 builds, Python 3.13 API, Rez package
- ✅ MCP server (`sculptforge-cli --mcp`): every command as a tool for Claude Code / Claude Desktop / any MCP client

## Phase 2 — Detail & workflow (v0.2–0.3)

- ✅ Masking: Ctrl paints / Ctrl+Alt erases the mask, invert, clear, blur, sharpen, grow, shrink,
  mask by cavity and by face group; every brush and vertex paint respect the mask
- ✅ Face Groups (auto by normals, by connectivity, by UVs, from mask, merge visible) and classic visibility:
  Ctrl+Shift isolate / hide a group, rectangle hide, show all, invert, remove hidden; Ctrl / Ctrl+Shift switch
  the brush to the mask / selection brushes
- ✅ Parts: separate meshes in one scene, each with its own undo history; append (primitives or files),
  duplicate, delete, rename, show/hide, solo, reorder, merge down / merge visible, split by face groups or pieces,
  Alt+click to select. Saved in projects (format 2) and exported as separate objects to OBJ, glTF, FBX (an LOD
  group per Part), USD and Bridge; multi-object files open as Parts
- ✅ UI tests: `.sfui` scripts drive the real app (clicks, typing, strokes) with screenshots, in CI
- ✅ Multiresolution: subdivision levels you can step up/down while keeping detail (v0.5)
- ✅ Dynamic topology (local remeshing under the brush, dynamic topology style) (v0.8)
- ✅ Alphas (round, square, noise or any grayscale image) and stroke types (dots, drag-rect, spray, lazy mouse)
- ✅ Lasso Mask / Rectangle Mask and Select Lasso (v0.4); 📋 stencils, MaskCurve
- ✅ More brushes: Clay Block, Sharp Crease, Tendril, Trim (dynamic), Layer, Nudge
- ✅ Lit Sphere materials (seven built in, with a high-contrast Sculpt default since v0.4), with cavity shading on top
- 📋 Ambient occlusion shading
- ✅ Project file format (`.sfproj`: mesh, layers, mask, vertex paint, face groups, visibility, UVs, splats, brushes,
  view settings), quit-with-unsaved-changes prompt, recent files, autosave with restore
- ✅ Bridge: Bridge files in and out with face groups, mask, vertex paint and UVs, plus Send / Receive on
  Windows and macOS. The format follows the GoB add-on
- 📋 Multithreaded brush evaluation, partial GPU uploads per BVH node
- 📋 Benchmarks: brush latency at 1M / 5M / 20M triangles in CI

## Done in v0.4 — classic feel

Shaped by feedback from a working professional modeler: classic navigation (free rotation, Shift snap, Alt-release
zoom, right-click navigation), lasso / rectangle masking and hiding, a Sculpt default material, quads kept
through import, editing and export, Photoshop-style paint layers, and an `install/` folder with
always-latest downloads.

## Done in v0.5 — the sculpting workflow core

Ranked by what sculptors cite most on sculpting forums:

- ✅ Multiresolution subdivision levels (step up and down, keep detail, reconstruct)
- ✅ quad remesh-class quad retopology (Quad Remesh: target count, Size Variation, Follow Face Groups, vertex paint density,
  symmetry) plus Project Detail detail transfer (v0.5); ✅ curve guides (Quad Remesh Guides, v0.8)
- ✅ Automatic UV unwrapping (Auto UV, xatlas) and texture painting on UV maps (v0.4.2)
- ✅ Normal / displacement / AO / cavity / colour map baking with MikkTSpace tangents and a viewport preview (v0.5)
- ✅ Performance at 20-100M polygons, multithreaded brushes, fast undo (benchmark, partial GPU uploads, Fast
  Navigation; v0.5)
- ✅ Gizmo for moving, scaling, rotating and posing parts (W / E / R, masked falloff, all Parts together) (v0.5)

## Done in v0.6 — performance

- ✅ About half the memory per triangle: subdivision levels generated instead of stored, implicit vertex
  adjacency on divided levels, project open read in place (33M: 156 -> 83 bytes per triangle); Divide limited
  by memory instead of 50M
- ✅ Viewport: clusters culled on the CPU, 12-byte vertices, a motion proxy under Fast Navigation (33M orbit
  20.6 -> 9.2 ms with 4x AA, 3.7 ms with Fast Navigation; GPU 46 -> 15 bytes per triangle)
- ✅ Brushes: bitmap vertex sets, refits from the query, parallel undo recording (Mask / Paint about 2x)
- ✅ Update notifications (once a day, nothing sent about the user, off for package managers)

## Done in v0.7 — classic feel and Blender

- ✅ the Classic sculpting keys and gestures; the 0.6 keys as a preference
- ✅ Blender bridge: the SculptForge Bridge add-on for Blender 4.2+, objects update in place with transforms,
  UVs, colours, mask and face sets
- 📋 Next: mmap project open, an high-detail geometry equivalent

## Done in v0.8 — dynamic topology, VDM, guides

- ✅ Dynamic topology: the Dynamic toggle and Detail, mirrored remeshing with symmetry, small undo
  steps, under 15 ms per dab at 1M triangles
- ✅ Quad Remesh Guides: draw the edge loops Quad Remesh follows
- ✅ Sculpt layers on every subdivision level (record at the top, step, divide, delete levels)
- ✅ Vector displacement brushes: the VDM brush (Drag Stamp / Slide Stamp), built-in Horn / Ear Curl / Scales /
  Nostril, Create VDM from a VDM plane, Blender-compatible OpenEXR in and out
- ✅ Blender add-on: the Classic sculpting keys in Sculpt Mode (a preference)

## In development for v0.9 — maquette to performance

- ✅ Facial shapes: the 52 ARKit blend shapes generated on any sculpted humanoid head (template fit + deformation
  transfer, eyes / teeth / tongue Parts, symmetry, subdivision levels), Face test, export with ARKit names to
  GLB / USD / FBX / Blender shape keys
- ✅ Live face: `face_weights` (partial updates, no undo), takes played and baked to glTF animation / USD time samples
- 📋 Next for faces: FBX blend-shape animation, take animation over the Blender bridge, an image-based landmark
  detector, corrective combination shapes, eye rotations as bones for rigs that want them
- ✅ Splat training from a phone video or photos with a live viewport preview (Brush, downloaded on request;
  COLMAP camera positions, or the phone's AR poses; sharpest-frame selection; Good enough - stop / Cancel)
- 📋 Splat training: background masking of the capture, resume a stopped training, a Brush build for Linux ARM
- ✅ File > Capture from phone: QR pairing (or a 6-digit code), LAN only, only while the window is open
- ✅ Capture page on the phone (no app): scan video or photo burst with coaching, one photo to 3D, splat .ply from
  Scaniverse / Polycam / Luma; resumable chunked uploads into the Captures inbox with capture.json
- ✅ View in AR on the desk: USDZ (AR Quick Look) and GLB (Scene Viewer), decimated, colour baked, re-exported
  after strokes
- ✅ Live face feed: MediaPipe on the phone, 52 ARKit weights + head rotation over a WebRTC data channel
  (libdatachannel, optional at build time), face takes as JSON Lines
- 📋 Play back and export face takes (with the facs feature); in-page video recording with live coaching where the
  page is secure; a QR code back from the phone for networks without internet
- ✅ Face page at https://sculptforge.app/link/ (one constant: kPhoneLinkSiteUrl)
- 📋 Deploy the link relay Worker (`services/link-worker`, link.sculptforge.app); then consider dropping ntfy.sh

## Phase 3 — AI assistant, beyond brushes

- 📋 Chat assistant that drives any SculptForge command (tool calling over the existing registry)
- 📋 OS keychain storage for API keys
- 📋 Preview/confirm for destructive AI actions
- 📋 Viewport screenshot as context (proportion/anatomy feedback)
- 📋 Text-to-3D through the generator interface
- 📋 Multi-view image-to-3D (front/side/back) and image-conditioned retopology
- 📋 Viewport snapshot tool for MCP agents (let the agent see what it sculpted)
- 🚧 Normal/displacement map baking (high → low poly) ✅ (v0.5), auto-retopology + UVs
- 📋 Live link to Unity / Godot / Blender (Unreal done); Unreal: send textures (baked normal maps done, v0.5), Nanite option

## Phase 4 — Advanced modeling (v0.5+)

- 📋 Boolean merge/subtract on the voxel pipeline; sharper voxel remesh (signed distance instead of occupancy)
- ✅ Quads: imported quads kept, shown and exported as quads, quad subdivision and quad voxel remesh (v0.4);
  quad remesh-class retopology from triangle meshes (Quad Remesh, v0.5)
- ✅ Vertex Paint: Paint brush, RGB while sculpting, colour pick, fill (respecting the mask), cavity paint;
  exported to PLY, glTF, OBJ, USD and Bridge; Photoshop-style paint layers with blend modes (v0.4)
- ✅ Texture painting on UV maps (resolution independent of the mesh), with the same paint layers; Auto UV,
  vertex paint <-> texture, PNG next to OBJ / glTF / USD / FBX / Bridge exports (v0.4.2)
- ✅ Textures survive Auto UV, voxel remesh, Quad Remesh, Decimate and merges (Keep Texture, v0.5); textured
  exports validated with the Khronos glTF validator, OpenUSD and ufbx (v0.5)
- ✅ One-click **New Texture** while painting (Auto UV first), parallel texture dabs: 4-5x faster at 4K (0.8.1)
- 📋 UV editing (seams, relax, pack options), UDIM tiles; ✅ texture baking (normal/displacement, v0.5)
- ✅ STL import (binary and ASCII) and binary export
- 📋 Binary USD (usdc), SPZ-compressed splats, USD references/payloads
- ✅ Make sculptable: splat capture → watertight surface → face-aware quads (eye, mouth and nasolabial loops) with
  the capture's colours, crop box, Game / Film / Sculpt presets (v0.9)
- 📋 Make sculptable: a learned face landmark model (bundled, permissive licence) for hard faces; ears and hands
- 📋 Splat editing: paint-select, sculpt deforming nearby splats

## Phase 5 — Ecosystem (v1.0)

- 📋 OpenColorIO viewport, Alembic export, USD layers/variants/references and `.usdc`
- 📋 Plugin API on top of the Python module
- 📋 Installers: AppImage/Flatpak, signed macOS .dmg, Windows MSI
- 📋 Documentation site and tutorials
