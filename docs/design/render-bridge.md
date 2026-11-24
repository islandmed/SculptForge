# Render bridge: stills through Hydra render delegates

Status: design + phase 1 and 2 built (feat/render-bridge), 2026-11-17. Internal planning document; the public page is
`docs/guide/render-bridge.md` and states only what ships.

**Goal.** File > Render with... sends the scene (our own USD export: meshes, materials, vertex paint) plus the
current camera and a light rig to a Hydra render delegate the artist or studio already has, and brings back a still
(PNG or EXR). The still shows in the app, is saved, and can open in any image editor the artist picks once
(a quick client preview from the sculpt). Studios get the same machinery on their own USD stack.

## Design: out of process, the user's own USD tools

The app does not link OpenUSD (src/io/UsdIO.cpp is ours). It stays that way:

1. The app writes a job folder: `scene.usda` (the normal Export USD of the visible Parts) and `render.usda`, a
   small stage that sublayers it and adds a camera, lights and a `RenderSettings` prim.
2. It spawns a **render helper**: OpenUSD's standard `usdrecord` (shipped with every OpenUSD build, with USD
   Python bundles and with most DCC USD distributions), chosen by the user. The helper loads Hydra, the delegate
   and its plugins in **its own process**: third-party native code never runs inside SculptForge.
3. The helper writes the image and exits. The app watches the exit code (no pipes, sockets or IPC), then loads the
   image, shows it, copies it to the Renders folder and, if asked, opens it in the image editor.

Why not link OpenUSD? It adds about 100 MB of libraries, a Python/TBB/MaterialX stack and a version that would
never match the studio's; delegates are built against one exact USD version, so the only USD that loads *their*
delegates is *their* USD. Running their `usdrecord` means the OpenUSD version, the delegates, licences and
`PXR_PLUGINPATH_NAME` are exactly what the artist or IT already controls (brief, studio item 1). A crash, hang or
leak in a delegate takes down the helper, never the sculpt.

Our own small `sf-hydra-render` helper (a C++ `usdrecord` equivalent linking OpenUSD, shipped as an optional
download) is the fallback for artists with no USD install. It is phase 2b; it speaks the same command line, so the
app side does not change.

### The render stage (render.usda)

```
#usda 1.0
( defaultPrim = "SculptForgeRender"  metersPerUnit = 1  upAxis = "Y"  subLayers = [@./scene.usda@] )
def Xform "SculptForgeRender" {
    def Camera "Camera" { matrix4d xformOp:transform = ...  focalLength, horizontal/verticalAperture,
                          horizontal/verticalApertureOffset (the view's lens shift), clippingRange, projection }
    def DistantLight "Key" / "Fill" / "Rim"   (placed relative to the camera, like the viewport's studio light)
    def DomeLight "Sky"                       (soft white environment, low intensity)
    def RenderSettings "Settings" { rel camera, uniform int2 resolution, pixelAspectRatio }
}
```

- The camera is the viewport camera exactly: `inverse(view())`, the vertical field of view, the aspect of the
  chosen resolution (wider or taller than the view keeps the height and widens the frame), the lens shift as an
  aperture offset, orthographic views as `projection = "orthographic"` with apertures in tenths of a scene unit.
- The light rig is on by default ("Studio light"); off renders with whatever the scene and the delegate give
  (phase 2c: sublayer a studio lookdev/shot USD instead, see the phase plan).
- `scene.usda` is the existing exporter, so the bridge renders exactly what Export USD gives a studio: defaultPrim,
  `metersPerUnit`, `upAxis`, one Mesh prim per Part (linked copies as instances), `primvars:displayColor` for
  vertex paint read by a UsdPreviewSurface through a `UsdPrimvarReader_float3`, textures as UsdUVTexture, edge
  creases with `subdivisionScheme`, blend shapes. Improving the export improves the bridge and the studio files
  at once (gaps in the phase plan).

### The helper command line

`usdrecord --camera /SculptForgeRender/Camera --imageWidth W [--renderer NAME] [--renderSettings
/SculptForgeRender/Settings] render.usda out.png|out.exr`

