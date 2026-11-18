# Render with your renderer

> **In short:**
> - **File > Render with... (preview)** renders a still of your sculpt from the current view with a renderer you already
>   have: any Hydra render delegate your OpenUSD install finds, or the fast preview renderer that comes with it.
> - The still shows in SculptForge, is kept in your Renders folder and can open in any image editor: a quick
>   preview for a client, straight from the sculpt.
> - It is off until you choose a render helper. The helper runs as its own program; nothing runs inside SculptForge.
> - It is marked **(preview)** until artists have confirmed it with more renderers on their own machines; tell us how
>   it goes on Discord.

## What you need

A **render helper**: OpenUSD's `usdrecord` program. It comes with every OpenUSD install, with the USD Python
packages many DCC applications and studios ship, and with most pipeline toolkits. Whatever renderers (Hydra render
delegates) that install finds through its own plugin folders are the ones you can pick; studios get exactly the
OpenUSD version, renderers and licences their pipeline already uses.

## Set it up once

1. Open **Edit > Preferences > Render bridge**.
2. **Choose Helper...** and pick `usdrecord` (on Windows, the `usdrecord` script in your USD install's `bin`
   folder, not `usdrecord.cmd`). If one is on your PATH, **Use Found** offers it; you confirm the path before it is
   ever run.
   A prebuilt OpenUSD download unpacked into a folder works as it is: pick `bin/usdrecord` inside it, and SculptForge
   uses that folder's own Python and libraries for the render.
3. Optional: **Choose Editor...** picks the image editor finished stills open in (any program). Without one they
   open in your system's default program for images. **Choose Folder...** changes where stills are kept
   (default: a `SculptForge Renders` folder in your Pictures folder).

SculptForge remembers a fingerprint of the helper you confirmed. If the file changes later (an update, or anything
else replacing it), you are asked again before it runs.

## Render a still

1. Frame the view you want.
2. **File > Render with... (preview)** opens the **Render** window.
3. Pick the **renderer** (the list is what your helper finds; **Default** is the helper's own), the **width**
   (with **Match view** the height follows the viewport's shape; off, type both), **PNG** or **EXR**, and whether
   to use the **Studio light** (a key, fill and rim light that turn with the camera, and a soft white sky; off, the
   renderer's own camera light).
4. **Render**. The view stays usable while it renders; **Stop** ends it.
5. The still shows in the window and is kept in your Renders folder. **Open in Image Editor**, **Show Folder** and
   **Save As...** are under it; **Open in editor when done** opens each new still by itself.

EXR stills are linear and keep the full range for compositing; the window shows them converted for the screen.

If a render fails, the window says why and shows the renderer's last messages (for example a renderer that can't
find its licence).

## What gets rendered

The visible Parts, exactly as **File > Export USD / USDZ** writes them: one mesh per Part, vertex paint, textures,
edge creases and the clay material, plus the current camera (lens shift and orthographic views included), the
studio light and the image size. Each render is written to a fresh folder in your cache (`render.usda` beside
`scene.usda`); the last few are kept, with the renderer's log, in case you want to look.

## From scripts and the command line

The `render_bridge` command renders from the current view (`renderer=`, `width=`, `height=`, `format=png|exr`,
`light=`); `render_bridge_cancel` stops it. `sculptforge-cli` renders a three-quarter view of the visible Parts in
the foreground and writes the image where you say:

```
sculptforge-cli -c "open path=head.sfproj; render_bridge path=head.png width=1600 height=1600"
sculptforge-cli --render-helper /opt/usd/bin/usdrecord -c "open path=head.obj; render_bridge path=head.exr"
```

Without `--render-helper` it uses the helper confirmed in the app. `render_bridge_renderers` lists what the helper
finds.

## Safety

- Only the helper you chose runs, by its full path, as a separate program with your own environment (SculptForge's
  own settings and secret-looking variables such as API keys are left out). No ports, no network, nothing else.
- Renderers load inside that program, from your USD install's plugin folders only; SculptForge never loads one.
- The scene file is written by SculptForge from your meshes; nothing in a file you imported is ever run.
- Studios can turn the render bridge off for everyone with `"render": {"bridge": "never"}` in the policy file
  ([SculptForge for studios](studios.md)).
