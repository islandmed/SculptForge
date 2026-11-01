# Working with Maya

Keep Maya for the scene, rigging and animation, and sculpt in SculptForge: the **SculptForge shelf** for Maya
sends the selected meshes to SculptForge, and **File > Maya > Send to Maya** sends them back, replacing each
Maya object's mesh by name. It works like ZBrush's GoZ between ZBrush and other apps, on Linux, Windows and macOS.

The two apps exchange files through **one folder that you choose in both**, and nothing else: SculptForge opens
no ports or connections for this, reads and writes only inside that folder, and never runs anything it finds
there. The bridge is off until you choose the folder.

## Install the shelf in Maya

1. Download `sculptforge_maya.zip` from the
   [latest release](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_maya.zip) and
   unzip it.
2. Copy `sculptforge_maya.py` into your Maya scripts folder: `Documents\maya\scripts\` on Windows,
   `~/Library/Preferences/Autodesk/maya/scripts/` on macOS, `~/maya/scripts/` on Linux.
3. In Maya open the **Script Editor**, and in a **Python** tab run `import sculptforge_maya` and then
   `sculptforge_maya.install_shelf()`. A **SculptForge** shelf appears with **Send**, **Get** and **Folder**.

The script uses plain `maya.cmds` and Maya's own OBJ exporter; it needs Maya 2022 or newer.

## Choose the exchange folder

1. In SculptForge choose **File > Maya > Exchange folder...** and pick a folder of your own, for example an
   empty `Documents/SculptForge Maya`.
2. In Maya click **Folder** on the SculptForge shelf and pick the same folder.

**File > Maya** shows the folder; **Turn the Maya bridge off** forgets it (nothing in it is deleted).

## A round trip

1. In Maya select one or more meshes and click **Send** on the SculptForge shelf.
2. In SculptForge choose **File > Maya > Get from Maya**. Each mesh becomes a Part with its Maya name; a
   Part with that name already there is replaced (one undo step on that Part).
3. Sculpt, paint, divide, remesh: anything SculptForge does.
4. Choose **File > Maya > Send to Maya**. Every visible Part goes.
5. In Maya click **Get**. Each object that came from Maya gets the new mesh and keeps its transform, parent and
   shading groups; a Part you added in SculptForge becomes a new object. The whole Get is one undo step in
   Maya.

**Get automatically** (in **File > Maya**, off by default) loads what Maya sends as soon as it arrives:
SculptForge checks the folder's manifest once a second.

## What comes across

| | Maya | SculptForge |
|---|---|---|
| Shape | The mesh, in world space | The Part, as placed in Maya's scene |
| Faces | Quads and triangles; n-gons as triangles | Triangles and quads (Wire shows the quads) |
| UVs | The current UV set | UVs |
| Name | The transform's name | Part name |
| Units | Maya's linear unit (cm by default) | Taken as they are: 1 cm in Maya is 1 unit here |

Face Groups, the mask and vertex paint stay in SculptForge. Normals, creases, construction history, deformers and
skinning stay in Maya: **Get** replaces the mesh shape, so send a copy of a skinned or deformed mesh. A Part
named "Body Hi" arrives in Maya as `Body_Hi` (Maya names allow letters, digits and `_`), and comes back to the
same Part.

## The exchange folder

```
<folder>/maya_to_sculptforge/manifest.json + r<revision>_<k>.obj   written by Maya
<folder>/sculptforge_to_maya/manifest.json + r<revision>_<k>.obj   written by SculptForge
```

Each manifest is a small JSON file: `{"format": "sculptforge-maya", "version": 1, "revision": 3,
"sender": "maya", "unit": "cm", "objects": [{"name": "pCube1", "file": "r3_000.obj"}]}`. Both sides check it
strictly before reading anything: at most 256 KiB and 256 objects, every field of the right type, and each file
must be a plain `.obj` name in the manifest's own folder (no folders, no `..`, no absolute paths, no links).
Each send removes the files of the previous one; anything else in the folder is left alone.

The commands are `maya_send folder=<folder>` and `maya_receive folder=<folder>` (see
[Scripting and automation](scripting.md)).

## Troubleshooting

- **"Nothing has been sent from Maya yet"**: check that both apps use the same folder (open **File > Maya** in
  SculptForge; click **Folder** in Maya to see or change it).
- **Get in Maya says nothing has been sent**: choose **File > Maya > Send to Maya** in SculptForge first.
- **A mesh came in as a new object instead of replacing one**: Get matches the transform's name; renaming the
  object in Maya or the Part's Maya name breaks the link.