- `--renderer` is only passed when the user picked one; the list comes from the helper itself (`usdrecord --help`
  prints argparse's `{Storm,Embree,...}` choices from `UsdImagingGL.Engine.GetRendererPlugins()`), so we list
  only what that USD build discovers through its own plugin paths. Names are checked against that list and against
  `[A-Za-z0-9 _.-]` before they go on a command line.
- `--renderSettingsPrimPath` (newer usdrecord) is passed only when `--help` lists it; it carries the resolution.
- The height follows from the camera aperture ratio (usdrecord takes the width only).
- EXR when the file name says `.exr` (usdrecord writes through Hio/OIIO; builds without EXR support fail with a
  clear message and PNG still works).
- Samples: there is no delegate-neutral knob (usdrecord renders until the delegate reports converged; each
  delegate reads its own namespaced render settings). No fake setting in the UI: samples and other delegate
  settings come with phase 2c, authored on the RenderSettings prim from a studio lookdev layer or a per-delegate
  preset the studio provides.
- The child runs in its own process group (posix_spawn; on Windows CreateProcessW, started suspended into a
  kill-on-close Job Object; no shell), stdout+stderr to `render.log` in the job folder; Cancel kills the group / the
  whole job (python.exe and anything it started), and closing or crashing the app ends the job too. Its **whole environment is given** (ChildProcess::setEnvironment): the
  app's startup environment, cleaned of AppImage entries (app/OpenUrl.h), then without `SF_*` / `SCULPTFORGE*` and
  anything secret-looking (`*TOKEN*`, `*SECRET*`, `*PASSWORD*`, `*PASSWD*`, `*CREDENTIAL*`, `*_KEY`, `*API_KEY*`).
  `PXR_PLUGINPATH_NAME`, `PYTHONPATH`, `OCIO` and licence variables stay the user's own. The image editor gets the same.
- Windows: `usdrecord` is a Python script (with a `.cmd` launcher). We never run `.cmd`/`.bat` (cmd.exe argument
  parsing is an injection risk); a helper file that starts with a Python `#!` line runs as `python <script> ...`
  with the `python` next to it or on PATH. A `.exe` (husk-style CLIs, our helper) runs directly.

### UI (Classic and Streamlined both: File menu + one window)

```
File
  ...
  Render with... (preview)  Ctrl+Shift+R      -> opens the Render window
+-- Render -------------------------------------------- x -+
|  Renderer   [ Storm            v ]   (from the helper)    |
|  Size       [1920] x [1080]  [View] [1:1] [4:5] [16:9]    |
|  [x] Match view  [x] Studio light                         |
|  (PNG) (EXR)   [x] Open in editor when done               |
|  [ Render ]                                    0:07 [Stop]|
|  +-----------------------------------------------------+  |
|  |                 the finished still                  |  |
|  +-----------------------------------------------------+  |
|  Saved: ~/Pictures/SculptForge Renders/head_0001.png      |
|  [Open in image editor] [Show in folder] [Save As...]     |
+-----------------------------------------------------------+
Preferences > Render bridge
  Render helper   /opt/usd/bin/usdrecord        [Choose...] [Use found]
  Image editor    (system default)              [Choose...] [Clear]
  Renders folder  ~/Pictures/SculptForge Renders [Choose...]
```

- Off by default: until a helper is chosen the window explains what is needed and Render is refused. **Choose
  Helper...** (a file dialog: the choice is the confirmation) or **Use Found** for a `usdrecord` on PATH, which opens
  a prompt naming the absolute path; nothing runs until **Use This Helper**. The confirmed file's SHA-256 is kept
  with the path; a render (app or CLI) with a helper whose fingerprint changed asks again (the prompt says it
  changed) and never runs it unconfirmed. Nothing is run at start-up; the renderer list is read (`--help`) only from
  a confirmed helper, on a worker thread.
- Progress: an elapsed timer and Stop (delegates report no portable progress); the app stays responsive (the job is
  a child process polled once a frame; writing the USD happens on the main thread exactly like Export USD, one
  user-initiated step).
- Failure: the window shows the reason and the last lines of `render.log` (helper missing, exit code, no image,
  unknown renderer, timeout); never a crash. A 30-minute limit stops a hung helper.
- Commands for scripts and headless use: `render_bridge` (renderer, width, height, format, light) and
  `render_bridge_cancel` in the app (and so its Python `sf.run`); `sculptforge-cli` has `render_bridge` (path=, in
  the foreground, a framed three-quarter view) and `render_bridge_renderers`. **No command takes a program path**:
  commands are also MCP tools, and an AI tool must never choose what runs. The CLI's helper is the one confirmed in
  the app, or `sculptforge-cli --render-helper PATH` (typed by a person or a pipeline script).

## Phase plan

| Phase | What | Studio items |
|---|---|---|
| **1 (built)** | usdrecord path end to end: job folder, render stage, camera + light rig, PNG, the Render window, Open in image editor (any program, Preferences), helper chosen and confirmed by the user (fingerprint), Stop, time limit, errors with the log tail, policy switch. Tests: tests/test_render_bridge.cpp (stage, command line, environment, fingerprint, policy, whole renders through a Python stand-in helper; a real Storm render when `usdrecord` is on PATH, else skipped with the reason), tests/ui/render_bridge.sfui (window, setup, render, failure, Stop, policy). | 1, 2 (export as is), 7 (policy switch) |
| **2 (built)** | Renderer picker (the helper's own list), width + match view or free size, EXR (shown in-app converted to sRGB), `render_bridge` commands (app, Python, CLI with `--render-helper`), guide page (docs/guide/render-bridge.md). | 6 (headless) |
| 2b | Optional `sf-hydra-render` helper download (OpenUSD 26.x, Storm only + plugin path), same command line; signed like the app. | 1 (fallback) |
| 2c | **Render in context:** sublayer a studio lookdev or shot USD (their lights, dome, camera; ours off); asset paths through the stage's own Ar resolver (the helper's). OCIO: pass the user's `$OCIO` through (already in the environment) and an EXR "raw/linear" option; AOVs (depth, normal, per-Part ID) through RenderProducts/RenderVars on the settings prim. | 3, 4 |
| 2d | **Export fidelity** (helps every USD user): face groups as GeomSubsets (`familyName = "faceGroup"`), USDC (crate) for meshes over ~2M triangles (text USD of 5M triangles is ~400 MB and slow to parse), MaterialX alongside UsdPreviewSurface, `purpose = "render"` with a proxy for huge meshes, subdivision levels instead of the dense mesh where the delegate subdivides. | 2 |
| 3 | **Dailies:** turntable sequences (N frames around the target, one helper run with a time-sampled camera), burn-in (asset, version, artist, date), output name templates. **Studio profile locks:** helper path, OCIO config, output template, allowed renderers in the studio profile (core/StudioProfile.h keys `render_helper`, `render_renderers`, `render_output`). The on/off switch is already in the policy file (phase 1). | 5, 7 |

## Security review

Attack surface added: **one child process the user chose, started by a user action.** No ports, no sockets, no
IPC beyond spawn + files + exit code, no network (the helper inherits nothing from us that reaches the network;
a delegate that phones home is the user's installed software, as when they run it themselves).

- **What runs:** only the program the user picked in Preferences (or confirmed with "Use found"), by absolute path,
  with an argument vector (no shell, no string command line on POSIX; Windows quoting per CommandLineToArgvW, and
  `.cmd`/`.bat` refused). The image editor likewise: the chosen program with one argument, the image path; when none
  is chosen, the desktop's default opener (the existing app/OpenUrl.h path, cleaned environment).
- **What it loads:** delegates come only from that USD build's own plugin discovery (its install, the user's
  `PXR_PLUGINPATH_NAME`). We never add plugin paths, never pass plugin or library paths taken from a scene, never
  load a delegate in our process.
- **Scene files never run anything:** the stage is generated by us from mesh data; it references only files we
  wrote into the job folder (scene.usda, texture PNGs). Imported USD is flattened to meshes on import (references are
  not resolved, src/io/UsdIO.h), so a malicious USD cannot smuggle a reference, a variant, a script or a plugin
  into the render. Render names, paths and renderer names are validated before they go on the command line.
- **Files:** the job folder is created fresh per render under the user's own cache, 0700 on POSIX
  (`$XDG_CACHE_HOME/SculptForge/render-bridge/job-*`, `~/Library/Caches/...`), on Windows under
  `%LOCALAPPDATA%\SculptForge\render-bridge` only (per-user ACL, never roaming); old job folders are deleted (the
  newest 5 kept for their logs). The result is copied to the Renders folder with a
  name we make (asset name made safe + counter); we never overwrite a file without Save As.
- **Reading the result:** PNG through stb_image, EXR through tinyexr (both already in the app for alphas and VDM;
  the size is capped at 16384 px a side before decoding to bound memory). A helper that writes a hostile image is
  the user's own program.
- **Resources:** one render at a time, Stop kills the process group (Windows: the Job Object), a 30-minute limit, nothing per frame when
  idle (floor tier unaffected: the poll is a waitpid with WNOHANG while a render runs, nothing otherwise).
- **Policy switch:** `"render": {"bridge": "never"}` in the managed policy file (core/ManagedPolicy.h) turns it off
  entirely: no menu item, no window, no Preferences section, and the app's and the CLI's `render_bridge` commands
  refuse (a broken policy fails closed: off). Checked at each use, so a policy change applies at once. "Don't install
  a helper" is not the control (we may ship one, phase 2b). Profiles pin the helper and renderers in phase 3.
- **Confirmation:** a helper runs only by the absolute path the user confirmed, with the SHA-256 recorded then; a
  changed file is never run without a new yes (Python start-up's rule for its own runtime).
- **MCP:** the commands take no program or helper path (see Commands); an AI tool can at most render with the helper
  the user confirmed, like any export.
- **Flatpak / sandboxes:** the helper must be reachable inside the sandbox (or via `flatpak-spawn --host`, not
  used: that is a sandbox escape the user should opt into themselves by choosing such a wrapper).

Reviewed by: input 11-17 (conditions 1-3: cleaned child environment, policy switch, confirm + fingerprint; Windows
job folder under LocalAppData).
