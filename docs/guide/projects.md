# Projects, autosave and snapshots

A SculptForge project (`.sfproj`) saves your whole scene in one file, so you can close the app and carry on exactly
where you stopped. This chapter covers saving and opening projects, the unsaved-changes prompt, recent files,
autosave and crash recovery, snapshots, and the Library tabs that show your work.

## What a project keeps

A `.sfproj` file holds everything SculptForge knows about the scene:

- every [Part](parts.md), with its name, visibility, mesh, mask, vertex paint, face groups, hidden faces and UVs;
- every [sculpt layer](layers.md) with its weight, and the active layer of each Part;
- each Part's [Quad Remesh Guides](retopology.md#quad-remesh-guides), the curves Quad Remesh's edge loops follow;
- loaded [Gaussian splats](splats.md) and their placement;
- your custom brushes, so a project opened on another computer still has them;
- the camera, the material and display settings, the brush, Draw Size, Intensity (and Smooth's), RGB Intensity, Falloff, paint colour and symmetry;
- a thumbnail, shown in the Library.

Undo history isn't saved: an opened project starts with a clean history.

Model formats such as OBJ, GLB or FBX can't hold all of this (masks, hidden faces, splats, brushes), so writing
them is an export, not a save. See [Exporting](exporting.md).

> **ZBrush:** A `.sfproj` is the equivalent of a ZBrush project (`.ZPR`), and saving a single Part scene is close
> to saving a `.ZTL`. SculptForge can't read `.ZPR` files, and opens a `.ZTL` only after
> the [ZTL Export plug-in](coming-from-zbrush.md#bring-your-ztools-over) converted it in ZBrush; otherwise bring
> models over with [Send to ZBrush](exporting.md) or as OBJ, FBX or GLB.

## Save a project

- **File > Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) saves to the current project. The first time, it asks for a file
  name.
- **File > Save As...** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) saves under a new name.
- In the ZBrush-style layout, clicking the document name in the title strip also saves.

The bar along the top shows the file name with `*` after it while there are unsaved changes, and the window title
starts with `*`. Any edit in any Part counts, as do splat edits and layer weight changes.

If you opened a model file (an OBJ, say) rather than a project, **Save** asks where to save a new `.sfproj`; the
original model file is left alone.

## Open a project or model

- **File > Open...** (<kbd>Ctrl</kbd>+<kbd>O</kbd>) opens a project, a model or a splat capture. Opening a project
  or model replaces the scene.
- **File > Open Recent** lists the last 10 files you opened or saved. Files that no longer exist are dropped from
  the list. **Clear Recent** empties it.
- Drop a file onto the window to open it.
- In the [Library](interface.md), double-click a project or model.

**File > New Sphere** (<kbd>Ctrl</kbd>+<kbd>N</kbd>) and **File > New Plane** start a fresh scene.

When you open a project, its saved camera, material and brush settings come back too.

## Start from a starter mesh

Instead of a sphere, start from a base mesh with clean, even quads: **File > New from Starter Mesh** lists them,
the Library's **Tool** tab has a **Starter meshes** folder with a thumbnail of each, and the welcome's
**Start from a head** opens the human head straight away.

| Starter mesh | What it is |
|---|---|
| Human Head | A neutral head and neck with loops round the eyes and mouth, for portraits |
| Bust | Head, neck, shoulders and upper chest |
| Full Body | A human figure in A-pose, 167 cm tall |
| Hand, Foot, Ear | A left hand (raised, palm to the front), a left foot and ankle, a left ear |
| Eye | An eyeball 24 mm across, with the cornea and rings of quads round the pupil |
| Stylised Head | A cartoon head: big cranium and eyes, small nose and jaw |
| Creature Head | A dragon or creature blockout: skull, jaw and two horns, each its own [Part](parts.md) |
| Torso | A human torso from the neck to the upper thighs |
| Rounded Box, Cylinder, Capsule, Ring | Hard-surface primitives with bevelled edges and quad caps |

Every starter mesh is life size in centimetres, centred, standing on the ground and looking down +Z, and the
symmetric ones are exactly symmetric, so [X symmetry](sculpting.md) works from the first stroke. The Hand, Foot,
Ear, Head, Bust and Torso are cut from a body, so they have open edges where they were cut.

A starter mesh opens with subdivision levels (two, or one for the Full Body): block out the big forms at
**SDiv 1**, the base cage, and step up for the detail (see [Geometry](geometry.md)). It opens as a new, unsaved
scene: **Save** asks for a name. Double-click a tile to open it; <kbd>Shift</kbd>+double-click appends its base
cage as a Part, for example an Eye next to a head.

The human meshes come from the MakeHuman base mesh, which MakeHuman released into the public domain (CC0); the
others are made by SculptForge's own build script. You may use them for anything, including commercial work.

