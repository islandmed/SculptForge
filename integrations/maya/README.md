# SculptForge bridge for Maya

Send the selected meshes from Maya to SculptForge and get them back, the way GoZ works between ZBrush and
other apps. One Python file (`sculptforge_maya.py`), plain `maya.cmds`, no other modules. Tested against a
mocked `maya.cmds` (`tests/maya/test_sculptforge_maya.py`); it uses only commands Maya has had for many
versions (Maya 2022 or newer, Python 3), but it has not been run inside a real Maya yet: please report what
you find on the [issue tracker](https://github.com/islandmed/SculptForge/issues/42).

The two apps share **one exchange folder that you pick in both** and nothing else: no ports, sockets or
servers, and neither app runs anything it finds in the folder. Meshes travel as OBJ files (quads and UVs).

## Install

1. Download `sculptforge_maya.zip` from the
   [latest SculptForge release](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_maya.zip)
   and unzip it (or take `sculptforge_maya.py` from this folder).
2. Copy `sculptforge_maya.py` into your Maya scripts folder:
   - Windows: `Documents\maya\scripts\`
   - macOS: `~/Library/Preferences/Autodesk/maya/scripts/`
   - Linux: `~/maya/scripts/`
3. Start Maya, open the **Script Editor** (Windows > General Editors > Script Editor), and in a **Python** tab run:

   ```python
   import sculptforge_maya
   sculptforge_maya.install_shelf()
   ```

   A **SculptForge** shelf appears with three buttons: **Send**, **Get** and **Folder**. You only do this once;
   Maya keeps the shelf.

## Use

1. In SculptForge choose **File > Maya > Exchange folder...** and pick a folder (an empty one of your own, for
   example `Documents/SculptForge Maya`). In Maya click **Folder** on the SculptForge shelf and pick the same
   folder.
2. In Maya select one or more meshes and click **Send**.
3. In SculptForge choose **File > Maya > Get from Maya** (or turn on **Get automatically**, which checks the
   folder once a second). Each mesh becomes a Part with its Maya name.
4. Sculpt, then choose **File > Maya > Send to Maya**.
5. In Maya click **Get**. Each object that came from Maya gets its new mesh, keeping its transform, parent and
   shading groups; a Part you added in SculptForge becomes a new object. The whole Get is one undo step.

## What comes across

- Positions in world space and Maya's own units (SculptForge takes the numbers as they are), quads, n-gons (as
  triangles), UVs.
- Not: face groups, masks and vertex paint stay in SculptForge; normals, creases, history, deformers and skinning
  stay in Maya. Get replaces the mesh shape, so a skinned or deformed mesh loses its deformers: send a copy of
  those.
- Maya node names: SculptForge sends names made of letters, digits and `_` (a Part called "Body Hi" arrives
  as `Body_Hi`).

## The folder

```
<folder>/maya_to_sculptforge/manifest.json + r<revision>_<k>.obj   written by Maya (Send)
<folder>/sculptforge_to_maya/manifest.json + r<revision>_<k>.obj   written by SculptForge (Send to Maya)
```

```json
{"format": "sculptforge-maya", "version": 1, "revision": 3, "sender": "maya", "sender_version": "2026",
 "unit": "cm", "objects": [{"name": "pCube1", "file": "r3_000.obj"}]}
```

Both sides read the manifest strictly: at most 256 KiB and 256 objects, every field of the right type, names
reduced to Maya's characters, and files must be plain `*.obj` names in the manifest's own folder (no folders,
no `..`, no absolute paths, no links). Each send removes the files of the previous one.
