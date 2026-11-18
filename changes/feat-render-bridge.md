---
whats_new: **Render with your renderer**: **File > Render with... (preview)** renders a still of your sculpt from the current view with
  a renderer you already have (any Hydra render delegate your OpenUSD install finds), shows it, keeps it in your
  Renders folder and opens it in any image editor you choose: a client preview in one click. Set it up once in
  **Edit > Preferences > Render bridge**.
highlight: Render a still with your own renderer
added: **Render with... (preview)** (File menu, off until set up in Edit > Preferences > Render bridge): renders the visible Parts
  from the current view (camera, lens shift and orthographic views, an optional studio light of key, fill, rim and a
  sky dome) through OpenUSD's `usdrecord` from your own USD install, so every Hydra render delegate it finds can be
  picked. PNG or EXR, any size or the view's shape; the still shows in the Render window, is kept in the Renders
  folder (Pictures > SculptForge Renders) and opens in the image editor you choose (any program, else the system's
  default). The renderer runs as its own program: Stop ends it, a failed render shows the renderer's own message,
  and the view stays usable meanwhile. The helper runs only after you confirm it, and asks again if its file changes;
  it gets your environment without SculptForge's settings or API keys. The `render_bridge` command and
  `sculptforge-cli --render-helper PATH` render headless; a studio policy can turn it off
  (`"render": {"bridge": "never"}`). Requested by Oslen on Discord and Hameron on Discord.
testers:
  - Linux or Windows with OpenUSD installed (usdrecord on PATH): Edit > Preferences > Render bridge > Use Found, confirm,
    then File > Render with... (preview) > Render: a still of the sculpt from the same angle appears in the window within a few
    seconds; Open in Image Editor opens it.
  - Any OS with a Hydra renderer besides the default: pick it in the Renderer list, Render; the still uses it. Try EXR.
  - Any OS: start a render, orbit the view meanwhile (it stays smooth), then Stop: it ends at once.
  - Any OS without OpenUSD: File > Render with... (preview) explains what is needed; nothing else changes.
  - Windows (every program SculptForge starts now runs in a job of its own): train splats from a video (frames are read,
    training runs and finishes, Cancel stops it), and update from Help > Check for Updates: the update installs,
    SculptForge starts again on its own and keeps running. Closing SculptForge mid-render ends the render.
---
Phase 1 and 2 of docs/design/render-bridge.md (security review there). Phase 2b (our own helper download), 2c
(render in context, OCIO, AOVs), 2d (export fidelity) and 3 (dailies, profile locks) follow.
