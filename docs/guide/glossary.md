# Glossary

This chapter maps ZBrush terms to their SculptForge equivalents, so you can find a familiar tool quickly, and
explains the terms that only SculptForge uses. Features use SculptForge's own names (Parts, Forge Spheres,
Vertex Paint, Library...); the short list is in
[Coming from ZBrush](coming-from-zbrush.md#zbrush-terms-in-sculptforge), and the command palette (<kbd>F3</kbd>)
finds every feature by its ZBrush name too. "Not yet" means SculptForge doesn't have it today; see the roadmap
for what is planned.

## ZBrush terms in SculptForge

| ZBrush | SculptForge |
|---|---|
| Alpha | **Alpha**: built-in round, square and noise, or any grayscale image. Left shelf, **Sculpt > Alpha**. See [Sculpting](sculpting.md) |
| Auto Groups | **Tool > Face Groups > By pieces** (`polygroup_by_connectivity`) |
| BPR render | Not yet as a renderer. Headless renders and turntables with `sculptforge --render` ([Scripting](scripting.md)) |
| Brush pop-up (B) | **Brush picker**, <kbd>B</kbd>, then type letters (B S T = Standard) |
| Clip brushes | Not yet. The **Trim** brush shaves to a plane |
| Curve brushes, IMM, ZModeler | Not yet |
| Decimation Master | **Tool > Geometry > Decimate** (`decimate`). See [Geometry](geometry.md#decimate) |
| Del Hidden | **Tool > Visibility > Delete hidden** (`delete_hidden`) |
| Del Lower, Del Higher | **Tool > Geometry > Del Lower / Del Higher** (`subdiv_delete_lower`, `subdiv_delete_higher`). See [Subdivision levels](subdivision-levels.md) |
| Divide (Ctrl+D), Smt | **Tool > Geometry > Divide**, <kbd>Ctrl</kbd>+<kbd>D</kbd>, with **Smt** beside it (`subdiv_divide`). See [Subdivision levels](subdivision-levels.md) |
| Document, 2.5D canvas, pixols | Not planned: SculptForge is a 3D sculpting app only |
| Draw Size | **Draw Size** (top shelf, <kbd>S</kbd>, <kbd>[</kbd> / <kbd>]</kbd>) |
| DynaMesh | **Voxel Remesh** (**Tool > Remesh**, `remesh`). See [Geometry](geometry.md#voxel-remesh) |
| Extract (Tool > SubTool > Extract) | **Tool > Parts > Extract**: the masked part becomes a shell Part, with **Thick**, **E Smt** and **Double**; **Extract** previews, **Accept** keeps it (`extract`, `extract_accept`). See [Parts](parts.md#extract-a-shell) |
| Focal Shift | No slider for the built-in brushes. Custom brushes have **Hardness** and **Tip sharpness** in their recipe |
| Ghost, Transp | Not yet. Inactive Parts are drawn slightly dimmed |
| GoZ | **Send to ZBrush** (GoZ-compatible): send to and receive from ZBrush, and import or export GoZ-compatible files (**File > Send to ZBrush**). See [Exporting](exporting.md#exchange-models-with-zbrush-send-to-zbrush) |
| Group Masked | **Tool > Face Groups > From mask** (`polygroup_from_mask`) |
| Group Visible | **Tool > Face Groups > Merge visible** (`polygroup_merge_visible`) |
| Groups By Normals | **Tool > Face Groups > By normals** with an angle (`polygroup_auto_by_normals`) |
| Bake, Multi Map Exporter (normal / displacement / AO maps) | **Tool > Bake** (`bake`): tangent and object-space normal, displacement, AO, cavity and colour maps from a high-poly Part onto a low-poly's UVs. See [Baking](baking.md) |
| Layers (sculpt layers) | **Tool > Layers**: sculpt layers with weights, exported as blend shapes. See [Layers](layers.md) |
| LazyMouse | **Steady Stroke** (<kbd>L</kbd>, every stroke type) and the **Lazy Stroke** stroke type |
| LightBox | **Library**, <kbd>,</kbd> (comma) |
| Live Boolean | Not yet |
| Local symmetry, radial symmetry | **Sculpt > Symmetry & Gizmo > Symmetry**: **R** (radial symmetry, Radial Count 2-64 around X, Y or Z, combined with the X / Y / Z mirrors) and **Local** (about the active Part's centre). See [Sculpting](sculpting.md#radial-symmetry) |
| Mask by Cavity | **Tool > Masking > By cavity** (`mask_by_cavity`) |
| Masking (Ctrl) | Hold <kbd>Ctrl</kbd> to paint the mask, <kbd>Ctrl</kbd>+<kbd>Alt</kbd> to erase. See [Masking](masking.md) |
| MaskLasso, MaskRect | **Tool > Masking**: pick the shape, then <kbd>Ctrl</kbd>+drag (add <kbd>Alt</kbd> to unmask). See [Masking](masking.md) |
| MatCap | **Material**: the built-in Lit Spheres, Soft Clay the default, plus Studio (<kbd>M</kbd>), and your own with **Load Lit Sphere...** (`load_matcap`). See [Vertex Paint](vertex-paint.md) |
| Mirror (Tool > Deformation) | **Tool > Deformation > Mirror** with X Y Z (`mirror`): flips the Part across the plane through the origin. See [Geometry](geometry.md#mirror) |
| Mirror And Weld | **Tool > Geometry > Modify Topology > Mirror And Weld** with X Y Z (`mirror_and_weld`): the + side mirrored onto the - side and welded into one watertight mesh, keeping face groups and vertex paint. See [Geometry](geometry.md#mirror-and-weld) |
| Morph target | A [sculpt layer](layers.md); exported as glTF morph targets and FBX/USD blend shapes |
| Mrgb, M (material channel) | Not yet: there is no per-vertex material channel |
| Polyframe (PolyF, Shift+F) | **Wire** on the right shelf, or <kbd>Shift</kbd>+<kbd>F</kbd> |
| Polygroups | **Face Groups**: same idea, same Ctrl+Shift gestures. See [Face Groups and visibility](face-groups.md) |
| Polypaint | **Vertex Paint**: Paint brush (<kbd>P</kbd>), Rgb, fills, cavity paint. See [Vertex Paint](vertex-paint.md) |
| Polypaint layers | **Tool > Paint Layers**: Photoshop-style colour layers with blend modes. See [Paint layers](vertex-paint.md#paint-layers) |
| Project All (ProjectAll) | **Tool > Parts > Project > Project All** (`project_all`), with Dist, Mean and PA Blur; respects the mask. See [Quad retopology](retopology.md#project-all) |
| Projection Master | Not yet. **Mesh > Generate 3D > Stamp** projects an image onto the model from the view |
| QuickSave | **File > Save Snapshot** (<kbd>9</kbd> in the ZBrush-style keys); the last 10 are kept in the Library **Snapshots** tab. See [Projects](projects.md#snapshots) |
| Rgb | **Rgb** on the top shelf (paint while sculpting) |
| Rgb Intensity, Z Intensity, Focal Shift | Same keys: **RGB Intensity** (<kbd>I</kbd>), **Intensity** (<kbd>U</kbd>), **Falloff** (<kbd>O</kbd>) in **Sculpt > Draw & Tablet** and the Quick Menu |
| Sculptris Pro, dynamic topology | **Dynamic** on the top shelf, with **Detail** (`dyntopo`). See [Dynamic topology](dynamic-topology.md) |
| Reconstruct Subdiv | **Tool > Geometry > Reconstruct Subdiv** (`subdiv_reconstruct`) |
| SDiv, Lower Res / Higher Res (Shift+D / D) | **Tool > Geometry**: the **SDiv** slider, **Lower Res** <kbd>Shift</kbd>+<kbd>D</kbd>, **Higher Res** <kbd>D</kbd> (`subdiv_level`). See [Subdivision levels](subdivision-levels.md) |
| SelectLasso, SelectRect | **Tool > Visibility**: pick the shape, then <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag (add <kbd>Alt</kbd> to hide). See [Face Groups](face-groups.md) |
| Solo | **Solo** on the right shelf or in **Tool > Parts** |
| Spotlight | Not yet |
| Edit > Preferences > Performance, Edit > Preferences > Mem | **Edit > Preferences > Performance**: Antialiasing, Fast Navigation, Multithreading, the last stroke's cost and memory. See [Performance](performance.md) |
| Store Config | **Store Config**, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> (**Edit > Preferences > Config**) |
| Stroke (Dots, DragRect, DragDot, Spray) | **Stroke** types Dots, Drag Stamp, Slide Stamp, Spray, Lazy Stroke (<kbd>L</kbd> toggles Steady Stroke) |
| SubTool | **Part**: same idea, <kbd>Alt</kbd>+click to select. See [Parts](parts.md) |
| Texture Map, New From Polypaint | **Tool > Texture Map**: **New Texture** (1K-8K) and paint straight into it, or **Vertex Paint > Texture**. See [Texture painting](texture-painting.md) |
| Texture palette | Not yet as a palette of texture images. **Tool > Texture Map > Import Texture** loads an image onto the UVs |
| Transpose, Gizmo 3D (Move W, Scale E, Rotate R) | **Gizmo**: same keys, arrows, rings and boxes; masked parts stay, Falloff softens the joint. See [Gizmo](gizmo.md) |
| Transpose All | **All Parts** in the Gizmo panel. See [Gizmo](gizmo.md#all-parts) |
| UV Master (Unwrap), UV editing | The **UV Editor** (<kbd>Alt</kbd>+<kbd>U</kbd>): seams on the model, Least Stretch, Conformal, By Face Groups, Planar and Unfold unwraps, UDIM packing, texel density, trim sheets (`uv_unwrap`, `uv_pack`...). **Tool > Texture Map > Auto UV** (`uv_auto`) is the one-click unwrap. See [UV Editor](uv-editor.md) |
| VDM brush, 3D alpha, Create VDM brush from the VDM plane | The **VDM** brush and **Sculpt > Brush > Vector Displacement**: built-in maps, **Load VDM...** (EXR), **New VDM Plane**, **Create VDM**, **Export EXR...**. See [Vector displacement brushes](sculpting.md#vector-displacement-brushes) |
| Zadd / Zsub | **Add** / **Sub** on the top shelf; <kbd>Alt</kbd> flips for one stroke |
| ZPlugin | No in-app plugin system yet. Automate with the Python module, `sculptforge-cli` and MCP ([Scripting](scripting.md)) |
| ZRemesher | **Tool > Geometry > Quad Remesh** (`qremesh`): Target Polygons, Half / Same / Double, Adaptive Size, Keep Groups, Use Vertex Paint, symmetry. No curve guides yet. See [Quad retopology](retopology.md) |
| ZScript | Script commands: the console (backtick key), script files, `sculptforge-cli`. See [Scripting](scripting.md) |
| ZSpheres, Adaptive Skin, Preview (A) | **Tool > Forge Spheres**: Draw, Move, Scale, Alt+click deletes, **Preview** (<kbd>A</kbd>), **Make Mesh** (`zsphere_make_mesh`). See [Forge Spheres](forge-spheres.md) |
| ZTL, ZPR files | Not read directly. Convert ZTools with the SculptForge ZTL Export plug-in for ZBrush (then open the `.ZTL`), or use Send to ZBrush, OBJ, FBX or GLB; save work as `.sfproj` |

## SculptForge terms

| Term | Meaning |
|---|---|
| Brush Lab | The AI brush creator: describe a brush in words and it builds a recipe you can tweak. Opens from **AI Brush**. See [Brush Lab](brush-lab.md) |
| Brush code | One line of text (`sfbrush1:...`) that holds a whole brush, for sharing in chat or a forum. Paste with <kbd>Ctrl</kbd>+<kbd>V</kbd> |
| Brush library | Your saved custom brushes, loaded at every start |
| Brush recipe | The data that defines a custom brush (direction, depth, plane, pinch, pattern and so on): values, never code |
| `.sfbrush` | A file holding one brush recipe |
| `.sfproj` | A SculptForge project file: the whole scene. See [Projects](projects.md) |
| Cross field | The directions Quad Remesh wants quad edges to follow at every point of the surface, smoothed over the whole model. Singularities (where three or five quads meet) appear where the field must turn |
| Command palette | <kbd>F3</kbd>: search every command, brush and setting by name and run it |
| Community gallery | Shared CC0 brushes you can install in one click (**Community brushes**) |
| Console | The script console (backtick key): every action appears as a script line |
| Ctrl help | Hold <kbd>Ctrl</kbd> and point at a control to see what it does, its shortcut and its command |
| Gaussian splat | A 3D capture made of soft coloured blobs (from phone scanning apps). Load one as a reference, make it a sculptable mesh, or turn a sculpt into splats. See [Gaussian splats](splats.md) and [From scan to sculptable mesh](scan-to-sculptable.md) |
| Falloff (Gizmo) | How far a gizmo move eases in from the mask's edge, as a share of the Part's size: a soft joint when posing. See [Gizmo](gizmo.md#posing-a-limb) |
| Generate 3D | Turn an image or a description into a sculptable model. See [Image and text to 3D](generate-3d.md) |
| Gizmo | The Move / Scale / Rotate handles (<kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd>) for placing and posing a Part or its unmasked part. See [Gizmo](gizmo.md) |
| LOD chain | A series of lighter copies of a model (LOD0, LOD1...) for game engines. See [Geometry](geometry.md#lod-chains) |
| MCP | Model Context Protocol: `sculptforge-cli --mcp` lets AI agents such as Claude run SculptForge commands |
| Pivot | The point the Gizmo turns and scales about. <kbd>Alt</kbd>+click the model to put it there. See [The pivot](gizmo.md#the-pivot) |
| Subdivide | One-way subdivision without levels (**Tool > Geometry > Subdivide**, `subdivide`); **Divide** keeps the levels |
| Send to Unreal | Export and import a model into a running Unreal Editor in one step (<kbd>Ctrl</kbd>+<kbd>U</kbd>) |
| Shelves, palettes, trays | The interface around the canvas: shelves hold the most-used controls, palettes hold everything, trays hold pinned palettes. See [The interface](interface.md) |
| Studio brush folder | A shared, read-only brush folder for a team (`SCULPTFORGE_BRUSH_PATH`). See [For studios](studios.md) |
| Paint target (Vertex / Texture) | Where painting goes on a Part that has a texture: its vertices (vertex paint) or its texture. Top shelf, next to **Rgb**. See [Texture painting](texture-painting.md#vertex-or-texture) |
| Quad Remesh | SculptForge's automatic quad retopology (ZBrush's ZRemesher), in **Tool > Geometry**. See [Quad retopology](retopology.md) |
| Mirror plane (Quad Remesh) | The plane Quad Remesh's **Symmetry** mirrors across: the model's own middle (the world's centre plane when the model is symmetric about it), found from the model. Like ZBrush's local symmetry. See [Quad retopology](retopology.md#quad-remesh) |
| Padding (gutter) | The few texels around each UV island that texture painting and baking also fill, so seams don't show. See [Texture painting](texture-painting.md) and [Baking](baking.md) |
| Bake | Capture a high-poly's detail as images (maps) on a low-poly's UVs. See [Baking](baking.md) |
| Cage (front / rear distance) | How far in front of and behind the low-poly the bake's rays look for the high-poly. See [Baking](baking.md#how-the-bake-works) |
| Displacement map | A map of how far the high-poly lies from the low-poly along its normal; mid-grey = 0. See [Baking](baking.md#displacement-mid-grey-is-zero) |
| Keep Texture | Remesh option (Voxel Remesh, Quad Remesh): give the new surface Auto UVs and project the painted texture onto it, so the paint survives. See [Texture painting](texture-painting.md#uvs-and-geometry) |
| MikkTSpace | The standard way of computing tangents that game engines share, so tangent-space normal maps look the same everywhere. See [Baking](baking.md) |
| Normal map | A map of surface directions that makes a low-poly shade like the high-poly. Tangent-space maps are relative to the surface (for engines), object-space maps to the model. See [Baking](baking.md) |
| Texel | One pixel of a texture. Texture painting paints texels; their 3D position on the model decides the brush falloff |
| UV island | A piece of the surface flattened into the texture square. Unwrap and Auto UV cut the model into islands along seams. See [UV Editor](uv-editor.md) |
| UDIM tile | One unit square of UV space, numbered 1001 (u and v between 0 and 1), 1002 to its right, ten per row: a Part's islands can spread over several, each its own texture. See [UV Editor](uv-editor.md#packing-and-udim-tiles) |
| Texel density | Texels of the texture per unit of surface length: equal density makes detail look the same size everywhere. See [UV Editor](uv-editor.md#texel-density) |
| Trim sheet | A texture of horizontal strips that repeat left to right; islands are laid into a strip. See [UV Editor](uv-editor.md#trim-sheets) |
| Antialiasing (AA) | Smoothing the 3D view's edges with several samples per pixel (Off, 2x, 4x, 8x in **Edit > Preferences > Performance**). See [Performance](performance.md) |
| BVH | Bounding volume hierarchy: the structure SculptForge uses to find the surface under the cursor and the vertices under a brush. Its update time is shown per dab in **Edit > Preferences > Performance** |
| Dab | One step of a brush along a stroke. Its cost per dab decides whether a stroke keeps up with the display (under 16 ms at 60 Hz). See [Performance](performance.md) |
| Fast Navigation | Drawing scenes of 10 million triangles or more without antialiasing while the view moves, so it keeps up. See [Performance](performance.md) |
| Multithreading | Brushes, normal and BVH updates and saving spread over every processor core; same results either way. See [Performance](performance.md) |

## See also

- [Welcome to SculptForge](welcome.md)
- [The interface](interface.md)
- [Keyboard and mouse reference](shortcuts.md)
