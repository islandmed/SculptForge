# Features

Back to the [README](../README.md).

## Experienced sculptor?

Pick **Classic sculpting keys** at the first start and your hands know the way. Bring models over as OBJ, FBX, glTF or USD, or through the Bridge (face groups, mask and vertex paint come along).

You get:
- Familiar brushes, symmetry, and Alt and Shift modifiers
- Masking and face groups on Ctrl and Ctrl+Shift
- Parts, subdivision levels, dynamic topology, quad remeshing and a Gizmo

## Sculpting: brushes, masks, layers, remesh, paint

- Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch, Sharp Crease, Clay Block, Tendril, Trim, Layer and Nudge brushes; X/Y/Z symmetry; tablet pressure
- Stroke types (dots, drag-rect, spray, lazy mouse) and brush alphas (built-in or any grayscale image)
- **Masking** that every brush respects: paint, invert, blur, sharpen, grow/shrink, by cavity or face group
- **Face Groups and visibility** with Ctrl+Shift isolate, hide and rectangle hide
- **Parts**: body, eyes, teeth and armour as separate meshes, each with its own undo history; append, solo, reorder, duplicate, merge and split
- **Gizmo** (W Move, E Scale, R Rotate): a Part, or only its unmasked area with a soft falloff for posing a limb; Shift snaps
- **Vertex Paint** and six Lit Sphere materials
- **Texture painting** on UV maps: Auto UV (xatlas), 1K-8K textures, paint layers and blend modes; vertex paint and texture convert both ways
- **Voxel remesh**, **Quad Remesh** (target count, Size Variation, Follow Face Groups, symmetry), **Project Detail**, Loop subdivision and quadric decimation
- **Sculpt layers** that work as blend shapes, with live weights and playback
- **Project files** (`.sfproj`) with autosave, recent files and unlimited undo

## Interface: yours to arrange

- Task menus (File, Edit, Sculpt, Paint, Mesh, View, Render, Help) in the Forge Terminal look, or the Classic layout in one click (Preferences > Interface > Look)
- Shelves, palettes, a Library (`,`) of thumbnails, the brush picker (`B`), a command palette (F3) and a script console (`` ` ``)
- A built-in **user guide** (Shift+F1) with search, also [online](https://sculptforge.app/guide/); hold Ctrl over any control and press F1 for its chapter
- More in [docs/ui-design.md](ui-design.md)

## Free AI: Brush Lab and image or text to 3D

**Brush Lab** turns "dry cracked mud" or "overlapping dragon scales" into a brush, then "make it sharper".
- Brushes are recipes (data, never code) with every value a slider, kept in your library
- Share them as `.sfbrush` files or one-line brush codes (right-click > Copy brush code); studios share a folder through `SCULPTFORGE_BRUSH_PATH`
- The [community gallery](https://sculptforge.app/brushes.html) installs brushes in one click (Brush > Community brushes); see [docs/brushes.md](brushes.md)
- Free out of the box (no account or key): an open model (GPT-OSS 20B) hosted by Pollinations.ai, with a local Ollama as the offline fallback
- Or bring your own: Claude, any OpenAI-compatible endpoint, or a private local model ([5-minute setup](local-ai.md))

**Generate 3D** turns a sketch, concept art or a description into a model, remeshed so you can sculpt it at once.
- Free through the official Hugging Face demos of Hunyuan3D 2 / 2.1, TRELLIS.2 and Stable Fast 3D
- Or your own ComfyUI workflow, a paid fal.ai or Meshy key, or any local model; see [AI services & model licences](ai-services.md)
- Offline tools too: silhouette blockout, relief, and stamp-from-view

## Pipelines: Unreal, Blender, USD, splats, ComfyUI

- One-click **LOD chains**; blend shapes survive on every level
- **Send to Unreal** (Ctrl+U) imports or updates the asset in a running Unreal Editor, even on another computer, with no plugin; see [docs/unreal.md](unreal.md)
- **FBX** (LOD groups, blend shapes), **glTF/GLB** (morph targets), **OpenUSD** (UsdSkel blend shapes), OBJ, PLY and STL, checked with the Khronos glTF validator and `usdchecker`
- **Blender bridge** add-on (4.2+, `addons/blender`): Send to SculptForge, sculpt, Send to Blender; objects update in place with transforms, materials and modifiers ([guide](guide/blender.md))
- **Bridge** files keep face groups, mask, vertex paint and UVs; File > Bridge on Windows and macOS sends each visible Part as its own object ([Exporting](guide/exporting.md#exchange-models-through-the-bridge))
- **Gaussian splats**: open phone captures (Polycam, Luma, Postshot, Scaniverse) as reference or convert them to watertight meshes; PLY, `.splat`, `KHR_gaussian_splatting`, OpenUSD
- **ComfyUI**, both ways: generate with any workflow ([docs/comfyui.md](comfyui.md)), or use the [ComfyUI-SculptForge](https://github.com/islandmed/ComfyUI-SculptForge) nodes for remesh, decimate, LODs and export (ComfyUI-Manager: *Install via Git URL*)

## Automation: headless CLI, MCP, Python

- `sculptforge-cli` runs every operation headless, for build farms and pipelines
- A built-in **MCP server** (`sculptforge-cli --mcp`) lets Claude Code, Claude Desktop or any MCP client sculpt, convert and export
- Headless thumbnail and turntable rendering, plus a scripted demo recorder (`--demo ... --record`)
