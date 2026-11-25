# integrations

Bridges to other programs that live in this repository but have their own install and, where noted, their own
release. (The Blender add-on is in [addons](../addons/README.md).)

| Folder | What | Needs |
|---|---|---|
| [maya](maya/README.md) | A Maya shelf script (one Python file): send the selected meshes to SculptForge and back through a shared folder, with an optional live link. Releases as `maya-vX.Y.Z` | Maya 2022 or newer |
| [comfyui/ComfyUI-SculptForge](comfyui/ComfyUI-SculptForge/README.md) | ComfyUI nodes that run `sculptforge-cli`: voxel remesh, decimate, LODs, export, "Inflate Image", and open in SculptForge | ComfyUI and `sculptforge-cli` |
| [comfyui/workflows](comfyui/workflows/README.md) | Ready-made ComfyUI workflows (API format) that Generate 3D can run on your own ComfyUI server | ComfyUI |

Guides: [Maya](../docs/guide/maya.md), [ComfyUI](../docs/comfyui.md). The C++ side of the Maya link is in
[src/link](../src/link/README.md) and [src/io](../src/io/README.md).

## Ground rules for a new integration

- The other program talks to SculptForge through files in a folder the user picked, through `sculptforge-cli`, or
  through the live-mode channel in `src/link`; never through a new open port.
- Ship it as data or a script the user can read. Nothing here downloads or runs code on its own.
- Test it against a fake of the other program (see `tests/maya` and `tests/blender`) so CI does not need the product.
