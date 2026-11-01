# SculptForge project files (.sfproj)

A `.sfproj` is an uncompressed ("stored") ZIP (src/io/Zip.cpp) written by `saveProject` /
read by `loadProject` (src/core/Project.cpp). Commands: `save_project path=...`,
`open_project path=...`; `open` also recognises `.sfproj`.

## Layout (version 5)
- `manifest.json`: always first. `format` = `"SculptForge project"`, `version` (integer, currently 5),
  `app_version`, then `subtools` (one entry per Part, in list order), `active_subtool` (index),
  `brushes` (recipe JSON), `active_brush` (name), optional `splats`, and app-owned keys (`camera`,
  `render`, `brush`, `symmetry`, `gizmo`). `gizmo` (added in 0.5 without a version bump; older readers
  ignore it) holds the Gizmo's `pivot`, `pivot_placed`, `orient_surface`, `axis_x` / `axis_y` (its
  surface axes), `use_mask`, `all_subtools` and `falloff`.
- Each `subtools[i]` holds `name`, `visible` (bool), `mesh`, `layers`, `active_layer` (sculpt layer
  strokes go into, -1 = base) and, for a mesh that came from ZBrush, `goz_name` (the GoZ object name
  that `goz_send` / `goz_receive` match on). Optional `shading` (`"smooth"` or `"flat"`; missing or unknown =
  follow the scene) is the Part's own viewport shading, and the app's `render.flat_shading` (bool) the scene's;
  both were added after 0.9.4 without a version bump (older readers ignore them).
- Optional top-level `attachments` (added in 0.9.5 without a version bump; older readers ignore it): files the app
  keeps in the project, each `{"name", "data"}` with `data` a `u8` blob under `attachments/<name>`. Names use
  letters, digits, space, `. _ -` and `/` (no `..`); a missing or unreadable attachment is left out on loading. The
  app stores the custom Lit Sphere the view uses as `matcaps/<name>.png` and names it in `render.matcap_file`
  (`render.matcap` keeps its name).
- Blobs are little-endian arrays. The manifest describes each one as
  `{"file", "type": "f32"|"u32"|"u8", "components", "count"}`; readers check all four. Part `i`'s
  blobs live under `subtools/<i>/` (the file name comes from the descriptor, so readers don't rely
  on it).

| Manifest key (in `subtools[i]`) | File | Layout |
|--------------|------|--------|
| `mesh.positions` | parts/i/mesh/positions.bin | f32 x3 per vertex (current blend) |
| `mesh.triangles` | parts/i/mesh/triangles.bin | u32 x3 per triangle |
| `mesh.base` | parts/i/mesh/base.bin | f32 x3 per vertex (only with layers) |
| `mesh.mask` / `colors` | parts/i/mesh/mask.bin, colors.bin | f32 x1 / x3 per vertex (linear RGB) |
| `mesh.polygroups` / `hidden` | parts/i/mesh/face groups.bin, hidden.bin | u32 / u8 per triangle |
| `mesh.uvs` | parts/i/mesh/uvs.bin | f32 x2 per triangle corner |
| `mesh.quad_partners` | parts/i/mesh/quad_partners.bin | u32 per triangle: the triangle it forms a quad with, 0xFFFFFFFF = none. Readers drop pairs that aren't mutual or don't share an edge. Added without a version bump: older readers ignore it and load triangles |
| `layers[k].delta` | parts/i/layers/<k>.bin | f32 x3 per vertex, plus `name`, `weight` |
| `paint_layers.base` | parts/i/paint/base.bin | f32 x3 per vertex: the Base paint layer (linear RGB) |
| `paint_layers.layers[k].color` / `alpha` | parts/i/paint/<k>_color.bin, <k>_alpha.bin | f32 x3 / x1 per vertex, plus `name`, `opacity` (0-1), `blend`, `visible`, `locked` |
| `texture.base` | parts/i/texture/base.png | u8 blob with `"encoding": "png"`: the texture (or its Base layer), RGBA, sRGB, top row = v 1 |
| `texture.layers[k].rgba` | parts/i/texture/<k>.png | PNG as above: the layer's colour, coverage in alpha, plus `name`, `opacity`, `blend`, `visible`, `locked` |

