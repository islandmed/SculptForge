# SculptForge PBR texturing plan (2026-10-31)

Feature set comparable to a Substance-class painter, implemented as SculptForge's own work. Use our own names
("Material Recipes", "Mask Recipes", "Mesh Maps", "Fill layer", "Edge Wear"). Never use Adobe names, icons,
presets or file formats (.sbsar/.spp). Built-in HDRIs and recipes are generated in code, or CC0 files listed in
THIRD_PARTY_NOTICES.md.

## Already in place
- One sRGB RGBA8 texture per SubTool: `src/core/Texture.h/.cpp` (`PaintTexture`, `TextureLayer`, `TexturePainter`), 64 px undo tiles, 4 px gutter, dirty-tile uploads to the GPU, parallel dabs, symmetry and mask.
- Ten Photoshop blend modes (`PaintLayer.h`).
- CPU baker (`Bake.h/.cpp`): normal, object normal, displacement, AO, cavity and colour, with front/rear distances.
- Auto UV via xatlas, MikkTSpace tangents, 16-bit PNG and EXR IO.
- Textures carry through remesh (`TextureTransfer`).
- Exports carry only base colour; glTF and USD hard-code metallic 0 / roughness 0.65; Unreal gets the normal map.
- Renderer: GL 3.3, shader variant bits `kPaint=1 kPrim=2 kTextured=4 kNormalMap=8 kFlat=16`, MatCap/studio lighting only, no IBL or tone mapping.
- Project format version 6 (`.sfproj` ZIP + JSON).

## Decisions for every phase
1. The CPU core owns layer evaluation, tile-parallel. The GPU only displays the result. Every feature is a registered command (CLI/MCP).
2. `TileImage`: sparse copy-on-write 64x64 tiles (null, uniform or dense), 1-4 components, 8 or 16 bit.
3. Channels: `enum Channel {BaseColor, Roughness, Metallic, Normal, Height, AO, Emissive, Opacity}` plus a format table. Only enabled channels get composites.
4. Extend `PaintTexture`/`TextureLayer` rather than fork them. Base colour stays at `pixels`; the composites `orm`, `normal`, `height16` and `emissive` sit beside it.
5. Save format 7, written only when the content needs it. A project with no PBR features still writes 6. New data goes under new keys.

## Phase 0: scaffolding (2-3 days)
Stub files (`TileImage`, `TextureChannels`, `MapExport`, `MeshMaps`, `LayerStack`, `Procedural`, `Generators`, `TextureFilters`, `MaterialRecipe`, `TextureProjection`, `EnvironmentMath`), the `register*Commands` and UI-action hooks, the `Channel` enum, the `kProjectVersion=7` minimum-version writer, and `LayerStackIO` hooks.

## Phase 1 (0.10)
**1A: multi-channel stack**
- `TileImage` and `TextureChannels`: per-channel blend, `NormalCombine` (RNM), height-to-normal.
- `TextureLayer` gains `std::array<LayerChannel>`.
- The dab takes a `BrushMaterial`.
- Undo is keyed by (layer, channel).
- `TextureTransfer` handles every channel.
- Commands: `texture_channels`, `paint_material`, `channel_view`.
- Target: a 5-channel 4K dab costs at most 2.5x today's dab.

**1B: PBR viewport** (waits for feat/zero-latency, since both change `Renderer.cpp`)
- `EnvironmentMath`: SH9, CPU BRDF lookup table, procedural environments, an `SCULPTFORGE_HDRI_PATH` library.
- `render/Environment`: GGX prefilter cubemap through FBOs.
- `kPbr=32` shader bit, Cook-Torrance with split-sum, tone maps (ACES, AgX, Reinhard).

**1C: export presets** (`MapExport`)
- JSON presets for Unreal (BC, N in DirectX convention, ORM), Unity URP/HDRP mask maps, glTF/Godot ORM, Blender and USD.
- PBR material slots in glTF, USD, OBJ and FBX.
- The Unreal bridge imports `T_*_BC/N/ORM` and builds `M_<name>`.
- Command: `export_maps`.

**1D: UI**
- Per-channel toggles on each layer row.
- Material brush swatches; C picks every channel.
- Solo-channel view.

## Phase 2 (0.11)
- **2A mesh maps:**
  - New bakes: curvature, position, thickness, ID, world normal, bent normal.
  - Sources: the mesh itself, **the SubTool's own top subdivision level** (a differentiator), name match (`_high`/`_low`), or a cage.
  - Maps are fingerprinted so stale ones are detected.
- **2B `LayerStack`:**
  - Layer kinds: Paint, Fill, Folder, Filter.
  - Mask effects: paint, fill, generator, filter, geometry, levels, invert.
  - Tile-cached evaluation; previews run at 1/4 resolution during slider drags.
- **2C procedurals and generators:**
  - Noise in object space via the position map, so it is seamless across UV seams.
  - Generators: EdgeWear, Dirt, Gradient, Thickness, LightDirection.
- **2D filters:** island-aware blur, sharpen, HSL, levels, warp.
- **2E:** layer tree UI.

## Phase 3 (0.12)
- Material and Mask Recipes (`.sfmaterial` ZIP JSON); 15-25 original procedural recipes in a LightBox "Surfaces" tab.
- Projection fills: triplanar, planar, spherical, cylindrical.
- Screen stencils / clone painting.
- ID-colour mask, geometry mask, anchor points.
- Radial symmetry.

## Phase 4 (1.0)
- UDIM (most invasive; `TextureSet` map of tiles, `GL_TEXTURE_2D_ARRAY`).
- Particle brushes.
- CPU path tracer (GGX, env importance sampling, denoise).
- Dynamic path-stamping strokes.

## Risks
- Memory at 8K with many channels.
- Stale mesh maps after sculpting.
- UV seams in filters.
- Growth in shader variants.
- Merge conflicts: each file has one owner per phase.
- IP hygiene.