## The unsaved-changes prompt

When the scene has unsaved changes, **New Sphere**, **New Plane**, **Open...**, **Open Recent**, dropping a model or
project, **Quit** (<kbd>Ctrl</kbd>+<kbd>Q</kbd>) and closing the window first ask:

- **Save** saves the project (asking for a name if it has none), then carries on;
- **Don't Save** discards the changes and carries on;
- **Cancel** goes back to your work.

Dropping an image or a `.sfbrush` file never asks, because it doesn't replace the scene.

## Autosave and crash recovery

While the scene has unsaved changes, SculptForge writes a recovery copy every few minutes (every 5 minutes by
default; it waits until you finish a stroke). Change the interval, or turn it off with 0, in
**Edit > Preferences > Autosave** or with the **Autosave (min)** slider in the File palette (0 to 60 minutes).

The recovery copy is `autosave.sfproj` in the SculptForge settings folder (see
[Troubleshooting](troubleshooting.md) for where that is on your system). It is deleted once your work is saved or
you discard the changes.

If SculptForge didn't close normally (a crash or power cut), the next start asks **Restore Autosave?**:

- **Restore** opens the autosaved scene as an untitled, unsaved document. Save it to keep it.
- **Discard** deletes the recovery copy.

The autosave also appears first in the Library **Snapshots** tab while it exists.

> **Note:** Autosave is a safety net, not a substitute for saving. It keeps only the latest copy and is removed as
> soon as you save.

## Snapshots

**File > Save Snapshot** (also the **Snapshot** button in the ZBrush-style layout's title strip) saves a timestamped copy of the whole
scene (ZBrush users: QuickSave). Your project file, its name and its unsaved state are not changed, so a snapshot
is safe to take before any risky step.

- Copies go to the `QuickSave` folder of your library (by default `Documents/SculptForge/QuickSave`), named like
  `Snapshot 2026-10-21 23-15-04.sfproj` (older `QuickSave ...` files are listed too).
- The last 10 snapshots are kept; older ones are deleted.
- They appear in the Library **Snapshots** tab, newest first, labelled with their date and time. Double-click one
  to open it. The tab also has a **Save now** button.

With the ZBrush-style keys (**Edit > Preferences > Input**) <kbd>9</kbd> takes a snapshot; otherwise it has no default
shortcut. To give it one, hold <kbd>Ctrl</kbd>+<kbd>Alt</kbd>, click the **Snapshot**
button and press a key (see [The interface](interface.md)).

## Your library folder and the Library

SculptForge keeps a library folder for your work, by default `SculptForge` in your Documents folder. It is created
on first use with these sub-folders:

| Folder | Library tab | What goes there |
|---|---|---|
| `Projects` | Project | Your `.sfproj` files |
| `Tools` | Tool | Models to open or append as Parts |
| `Brushes` | Brush | `.sfbrush` files |
| `Alphas` | Alpha | Grayscale images for brush alphas |
| `MatCaps` | Material | Lit Sphere images, loaded as materials at start-up |
| `Images` | Image | Sketches and concept art for Generate 3D |
| `Splats` | Splats | Gaussian splat captures |
| `QuickSave` | Snapshots | Snapshots |

Change it in **Edit > Preferences > Library > Library folder**, then Store Config (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>)
to keep the setting.

Press <kbd>,</kbd> (comma) to show the Library. The tabs that matter for projects:

- **Project**: the projects in your `Projects` folder, plus any recent project saved elsewhere. Each shows the
  thumbnail saved with it.
- **Snapshots**: the autosave (when there is one) and your snapshots, newest first.
- **Recent**: everything in **Open Recent**, of every kind.

Double-click a tile to open it (you are asked about unsaved changes first). In the **Tool** tab,
<kbd>Shift</kbd>+double-click a model to append it as a Part instead.

![The Library showing project thumbnails](images/lightbox.jpg)

## Scripting

| Command | What it does |
|---|---|
| `save_project path=work/dragon.sfproj` | Save the project (without `path`, to the current project) |
| `open_project path=work/dragon.sfproj` | Open a project (`open` also recognises `.sfproj`) |
| `open path=scan.obj` | Open a model, splat capture or project |
| `new_sphere subdivisions=6` / `new_plane resolution=128` | Start a new scene |
| `new_starter_mesh name=human-head` | Start from a starter mesh (`levels=0` for the bare base cage) |

Projects saved by `sculptforge-cli` or Python open in the app too. For the file layout, see
[docs/project-format.md](https://github.com/islandmed/SculptForge/blob/main/docs/project-format.md).

## See also

- [The interface](interface.md): Library, shortcuts and Store Config
- [Parts](parts.md)
- [Exporting](exporting.md): formats for other applications
- [Troubleshooting](troubleshooting.md): where settings and the autosave live
