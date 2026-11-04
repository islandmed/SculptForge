# Welcome to SculptForge

SculptForge is a free, open-source digital sculpting application for VFX, animation and games. This chapter
explains what it is, what it can and can't do yet, and how the rest of this guide is organised.

## What SculptForge is

SculptForge is a focused sculpting tool with the brushes, symmetry, masking and navigation that ZBrush artists
already know. Its menus are grouped by task (**File, Edit, Sculpt, Paint, Mesh, View, Render, Help**), with
shelves around the canvas, a Library of thumbnails and pop-up pickers; a ZBrush-style layout with alphabetical
palettes is one click away. It runs natively on Windows, macOS and Linux,
starts in a moment, works offline and costs nothing. ZBrush's keys and gestures are one choice away: answer
**ZBrush** when the first start asks what you are coming from, or pick **ZBrush-style** in **Edit > Preferences > Input**.
See [Coming from ZBrush](coming-from-zbrush.md). There is no account, no subscription, no licence server
and no telemetry.

It is free software under the GNU General Public License v3.0 or later. You own everything you make with it:
the GPL covers the program, not your models.

SculptForge stays free and works the same for everyone; nothing is ever paywalled. Memberships add your name to
the credits, the monthly pack and a vote on what gets built. Insiders will get each release about two weeks
early, starting with SculptForge 1.0 (every release stays free for everyone; bug-fix and security releases go out
to everyone the same day): **Help > Support SculptForge...** opens [sculptforge.app/support.html](https://sculptforge.app/support.html) in your browser, where you can become a
member. That is all it does: the app has no payment code, no account and no licence check. Supporters are thanked
by name in **Help > About**. While SculptForge starts (the first time, after an update, and at most once a week
otherwise) a card shows the memberships; **I'm already a supporter** hides it for good and **Edit > Preferences > Support**
turns it off. After about ten hours of sculpting, SculptForge also shows one small note about this along the bottom
of the canvas, once, and never again after you close it.

## What you can do with it

- **Sculpt** with more than twenty built-in brushes (Standard, Clay, Clay Block, Sharp Crease, Move, Smooth,
  Inflate, Pinch, Flatten, Crease, Trim, Tendril, Layer, Nudge, VDM, the Kelvinlet brushes and more), four stroke types, brush alphas, X/Y/Z
  symmetry and tablet pressure. See [Sculpting and brushes](sculpting.md).
- **Design brushes in words** with Brush Lab: describe "dry cracked mud" and an AI builds a brush recipe you
  can tweak with sliders, keep in your library and share as a file or a one-line brush code. See [Brush Lab
  and sharing brushes](brush-lab.md).
- **Mask, group and hide** parts of the model the ZBrush way: Ctrl to paint a mask, Ctrl+Shift to isolate a
  face group. See [Masking](masking.md) and [Face Groups and visibility](face-groups.md).
- **Keep separate meshes** as Parts (body, eyes, teeth, armour), each with its own undo history. See
  [Parts](parts.md).
- **Paint colour** on the surface (vertex paint) and look at the model through the built-in Lit Sphere materials or your own. See
  [Vertex Paint and materials](vertex-paint.md).
- **Sculpt layers** that double as blend shapes (morph targets), with live weights. See [Sculpt layers and
  blend shapes](layers.md).
- **Rebuild the mesh**: Loop subdivision, voxel remesh (similar to DynaMesh), decimation and one-click LOD
  chains. See [Subdivide, remesh, decimate and LODs](geometry.md).
- **Generate a model** from a sketch, concept art or a description, free through Hugging Face or on your own
  ComfyUI server. See [Image and text to 3D](generate-3d.md).
- **Work with Gaussian splats** from phone captures: sculpt against them or turn them into meshes. See
  [Gaussian splats](splats.md).
- **Export** to glTF/GLB, FBX, OpenUSD, OBJ, STL and PLY, send models straight into Unreal Engine, and
  exchange them with ZBrush through Send to ZBrush (GoZ-compatible exchange). See [Exporting, Unreal and ZBrush](exporting.md).
- **Automate** everything: every action is a command you can type in the console, run headless with
  `sculptforge-cli`, script from Python or hand to an AI agent over MCP. See [Scripting and
  automation](scripting.md).

## What it doesn't do yet

SculptForge is young (version 0.10; see [What's new](whats-new.md)). Some things ZBrush artists rely on are
still on the roadmap:

- Painting across UDIM tiles (the [UV Editor](uv-editor.md) lays them out; painting covers tile 1001).
- Booleans and stencils.

Others are not on the roadmap: ZModeler, the 2.5D canvas, and reading ZBrush's own `.ZTL` and `.ZPR`
files directly (bring models over with Send to ZBrush, the ZTL Export plug-in for ZBrush, or as OBJ, FBX or GLB).

> **Not yet:** where a chapter describes a ZBrush feature SculptForge doesn't have, it says so in a callout
> like this one. The project's ROADMAP.md lists what is planned and in which order.

## Coming from ZBrush

Most habits carry over, and **Edit > Preferences > Interface > Look > Layout > ZBrush-style** brings back the
alphabetical palette bar. You sculpt with a left drag, hold <kbd>Alt</kbd> to subtract and <kbd>Shift</kbd> to
smooth, drag on the background to rotate, <kbd>Alt</kbd>+drag to pan, and hold <kbd>Ctrl</kbd> to mask. The
palettes have the names you expect (Alpha, Brush, Stroke, Tool, Transform...), the Tool palette holds the
Parts (ZBrush's SubTool), Masking and Face Groups (Polygroups) sub-palettes, <kbd>B</kbd> opens the brush pop-up with the same letter keys
(B, S, T picks Standard), <kbd>,</kbd> opens the Library and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>
stores your interface, and <kbd>S</kbd> <kbd>U</kbd> <kbd>I</kbd> <kbd>O</kbd>, <kbd>Space</kbd>, <kbd>Ctrl</kbd>+<kbd>W</kbd>
and the mask keys do what your hands expect with the ZBrush-style keys on (**Edit > Preferences > Input**).
[Coming from ZBrush](coming-from-zbrush.md) lists SculptForge's names for ZBrush terms, what is the same,
what differs and every shortcut side by side; the [Glossary](glossary.md) maps ZBrush terms to SculptForge ones.

## How this guide is organised

- **Getting started**: [installing](installing.md) (with [Windows notes](installing.md#windows)), [the
  interface](interface.md), [navigating the view](navigation.md) and [troubleshooting](troubleshooting.md).
- **Sculpting**: brushes, Brush Lab, masking, face groups, Parts, vertex paint, layers and mesh operations.
- **AI and capture**: generating models from images and text, and Gaussian splats.
- **Files and pipelines**: exporting, projects, scripting and studio deployment.
- **Reference**: [keyboard and mouse](shortcuts.md), [troubleshooting](troubleshooting.md) and the
  [glossary](glossary.md).

Inside SculptForge, **Help > User Guide** opens this guide in its own window with a table of contents, search
and links between chapters. Two other kinds of help are always a keystroke away:

- Hold <kbd>Ctrl</kbd> and point at any button, slider or tile to see what it does, its shortcut and the
  script command behind it.
- Press <kbd>F1</kbd> for the Controls sheet (every shortcut and mouse gesture) or <kbd>F3</kbd> to search for
  any command by name.

## Getting help and reporting problems

**Help > Report a problem** opens the issue tracker on GitHub
([github.com/islandmed/SculptForge/issues](https://github.com/islandmed/SculptForge/issues)). The form asks for
your hardware: operating system, CPU and RAM, graphics card and driver, and your tablet and its driver if you use
one. Add what you did just before the problem; see [Troubleshooting and
FAQ](troubleshooting.md) for what else helps. **Help > Website & documentation** opens the [SculptForge
website](https://sculptforge.app/).

For questions, tips and showing your work, **Help > Join the Discord...** opens the invite to the official
[SculptForge Discord](https://discord.gg/gWrYkEmQhH) in your browser. Bugs still go to the issue tracker, so they can be tracked and
fixed.

## See also

- [Installing SculptForge](installing.md)
- [The interface](interface.md)
- [Glossary](glossary.md)
