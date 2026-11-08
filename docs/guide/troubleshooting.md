# Troubleshooting: tablets, display, crashes and FAQ

This chapter collects fixes for common problems: start-up and graphics errors, tablets, AI service messages,
ComfyUI errors, the camera notch, first-launch warnings on macOS and Windows, where SculptForge keeps its
settings, how to reset the interface, and how to report a bug.

Quick answers:

| Problem | Fix |
|---|---|
| No pen pressure on Windows | Switch **Windows Ink** in the tablet driver and bring the pen back ([Pen tablets](#pen-tablets)) |
| The cursor jumps when the pen touches down | **Edit > Preferences > Tablet > Mouse mode (relative)** |
| "Windows protected your PC" | **More info**, then **Run anyway** ([First launch warnings](#first-launch-warnings-on-macos-and-windows)) |
| Text and buttons too small or too large | **Edit > Preferences > Interface > UI scale** ([details](#text-and-buttons-are-too-small-or-too-large)) |
| Menus or shelves look wrong after an update | **Edit > Preferences > Config > Reset Layout and Shortcuts** ([Reset the interface](#reset-the-interface)) |
| Sculpting feels slow | [The viewport is slow](#the-viewport-is-slow) |
| Lost work after a crash | [Recovering work after a crash](#recovering-work-after-a-crash) |
| Want the Classic layout back | **Edit > Preferences > Interface > Look > Layout > Classic** |

## SculptForge doesn't start

SculptForge draws with OpenGL 3.3 (core profile). Almost every GPU from the last decade supports it, but an
old driver, a remote desktop session or a virtual machine without 3D acceleration may not.

1. **Update your graphics driver** from the GPU maker (NVIDIA, AMD, Intel) or your Linux distribution.
2. **Virtual machines:** turn on 3D acceleration in the VM settings.
3. **Remote desktop:** some remote sessions only offer an older OpenGL. Run SculptForge locally, or use a
   remote tool that forwards the GPU.
4. **Linux:** on a machine without a working GPU driver, Mesa's software renderer works (slowly):
   `LIBGL_ALWAYS_SOFTWARE=1 sculptforge`.
5. **Linux on Apple Silicon (Asahi) and other ARM machines:** use the ARM64 download. The x86_64 one runs
   there only through the FEX emulator, which stops at once with `<jemalloc>: Unsupported system page size` on
   16 KiB pages; start it in a 4 KiB-page VM with `muvm -- ./SculptForge-linux-x86_64.AppImage` if you need it.

To see why it stopped, start it from a terminal and read the messages. It prints the OpenGL version and GPU it
found at start-up (for example `OpenGL 4.6 (Core Profile) Mesa 26.1.8 on ...`), or the error, such as
`OpenGL 3.3 context creation failed` or `Missing OpenGL function: ...`.

- Linux: run `sculptforge` in a terminal.
- macOS: run `/Applications/SculptForge.app/Contents/MacOS/SculptForge` in Terminal.
- Windows: the release build has no console window; if it closes straight away, update the driver first, then
  read the [log file](#the-log-file), which holds the same messages.

## First launch warnings on macOS and Windows

The release builds aren't code-signed yet, so the operating system warns the first time.

- **macOS (Gatekeeper):** "SculptForge can't be opened because it is from an unidentified developer". In
  Finder, right-click (or <kbd>Ctrl</kbd>+click) **SculptForge** in Applications and choose **Open**, then
  **Open** again. macOS remembers the choice. On recent macOS versions you may instead need **System Settings
  > Privacy & Security > Open Anyway**.
- **Windows (SmartScreen):** "Windows protected your PC". Click **More info**, then **Run anyway**.

Download SculptForge only from the official releases page on GitHub (the website that Help > **Website &
documentation** opens links to it).

## The viewport is slow

- Check the triangle count in the bar along the top of the window (**Tris**). Millions of triangles on a laptop GPU are heavy;
  **Tool > Geometry > Decimate** or a lower **Voxel Remesh** resolution brings it down. See
  [Subdivide, Remesh, Decimate and LODs](geometry.md).
- Turn off **Wireframe** (<kbd>Shift</kbd>+<kbd>F</kbd>) on dense meshes.
- Hide Parts you aren't working on, or **Solo** the active one.
- A large Gaussian splat capture costs GPU time too: lower the count with **Clean floaters**, or untick
  **Show** in **Render > Gaussian Splats** while you sculpt.
- On laptops with two GPUs, make sure SculptForge runs on the dedicated one (in the GPU maker's control panel
  or the operating system's graphics settings).
- **Edit > Preferences > Performance**: lower **Antialiasing** (or turn it off), try **View resolution** 75% or 50%,
  keep **Fast Navigation** on, and look at **Last stroke** to see whether brushes or the graphics card take the time. Step down a
  [subdivision level](subdivision-levels.md) for big changes. See [Performance and big meshes](performance.md).
- A Part missing from the view with "Not enough GPU memory" in the status bar didn't fit on the graphics
  card: hide other Parts or step down a level.

## Pen tablets

SculptForge reads pen pressure through the operating system's standard pen input, so most Wacom, XP-Pen, Huion
and similar tablets, and pen displays, work without settings.

1. Open **Sculpt > Draw & Tablet**. Under **Tablet** it shows "Pen pressure: 0.42" once the pen has come near the
   tablet, or "No pen detected".
2. **Pressure > strength** (on by default) and **Pressure > size** choose what pressure controls. They are
   also on the top shelf.
3. A custom brush can also set its own pressure response (**pressureCurve** in its recipe, see
   [Brush Lab and sharing brushes](brush-lab.md)).

**Edit > Preferences > Input** and **Edit > Preferences > Tablet** show "Pressure: on" once the pen has pressed, or
"Pressure: not detected". Without pressure the pen still draws, at full strength. A tablet plugged in while
SculptForge runs works straight away; the status bar says "Tablet connected" (also through Wacom Bridge).

If no pen is detected or pressure doesn't change:

- Install or update the tablet maker's driver.
- **Windows:** SculptForge reads pressure through Windows Ink, and through the driver's Wintab when Windows Ink is
  off (Wacom Tablet Properties > Mapping > **Use Windows Ink**). If pressure is still missing, switch that option
  and bring the pen back to the tablet. See [Pen tablets on Windows](installing.md#pen-tablets-windows-ink-and-wintab).
- **Linux:** check that the system sees the pen as a tablet (for example in your desktop's tablet settings).
- If the cursor moves but strokes have no pressure, test in another application to see whether the driver
  reports pressure at all.
- **The active Part changed by itself:** a Part becomes active with **Alt+click** on it (the keyboard's Alt, a
  quick click), a triple-click with the mouse or a click in the Parts list; the status bar names it and how. A
  tablet's ExpressKey set to Alt counts as the keyboard's Alt.
- **The cursor jumps back when you put the pen down elsewhere:** the tablet driver is set to mouse (relative)
  mode. Turn on **Edit > Preferences > Tablet > Mouse mode (relative)**. It also makes the pen relative with the driver
  in pen mode; the arrow is drawn at the brush while the pen is in range.
- **The pen picks a brush, then stops clicking menus or the canvas until you use the mouse** (Huion, display
  tablets, Wacom; 0.10.8 and earlier): update to 0.10.9 or later. If it still happens, tell us the tablet, its
  driver version and whether Windows Ink is on.
- **With two screens, the app slows down when the pointer is on the other screen:** fixed in 0.10.9; SculptForge
  now rests only when its window loses focus.

## AI services

### Hugging Face (free image and text to 3D)

| Message | What to do |
|---|---|
| *Hugging Face's free GPU time is used up for now* | The anonymous daily allowance ran out. Add a free token (huggingface.co/settings/tokens, read access) in **HF token** in the Generate 3D window, or set `HF_TOKEN`; or wait for it to refill (about a day); or pick another model |
| *The Hugging Face Space is asleep or restarting* | The model's demo is starting up. Try again in a minute, or pick another model |
| Waiting in the queue for a long time | The demos are shared. Try a faster model (Hunyuan3D 2 mini Turbo, Stable Fast 3D) or another time of day |
| TRELLIS.2 fails without a token | TRELLIS.2 needs a Hugging Face token |

### fal.ai and Meshy

| Message | What to do |
|---|---|
| *The API key was rejected* | Check the key in the Generate 3D window, or the `FAL_KEY` / `MESHY_API_KEY` variable |
| *Out of credits on this account* | Top up your account with that service |
| *Model or endpoint not found* | Check the fal model id |
| *Rate limited* | Wait a moment and try again |

### Hyper3D Rodin (direct)

| Message | What to do |
|---|---|
| *Add your Hyper3D API key* | It stays off until you add your own key (hyper3d.ai > API dashboard) in the Generate 3D window |
| *Hyper3D rejected the API key* | Check the key, or create a new one in the API dashboard |
| *Your Hyper3D plan doesn't include this tier* | Basic API access covers Extreme Low and Low; choose one of those, or upgrade on hyper3d.ai |
| *Out of credits on your Hyper3D account* | Top up on hyper3d.ai |
| *Refused the model file* | The result came from somewhere other than Hyper3D's servers, so SculptForge didn't download it. The model isn't lost: it stays in your Hyper3D account (hyper3d.ai dashboard) under the task id in the message. If it keeps happening, [report it](https://github.com/islandmed/SculptForge/issues) |

### Brush Lab

The default **Free** provider uses a hosted open model with no account. If it is unreachable and Ollama runs
on your computer, SculptForge uses your local model instead. "Rate limited by the provider" means too many
requests in a short time: wait a few seconds. For setup of local models, see
[Brush Lab and sharing brushes](brush-lab.md).

### No connection at all

AI features need internet access (except a local Ollama, LM Studio or ComfyUI). Behind a company proxy or
firewall, ask IT to allow the service you use: huggingface.co (and `*.hf.space`), fal.ai, meshy.ai, api.hyper3d.com (and Hyper3D's file servers), or the
Pollinations.ai text endpoint for the free Brush Lab model.

## ComfyUI errors

| Message | What to do |
|---|---|
| *Nothing is answering at http://...* | ComfyUI isn't running at that address, or a firewall blocks it. Check the address and port (8188 by default). For another computer, start ComfyUI with `--listen` |
| *...answered, but not like ComfyUI* | Something else uses that port, or a proxy is in the way. Use the address ComfyUI prints when it starts |
| *This workflow was saved in ComfyUI's editor format* | Export it again in ComfyUI with **Workflow > Export (API)** |
| *This ComfyUI does not have the node ...* or *...is missing the nodes ...* | Install the custom nodes the workflow uses (ComfyUI Manager > **Install Missing Custom Nodes**) and restart ComfyUI |
| *...names a model file this server doesn't have* | Download the model into the right subfolder of ComfyUI's `models` folder, for example `models/checkpoints` |
| *ComfyUI rejected the workflow: node N ...* | An input is out of range. Open the workflow in ComfyUI: it shows the same error on that node |
| *The workflow finished but saved no mesh file* | End the workflow with Save GLB or another node that saves a mesh |
| *...the GPU ran out of memory* | The GPU ran out of memory. Lower the resolution or octree size, or use a smaller model |
| *The job disappeared from ComfyUI's queue* | Someone cleared the queue or ComfyUI restarted. Generate again |
| *The workflow has no Load Image node* | You chose a text workflow on the **From image** tab. Use it from **From text** |

See [Image and text to 3D](generate-3d.md#comfyui-your-own-server) for setup.

## The interface

### The top of the window is hidden behind the camera notch

On MacBooks with a camera notch, SculptForge keeps the interface below the notch when it fills the screen. If
the title strip or palette bar is still covered, or a plain band appears where you don't want one, change
**Edit > Preferences > Interface > Camera notch**: **Automatic** (asks the system on macOS, recognises notched
MacBook displays on Linux), **Always reserve**, or **Off**. <kbd>F11</kbd> switches between fullscreen and a
window.

### Text and buttons are too small or too large

**Edit > Preferences > Interface > UI scale** sets the size (Automatic, 100% to 200%) and applies it at once and
keeps it for the next start. **Automatic** uses
the scale of the monitor the window is on: on Windows that is **Settings > Display > Scale**, per monitor, and the
interface follows when you drag the window to another monitor. On Linux under X11 it comes from the desktop's
font DPI (Xft.dpi); if Automatic looks wrong there, pick a size.

### My layout is gone after a restart

Shortcuts and preferences save themselves; the layout (shelves, trays, open palettes) waits for
**Edit > Preferences > Config > Save Layout**. See [The interface](interface.md#saving-preferences-and-layouts).

### Reset the interface

- **Edit > Preferences > Config > Reset Layout and Shortcuts** returns to the built-in layout and default shortcuts. The shortcuts
  save themselves; press **Save Layout** afterwards to keep the layout.
- **Revert to Saved Layout** goes back to the layout the last Save Layout kept.
- To start completely fresh, quit SculptForge and delete `ui.json` (layout, shortcuts, interface preferences)
  and `imgui.ini` (positions of floating windows such as Brush Lab) from the settings folder below.

### A key does nothing

Shortcuts don't fire while you type in a text field: click the canvas first. Check the key in **Help >
Controls** (<kbd>F1</kbd>), which shows every shortcut as currently assigned; a key you reassigned shows in
orange in **Edit > Preferences > Hotkeys**, where **Default shortcuts** undoes every change.

## Where settings live

SculptForge keeps its settings in one folder per user:

| System | Settings folder |
|---|---|
| Linux | `~/.local/share/SculptForge/SculptForge/` (or `$XDG_DATA_HOME/SculptForge/SculptForge/`) |
| macOS | `~/Library/Application Support/SculptForge/SculptForge/` |
| Windows | `%APPDATA%\SculptForge\SculptForge\` (usually `C:\Users\<you>\AppData\Roaming\SculptForge\SculptForge\`) |

| File or folder | What it holds |
|---|---|
| `ui.json` | Shortcut changes and preferences (saved as you change them) and the layout (written by **Save Layout**) |
| `imgui.ini` | Positions and sizes of floating windows |
| `ai.json` | AI settings: providers, models, keys, ComfyUI address (readable only by your account) |
| `project.json` | Recent files and the autosave interval |
| `autosave.sfproj` | The autosaved copy of unsaved work (removed once you save) |
| `clean_exit` | Marks a normal exit; without it, the autosave is offered for restore |
| `sculptforge.log`, `sculptforge-previous.log` | What this session and the last one did, and what crashed (see [The log file](#the-log-file)) |
| `console_history.txt` | The console's command history |
| `recent_actions.txt` | Recently used entries of the command palette |
| `brushes/` | Your brush library, one `.sfbrush` file per brush |
| `brush-gallery-cache/`, `brush-gallery.json` | Community brush gallery cache and its address |
| `thumbs/` | Cached Library and picker thumbnails (safe to delete) |

Your **library folder** (projects, tools, alphas, images, splats and snapshots shown in the Library) is
separate: `Documents/SculptForge` by default, changeable in **Edit > Preferences > Library > Library folder**.

> **Tip:** To move settings to another computer, copy `ui.json`, `ai.json` and the `brushes` folder. For a
> shared layout, use **Edit > Preferences > Config > Export Interface...** and **Import Interface...** instead.

## The log file

SculptForge writes what it does to `sculptforge.log` in its [settings folder](#where-settings-live). Choose
**Help > Open log folder** to open it:

| System | Log file |
|---|---|
| Windows | `%APPDATA%\SculptForge\SculptForge\sculptforge.log` |
| macOS | `~/Library/Application Support/SculptForge/SculptForge/sculptforge.log` |
| Linux | `~/.local/share/SculptForge/SculptForge/sculptforge.log` |

It holds the version, your system and GPU (the `OpenGL ... on ...` line), warnings and errors, and each command
you ran. If SculptForge crashes, it ends with lines starting `CRASH:` that say what crashed and what was done
last. Every start keeps the previous session's log as `sculptforge-previous.log` beside it, so after a crash
**attach `sculptforge-previous.log` to your report** (the new `sculptforge.log` is the session after it). It holds
no models and no keys, only command names and messages, which may include file names.

### A performance trace

When SculptForge gets slow, **Help > Record a performance trace** shows where the time goes. Turn it on, do what
feels slow (the stroke, the orbit, the remesh), then choose it again to stop. `sculptforge-trace.log` beside the
log then lists every frame that took longer than 33 ms with its parts (events, dabs, interface, GPU uploads,
drawing, GPU time), each stroke's timings, commands that held up the frames, background jobs starting and
ending, the mesh size and levels, and on its first line your GPU, driver and CPU. **Attach it to your report.**
It is off by default and costs nothing when off; it stays small (2 MB, then the older lines move to
`sculptforge-trace-previous.log`) and never leaves your computer.

## Recovering work after a crash

SculptForge autosaves unsaved work every 5 minutes by default (**Edit > Preferences > Autosave**, 0 turns it off).
After a crash, the next start asks "SculptForge did not close normally last time. Restore the autosaved
work?". **Restore** opens it as unsaved work (save it under a name); **Discard** deletes it. See
[Projects, autosave and snapshots](projects.md). The dialog also points to the crashed session's
[log file](#the-log-file).

## Frequently asked questions

**Can SculptForge open another sculpting app's own project files?**
Not directly: those formats are proprietary and undocumented. Export OBJ or FBX from that app, or use the
[Bridge](exporting.md#exchange-models-through-the-bridge).

**Does it have subdivision levels, voxel remesh, quad remesh or UVs?**
Yes: **Voxel Remesh**, **Quad Remesh** (with **Project Detail** to bring the detail back; see
[Quad retopology](retopology.md)), **Auto UV** unwraps any model, and Add Level adds subdivision levels you can
step up and down (see [Subdivision levels](subdivision-levels.md)). **Dynamic** on the top shelf is dynamic
topology, like dynamic topology (see [Dynamic topology](dynamic-topology.md)).

**Is it really free? Can I sell what I make?**
Yes and yes. SculptForge is GPL-3.0-or-later free software; the licence covers the program, not your models.
The AI models have their own licences (see [Image and text to 3D](generate-3d.md#privacy-and-model-licences)).

**Does SculptForge send anything about me anywhere?**
No. There is no telemetry and no account. AI features talk only to the service you choose, when you use them.

**Does it work offline?**
Yes. Everything except the hosted AI services works offline, and Brush Lab and image-to-3D can run on local
models (Ollama, LM Studio, your own ComfyUI or generator command).

**Where are my brushes?**
In the `brushes` folder in the settings folder above. Brush Lab saves every brush you make or refine there.

## Report a problem

Choose **Help > Report a problem** to open the issue tracker on GitHub (you need a free GitHub account). A
good report includes:

1. **The SculptForge version** (**Help > About SculptForge**) and your operating system.
2. **Your GPU and driver**: the `OpenGL ... on ...` line SculptForge prints at start-up (see
   [SculptForge doesn't start](#sculptforge-doesnt-start)), or the GPU name from your system settings.
3. **Steps to reproduce**: what you did, what you expected and what happened instead.
4. **The commands you ran**: open the console (the backtick key, left of <kbd>1</kbd>), click **Copy session
   as script** and paste it into the report. It often reproduces the problem exactly.
5. **After a crash, the log**: `sculptforge-previous.log` from **Help > Open log folder** (see
   [The log file](#the-log-file)).
   **When it is slow**, a [performance trace](#a-performance-trace) of the slowdown.
6. **A file** if the problem is with a particular model (a small `.sfproj` or the original file), and a
   screenshot for display problems.

For a security problem, don't open a public issue; follow `SECURITY.md` in the repository instead.

## See also

- [Installing SculptForge](installing.md)
- [The interface](interface.md)
- [Glossary](glossary.md)
