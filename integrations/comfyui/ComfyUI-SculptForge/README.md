# ComfyUI-SculptForge

[SculptForge](https://github.com/islandmed/SculptForge) nodes for [ComfyUI](https://github.com/comfyanonymous/ComfyUI):
turn the meshes your graphs generate (Hunyuan3D, TRELLIS, or a quick sketch inflate) into **sculpt-ready**,
**game-ready** and **VFX-ready** assets without leaving ComfyUI.

- **Voxel Remesh** (similar to ZBrush's DynaMesh) rebuilds noisy, broken or self-intersecting AI meshes as one clean, watertight
  sculpting surface.
- **Decimate**, **Subdivide** and **LOD Chain** for game budgets.
- **Export** to GLB, FBX (Unreal, Unity, Maya), USDA/USDZ (VFX pipelines, Apple AR), OBJ, STL (3D printing) or PLY.
- **Inflate Image** turns a sketch or silhouette into a 3D base mesh in a second on the CPU, no GPU or model download.
- Works with ComfyUI's own 3D nodes: every SculptForge mesh input also takes the core `MESH` type, and
  *SculptForge to MESH* goes the other way.
- **Open in SculptForge** sends the result to the desktop app to keep sculpting.

The nodes run the free `sculptforge-cli` command-line tool, so they add no Python dependencies to ComfyUI.

## Install

### 1. Install SculptForge

Download it from the [SculptForge releases](https://github.com/islandmed/SculptForge/releases/latest) (Windows,
macOS, Linux) or build it from source. The nodes look for `sculptforge-cli` in this order:

1. the `SCULPTFORGE_CLI` environment variable (the full path of the executable, or the folder it is in),
2. your `PATH`,
3. the usual install folders: `C:\Program Files\SculptForge`, `%LOCALAPPDATA%\Programs\SculptForge`,
   `/Applications/SculptForge.app/Contents/MacOS`, `/usr/bin`, `/usr/local/bin`, `~/.local/bin`, `/opt/sculptforge/bin`.

If it lives anywhere else, set the variable before starting ComfyUI:

```sh
# Linux / macOS
export SCULPTFORGE_CLI=/path/to/sculptforge-cli
python main.py
```

```bat
:: Windows
set SCULPTFORGE_CLI=D:\Tools\SculptForge\sculptforge-cli.exe
python main.py
```

*Open in SculptForge* finds the desktop app next to the CLI, or set `SCULPTFORGE_APP`.

### 2. Install the nodes

**ComfyUI-Manager:** open *Custom Nodes Manager*, choose *Install via Git URL* and paste
`https://github.com/islandmed/ComfyUI-SculptForge`.

**Manually:** clone it into `ComfyUI/custom_nodes`:

```sh
cd ComfyUI/custom_nodes
git clone https://github.com/islandmed/ComfyUI-SculptForge.git
```

Restart ComfyUI; the nodes appear under **SculptForge** in the node library.

The pack is developed in the [SculptForge repository](https://github.com/islandmed/SculptForge) under
`integrations/comfyui/ComfyUI-SculptForge` and mirrored to
[islandmed/ComfyUI-SculptForge](https://github.com/islandmed/ComfyUI-SculptForge); please open issues and pull
requests on the main repository.

## Nodes

| Node | Inputs | Outputs | What it does |
|---|---|---|---|
| **SculptForge Load Mesh** | `path` | `mesh` | Loads OBJ, FBX, GLB/glTF, PLY, STL, USD/USDA/USDZ, ZBrush .GoZ files or a SculptForge `.sfproj`. Relative paths are looked up in ComfyUI's `input` folder (e.g. `3d/rock.glb`). |
| **SculptForge Inflate Image** | `image`, optional `mask`, `thickness`, `resolution`, `subdivisions` | `mesh` | Inflates a sketch or silhouette into a rounded, closed base mesh. Connect *Load Image*'s `MASK` so the transparent background is left out; a plain backdrop works too. Uses the first image of a batch. |
| **SculptForge Image Relief** | `image`, `depth`, `resolution`, `invert` | `mesh` | Turns an image into a relief: light areas rise (logos, coins, emblems, height maps). |
| **SculptForge Voxel Remesh** | `mesh`, `resolution` (32-512) | `mesh` | Rebuilds the mesh as an even, watertight surface (similar to ZBrush's DynaMesh); fixes broken scans and generated models. |
| **SculptForge Decimate** | `mesh`, `ratio`, `triangles` | `mesh` | Reduces the triangle count, keeping the shape. `triangles` > 0 sets an exact target instead of the ratio. |
| **SculptForge Subdivide** | `mesh`, `levels` (1-4) | `mesh` | Loop subdivision; each level multiplies the triangles by 4. |
| **SculptForge LOD Chain** | `mesh`, `levels`, `ratio`, `filename_prefix` | `file_paths` | Saves `<prefix>_00001_LOD0.glb`, `_LOD1.glb`... to the output folder. |
| **SculptForge Export** | `mesh`, `format`, `filename_prefix` | `file_path` | Saves GLB, FBX, USDA, USDZ, OBJ, STL or PLY to the output folder, numbered like ComfyUI's save nodes. |
| **SculptForge Mesh Info** | `mesh` | `info` | Vertex and triangle counts, bounds and Parts, shown on the node. |
| **SculptForge to MESH** | `mesh` | `MESH` | Converts to ComfyUI's core `MESH` (with normals and vertex colours) for *Save 3D Model*, *Preview 3D* and the other core 3D nodes. |
| **Open in SculptForge** | `enabled`, optional `mesh` or `file_path` | | Opens the mesh, or an exported file, in the SculptForge desktop app. Only useful when ComfyUI runs on your own computer. |

Every `mesh` input accepts both SculptForge meshes (`SF_MESH`) and ComfyUI's core `MESH`, so you can wire
*VAE Decode Hunyuan3D → Voxel to Mesh* straight into *SculptForge Voxel Remesh*. Between SculptForge nodes the mesh
stays a SculptForge project in ComfyUI's temp folder, so face groups, vertex paint and sculpt layers survive every step.

Exported files are listed in the node's UI output under `3d` (`filename`, `subfolder`, `type: "output"`), the same
way ComfyUI's *Save 3D Model* reports them, so API clients (including SculptForge's own ComfyUI generator) can
download them from `/view`.

## Examples

The [`examples`](examples) folder has workflows in ComfyUI's API format (load them with *Workflow → Open*, or
queue them with `POST /prompt`):

- [`inflate-remesh-export.json`](examples/inflate-remesh-export.json): *Load Image* → *Inflate Image* → *Voxel
  Remesh* → *Export* GLB. Runs in a few seconds on a CPU. Put [`teddy.png`](examples/teddy.png) in ComfyUI's `input`
  folder to try it.
- [`hunyuan3d-remesh-lods.json`](examples/hunyuan3d-remesh-lods.json): ComfyUI's core Hunyuan3D 2.1 image-to-3D graph
  feeding *Voxel Remesh*, then a sculpt-ready GLB, a 4-level game LOD chain and a USDZ. Needs the Hunyuan3D 2.1
  checkpoint in `models/checkpoints` and a GPU.
- [`core-mesh-roundtrip.json`](examples/core-mesh-roundtrip.json): SculptForge → core `MESH` (*Get Mesh Info*, *Save
  3D Model*) → back into SculptForge for *Voxel Remesh*, *Decimate*, an LOD chain and an FBX.

The `LoadImage` nodes are titled `sf_image`, so SculptForge's ComfyUI generator can run these workflows too.

## Development

The tests need numpy, torch and Pillow (use ComfyUI's Python environment) plus pytest:

```sh
python -m pytest                       # unit tests with a fake sculptforge-cli, plus real-CLI tests if it is installed
COMFYUI_DIR=/path/to/ComfyUI SCULPTFORGE_CLI=/path/to/sculptforge-cli python -m pytest tests/test_comfyui_e2e.py
```

The end-to-end test starts its own ComfyUI on port 8189 (`SF_COMFYUI_PORT`) with this pack linked into a throwaway
`custom_nodes`, checks the nodes register in `/object_info`, queues the example workflows through the HTTP API and
opens the exported files with `sculptforge-cli`. Set `COMFYUI_PYTHON` if ComfyUI uses another interpreter.

## License

GPL-3.0-or-later, like SculptForge. See [LICENSE](LICENSE).
