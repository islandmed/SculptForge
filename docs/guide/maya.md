# Working with Maya

Keep Maya for the scene, rigging and animation, and sculpt in SculptForge: the **SculptForge shelf** for Maya
sends the selected meshes to SculptForge, and **File > Maya > Send to Maya** sends them back, replacing each
Maya object's mesh by name. It works through files in a shared folder, on Linux, Windows and macOS.

The two apps exchange files through **one folder that you choose in both**, and nothing else: SculptForge opens
no ports or connections for this, reads and writes only inside that folder, and never runs anything it finds
there. The bridge is off until you choose the folder. **Live** keeps the two in step by themselves, still through
the folder; the opt-in **[Live link](#live-link)** lets Maya follow a stroke while you draw it.

## Install the shelf in Maya

1. Download `sculptforge_maya.zip` from the
   [latest release](https://github.com/islandmed/SculptForge/releases/latest/download/sculptforge_maya.zip) and
   unzip it.
2. Copy `sculptforge_maya.py` into your Maya scripts folder: `Documents\maya\scripts\` on Windows,
   `~/Library/Preferences/Autodesk/maya/scripts/` on macOS, `~/maya/scripts/` on Linux.
3. In Maya open the **Script Editor**, and in a **Python** tab run `import sculptforge_maya` and then
   `sculptforge_maya.install_shelf()`. A **SculptForge** shelf appears with **Send**, **Get**, **Auto**,
   **Live**, **Link** and **Folder**. (Updating from an older shelf: copy the new file and run `install_shelf()` again.)

The script uses plain `maya.cmds`, Maya's own OBJ exporter and, for Live, `maya.api.OpenMaya` (all from your
own Maya: nothing of Maya's ships with SculptForge); it needs Maya 2022 or newer.

## Choose the exchange folder

1. In SculptForge choose **File > Maya > Exchange folder...** and pick a folder of your own, for example an
   empty `Documents/SculptForge Maya`.
2. In Maya click **Folder** on the SculptForge shelf and pick the same folder.

**File > Maya** shows the folder; **Turn the Maya bridge off** forgets it (nothing in it is deleted). While the
bridge is on, the top bar has a **Send to Maya** button.

## A round trip

1. In Maya select one or more meshes and click **Send** on the SculptForge shelf.
2. SculptForge loads them by itself. Each mesh becomes a Part with its Maya name; a Part with that name already
   there is replaced (one undo step on that Part).
3. Sculpt, paint, divide, remesh: anything SculptForge does.
4. Click **Send to Maya** in the top bar (or **File > Maya > Send to Maya**). Every visible Part goes.
5. Maya loads them by itself. Each object that came from Maya gets the new mesh and keeps its transform,
   parent and shading groups; a Part you added in SculptForge becomes a new object. The whole Get is one undo
   step in Maya.

Both sides pick up a send by checking the other side's manifest every 250 ms (its modification time and size
only), and only for sends made after the bridge started: something sent earlier waits for a manual Get.

- In SculptForge, **File > Maya > Get automatically** is on once you choose the folder; turn it off to load
  only with **Get from Maya**.
- In Maya, **Auto** on the shelf is on by default and starts with the first Send, Get or Folder click in a Maya
  session; click it to switch to **Manual**, where only **Get** loads.

## Live

With **Live** on, you don't click Send at all: each app sends what you change by itself, and the other loads it.

- In SculptForge, turn on **File > Maya > Live**. After each stroke (once you pause for 300 ms, never in the
  middle of one), the Parts that stroke, an undo or any other edit changed go to the folder. Parts you didn't
  touch stay put, so a big scene with one Part being sculpted sends only that Part. The files are written in the
  background while you keep sculpting.
- In Maya, click **Live** on the shelf (it turns **Auto** on too). The meshes you sent or got, and the ones
  selected when you turned it on, are watched; 300 ms after you stop editing one, it goes to SculptForge.

Neither app sends back what it just loaded: a Get is never an edit to send, each manifest says which app session
wrote it, and SculptForge skips an object that comes back exactly as it went out (no extra undo step). Live is off
until you turn it on, on each side, and stays as you left it.

## Live link

The **Live link** goes further: Maya follows your stroke while you draw it. It's a connection between the two
apps on this computer, so it is **off until you turn it on, asks first every time, and is off again whenever
SculptForge starts**.

1. In SculptForge choose **File > Maya > Live link...**. A warning explains what it opens:

   > **It opens a connection on this computer.** Anything running on this computer that knows the pairing code
   > could send meshes into SculptForge. It carries meshes only: never commands, files or scripts. Other
   > computers and web pages can't reach it. Turn it off when you're not using it: click "Live link on" in the
   > top bar. It is off again every time SculptForge starts.

   Click **Turn on**. An ember **Live link on** chip appears in the top bar.
2. Choose **File > Maya > Copy pairing code**.
3. In Maya click **Link** on the SculptForge shelf, paste, and click **Link**. The button says **Linked**, and
   SculptForge sends Maya every visible Part, so both start from the same mesh.

Now sculpt: about ten times a second, the vertices your stroke moves appear in Maya. When a stroke changes the
faces (dynamic topology, remesh, divide), the whole Part goes when the stroke ends. Edit a mesh in Maya and
SculptForge gets it as soon as you pause (one undo step there). Updates that arrive in Maya over the link aren't
Maya undo steps: undo in SculptForge, and Maya follows.

**To turn it off**, click **Live link on** in SculptForge's top bar (or **File > Maya > Turn the Live link off**),
or click **Linked** in Maya. Quitting SculptForge turns it off too.

How it is kept safe:

- **No network port.** It is a Unix domain socket in a folder only you can open (Linux, macOS) or a named pipe
  only your Windows account can open, remote clients refused. Web pages, other people on the machine and other
  computers can't reach it.
- **A pairing code** (128 random bits, new each time you turn it on) must open every connection; one Maya at a
  time.
- **Meshes only.** Object names, vertices, faces and UVs, checked strictly on arrival (sizes, ranges, finite
  numbers, Maya names). Nothing that arrives is run, and it never names a file.
- Studios can forbid it with `"maya": {"link": "never"}` in the [studio policy](studios.md).

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
- **Maya doesn't load a send by itself**: Auto starts with the first click on the shelf in a Maya session; click
  **Get** once (or **Send**), and check that the button says **Auto**, not **Manual**.
- **Live doesn't send from Maya**: Live watches the meshes you sent or got and the ones selected when you turned it
  on; send a new mesh once with **Send**, and it is watched from then on.
- **Link says SculptForge refused the pairing code**: the code is new each time the Live link is turned on; copy it
  again (**File > Maya > Copy pairing code**).
- **Link says SculptForge isn't offering it**: turn on **File > Maya > Live link...** in SculptForge first (it is
  off at every start).
- **A mesh came in as a new object instead of replacing one**: Get matches the transform's name; renaming the
  object in Maya or the Part's Maya name breaks the link.