| Manifest key (top level) | File | Layout |
|--------------|------|--------|
| `splats.*` | splats/*.bin | positions x3, rotations x4 (x,y,z,w), scales x3, opacities x1, sh x3 per coefficient; plus `count`, `sh_degree`, `name`, `transform` (16 floats, column-major) |

Optional attributes are simply absent when the mesh doesn't have them.

### Selection sets
`subtools[i].selection_sets` (only for SubTools with sets; src/core/SelectionSets.h; added after 0.10.4 without a
format version bump, older readers ignore it) holds `version` (1), `morph_target` (the set the Morph brush uses, if
any) and `sets`: per set its `name`, `channels`, `level`, `vertex_count`, `triangle_count` (for people and tools;
readers rely on the data) and `data`, a `u8` blob under `subtools/i/sets/<k>.bin`. The blob is self-contained,
little-endian: `SFSS`, the set format version (1), the channel bits (1 mask, 2 polygroups, 4 vertex paint,
8 visibility, 16 UVs, 32 shape), the vertex and triangle counts and level it was stored at, the name (u32 length +
UTF-8), then one section per channel (u32 channel bit, u64 byte length, payload): runs (u64 count, then u32 start,
u32 length each) of masked vertices plus a u32 "all ones" flag and otherwise f32 values; run-length polygroups (u32
id, u32 length); runs of non-white vertices plus f32 x3 colours; runs of hidden triangles; f32 x2 UVs per triangle
corner; f32 x3 positions. Readers skip sections they don't know and leave out (with a notice) any set they can't
read; a set whose format version is newer than theirs is left out the same way. `.sfsets` files
(`selection_sets_export`) are stored ZIPs of a `manifest.json` (`format` = `"SculptForge selection sets"`,
`version` 1, `morph_target`, `sets` as above with `data` naming the entry) and the same blobs under `sets/`.

### Paint layers
`subtools[i].paint_layers` (only written when the Part has paint layers; src/core/PaintLayer.h) holds
`active` (the layer painting writes into, -1 = Base), `base` and `layers` (bottom to top). `blend` is one of
`normal`, `multiply`, `screen`, `overlay`, `soft_light`, `add`, `subtract`, `darken`, `lighten`, `difference`
(unknown modes load as `normal`). `mesh.colors` still holds the composited stack, so readers that don't know
`paint_layers` (SculptForge 0.3 and earlier) open the project with the paint flattened, and the key needs no
version bump.

### Texture (version 3)
`subtools[i].texture` (only for Parts with a painted texture; src/core/Texture.h) holds `width`, `height`,
`paint_to_texture` (painting goes into the texture rather than the vertices), `active` (texture layer
painting writes into, -1 = Base), `base` and `layers` (bottom to top, the same fields and blend modes as
paint layers). Each image is a PNG of exactly `width` x `height`; a texture whose images are missing or the
wrong size refuses the whole file. The composite is not stored: it is rebuilt from the layers on load.
Version 3 was introduced for this key (SculptForge 0.5): earlier versions refuse the file instead of
silently dropping the paint. Version 1 and 2 files load unchanged.

### Subdivision levels (version 4)
`subtools[i].subdiv` (only for Parts with levels; src/core/Multires.h) holds `current` (the level the
Part's own `mesh` is) and `levels` (base first). Each level has `mesh` / `layers` / `paint_layers` as above
under `subtools/i/subdiv/<k>/` (or `same_as_mesh: true` for the current level when it equals the Part's
mesh), `stale` (positions to be re-evaluated from the level below), `detail` (f32 x3 per vertex: displacement
in the local frame of the subdivided level below), `layer_detail` (one per sculpt layer: the layer's offsets
minus the subdivided offsets of the level below, in the same local frames as `detail`; version 4 stored them in
world space, and readers convert those when loading a version 4 file, evaluating stale levels on the way) and, from
level 1 up, `topology`: `catmull_clark`, `smooth`, `pairing` (u32 per coarse triangle: the quad pairs the level
was divided with) and `vertex_child` / `edge_child` / `face_child` (u32: the fine vertex of each coarse vertex,
edge, in edge order, and polygon). Edges and polygons are rebuilt from the coarse level, so the maps must match;
levels that don't fit refuse the file. Readers that don't know `subdiv` load the current level. Version 5 (the
local-frame `layer_detail`) was introduced so a sculpt layer's detail turns with a rotated or reshaped low level;
version 4 readers would misread it, so they refuse version 5 files.

Loading replaces the scene: every Part comes back with its name, visibility, attributes, layers,
active layer and GoZ name, the saved active Part is selected, every Part starts with an empty
undo history and the document is clean. A file is refused (and the open scene left alone) when the
Parts list is missing or empty, or any Part's mesh is broken; an out-of-range `active_subtool`
selects the first one.

## Version 1
Version 1 files (SculptForge 0.2 and earlier) have one mesh at the top level instead of `subtools`:
`mesh` and `layers` as above (blobs under `mesh/` and `layers/`) and `active_layer`. They still load,
as a single Part called "Sculpt". Saving always writes the current version (5).

## Compatibility rules
- Unknown keys and files are ignored. Unknown **top-level** keys are kept in
  `Document::projectSettings` and written back, so older builds don't drop newer settings.
- A file whose `version` is greater than `kProjectVersion` is refused with a message naming the app
  version that wrote it. Bump `kProjectVersion` only for changes old readers would misread;
  adding a new optional blob does not need a bump.
- New mesh attributes: add a blob in `writeMesh` and read it in `readMesh` (size-checked); both
  are used for every Part.

## App behaviour (src/app/PanelProject.cpp)
- `Document::dirty()` = any undo push/undo/redo (in any Part), splat edit or `modified` since
  `markClean()` (save, open, new). The window title shows `*` while dirty.
- New / Open / Open Recent / drop / Quit / window close ask Save / Don't Save / Cancel when dirty
  (never in `--render`, `--record` or demos).
- The pref dir (`SDL_GetPrefPath`) holds `project.json` (10 recent files, autosave interval),
  `autosave.sfproj` (every N minutes while dirty, default 5, removed once the work is saved) and
  `clean_exit` (touched on normal exit). An autosave newer than `clean_exit` is offered for restore.
