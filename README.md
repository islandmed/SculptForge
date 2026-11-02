<p align="center">
  <img src="assets/branding/sculptforge-256.png" width="112" alt="SculptForge logo">
</p>

<h1 align="center">SculptForge</h1>

<p align="center">
  <strong>The free, open-source alternative to ZBrush.</strong><br>
  Digital sculpting for VFX, animation and games on Linux, macOS and Windows, with free built-in AI:<br>
  an AI brush creator and image/text to 3D.
</p>

<p align="center">
  <a href="https://github.com/islandmed/SculptForge/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/islandmed/SculptForge/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/islandmed/SculptForge/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/islandmed/SculptForge?color=f28c3c"></a>
  <a href="LICENSE"><img alt="License: GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-555">
</p>

<p align="center">
  <a href="https://sculptforge.app/"><b>Website & demo videos</b></a> ·
  <a href="https://github.com/islandmed/SculptForge/releases/latest"><b>Download</b></a> ·
  <a href="https://sculptforge.app/guide/"><b>User guide</b></a> ·
  <a href="#features">Features</a> ·
  <a href="#building">Build from source</a> ·
  <a href="CONTRIBUTING.md">Contribute</a> ·
  <a href="https://discord.gg/gWrYkEmQhH">Discord</a> ·
  <a href="https://sculptforge.app/support.html">Membership</a>
</p>

<p align="center">
  <a href="https://sculptforge.app/#demos"><img src="docs/images/sculpt-demo.gif" width="800" alt="Weathering a detailed lion head with symmetry and turning it to stone in SculptForge"></a>
</p>

SculptForge is a free, open-source alternative to ZBrush: a focused sculpting app with the brushes, symmetry and
navigation ZBrush artists already know. It costs nothing, has no subscription, runs natively on Linux as well as
Windows and macOS, and comes with source code you can change. It starts instantly, works offline,
and fits into game and animation pipelines: sculpt a character, generate its LODs, sculpt blend shapes, and
send it straight into Unreal Engine. **AI comes built in and free**: describe a brush and the AI brush creator
(Brush Lab) builds it, or turn a sketch, concept art or a description into a sculptable model. No account or key is needed. Prefer your own?
Plug in Claude, fal.ai or Meshy, or run models entirely on your own machine.

## Coming from ZBrush?

| | SculptForge | ZBrush |
|---|---|---|
| Price | **Free**, forever | Subscription |
| Source code | **Open (GPL-3.0)** | Closed |
| Linux | **Native** | Not available |
| Familiar brushes, symmetry, Alt/Shift modifiers | **Yes** | Yes |
| Send to Unreal, LOD chains, blend shapes → morph targets | **Built in** | Via plugins / export |
| Image or text → sculptable 3D model | **Built in** (free models) | No |
| Gaussian splats, OpenUSD / USDZ | **Built in** | No |
| AI agents (MCP), headless scripting | **Built in** | ZScript |
| Masking, face groups, visibility (Ctrl / Ctrl+Shift, lasso or rectangle), vertex paint, Lit Spheres | **Yes** | Yes |
| Vertex Paint layers with blend modes (Multiply, Overlay, Screen...) | **Yes** (new in 0.4) | Opacity only |
| Texture painting on UVs (1K-8K, same paint layers), automatic UV unwrap | **Yes** (new in 0.5) | Polypaint, converted to a texture |
| Automatic quad retopology (ZRemesher-style) and Project All detail transfer | **Yes** (new in 0.5) | Yes |
| Quads kept from import, shown and exported as quads | **Yes** (new in 0.4) | Yes |
| Parts (separate meshes: append, split, merge, solo, Alt+click to select) | **Yes** (new in 0.3) | Yes |
| Gizmo: move, scale, rotate and pose (masked parts, soft joints, All Parts) | **Yes** (new in 0.5) | Yes |
| Send to ZBrush (GoZ-compatible exchange) | **Yes** (new in 0.2; not yet tested against a live ZBrush) | Yes |
| Multiresolution (subdivision levels), dynamic topology, UV editing | Not yet ([roadmap](ROADMAP.md)) | Yes |

SculptForge doesn't read ZBrush's proprietary `.ZTL`/`.ZPR` files directly. Bring models over with Send to ZBrush (`.GoZ`
files keep face groups, mask and vertex paint), as OBJ, FBX or GLB, or convert a ZTool library once with the
[SculptForge ZTL Export plug-in for ZBrush](integrations/zbrush/README.md), after which SculptForge opens the `.ZTL` files.

> **Status: early release (v0.8).** The core sculpting, pipeline and AI features work and are tested on every
> commit. Version 0.8 adds dynamic topology (Sculptris Pro style: brushes add triangles where they need them),
> vector displacement brushes, Quad Remesh Guides and sculpt layers on every subdivision level; 0.7 added ZBrush-style
> keys and gestures and a Blender bridge add-on; 0.6 halved the memory and made the view twice as
> fast on huge meshes (see the [changelog](CHANGELOG.md)).

## Features

**An interface ZBrush users can find their way around**
- Task menus (File, Edit, Sculpt, Paint, Mesh, View, Render, Help) in the Forge Terminal look by default, or the
  ZBrush-style layout with alphabetical palettes in one click (Preferences > Interface > Look)
- Shelves around the canvas, palettes holding every tool, a Library (`,`) of thumbnails, the
  brush picker (`B`, type letters to pick), hold-Ctrl help on every button, a command palette (F3) and a script
  console (`` ` ``), and a fully customizable layout with Store Config. See [docs/ui-design.md](docs/ui-design.md).
- A built-in **user guide** (Help > User Guide, Shift+F1) with search, also on the
  [website](https://sculptforge.app/guide/); hold Ctrl over any control and press F1 to open its
  chapter. Source: [docs/guide](docs/guide/index.md).

**Sculpting**
- Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch, Sharp Crease, Clay Block, Tendril, Trim,
  Layer and Nudge brushes; X/Y/Z symmetry; tablet pressure; ZBrush-style modifiers and navigation.
- Stroke types (dots, drag-rect, spray, lazy mouse) and brush alphas (built-in or any grayscale image).
- **Masking** (Ctrl to paint, invert, blur, sharpen, grow/shrink, mask by cavity or face group) that every brush
  respects.
- **Face Groups and visibility** with ZBrush-style gestures: auto groups, Ctrl+Shift isolate / hide, rectangle hide, delete
  hidden.
- **Parts**: keep the body, eyes, teeth and armour as separate meshes, each with its own undo history. You can
  append a sphere, cube, plane or model file, show/hide, solo, reorder, duplicate, merge and split (by face group or
  by piece), and Alt+click a Part to select it. Projects, OBJ, glTF, FBX, USD and GoZ-compatible files keep them as separate
  objects.
- **Gizmo** (W Move, E Scale, R Rotate): move, scale and rotate a Part, or only its unmasked part with a
  soft falloff for posing a limb at its joint; Shift snapping, numeric entry, All Parts, one undo step per drag.
- **Vertex Paint** (Paint brush, RGB while sculpting, fills, cavity paint) and six Lit Sphere materials.
- **Texture painting** on UV maps: Auto UV (xatlas) unwraps any sculpt, then the Paint brush paints a 1K-8K
  texture with the same paint layers and blend modes; vertex paint and texture convert both ways, and OBJ, glTF,
  USD, FBX and GoZ-compatible exports write the PNG next to the model.
- **Project files** (`.sfproj`) that keep everything, with an unsaved-changes prompt, recent files and autosave.
- Unlimited undo, Loop subdivision, **voxel remesh** (DynaMesh-style) and quadric decimation.
- **Quad Remesh** quad retopology (ZRemesher-style: target count, Adaptive Size, Keep Groups, vertex paint density,
  symmetry) and **Project All** to bring the sculpted detail back onto the new topology.
- **Sculpt layers** that work as blend shapes, with live weights and playback.

**Free built-in AI: Brush Lab (describe a brush, AI builds it)**
- "Dry cracked mud", "overlapping dragon scales", then "make it sharper". Brushes are recipes (data, never
  code), with every value exposed as a slider. They're kept in your brush library; share them as
  `.sfbrush` files or one-line brush codes (right-click > Copy brush code, Ctrl+V to paste one). Studios share
  a folder through `SCULPTFORGE_BRUSH_PATH`, and the
  [community gallery](https://sculptforge.app/brushes.html) installs brushes in one click
  (Brush > Community brushes). See [docs/brushes.md](docs/brushes.md).
- **Free out of the box**: no account or key. It uses an open model (GPT-OSS 20B) hosted by Pollinations.ai,
  and falls back to a local Ollama when you're offline.
- Or bring your own: Claude (Anthropic API) or any OpenAI-compatible endpoint, including **private local
  models through Ollama or LM Studio** ([5-minute setup for Windows, macOS and Linux](docs/local-ai.md)).

**Free built-in AI: Generate 3D from an image or text**
- A sketch, a piece of concept art or a description becomes a full 3D model (limbs, back and all), then gets
  remeshed so you can sculpt it straight away.
- **Free** through the official Hugging Face demos of Hunyuan3D 2 / 2.1, TRELLIS.2 and Stable Fast 3D; or
  **your own ComfyUI** (local or a studio GPU server) running any workflow, with ready-made Hunyuan3D 2.1
  workflows included; or paid with your own fal.ai or Meshy key; or plug in any local model.
  See [AI services & model licences](docs/ai-services.md).
- Offline tools as well: silhouette blockout, relief, and stamp-from-view.

**ComfyUI, both ways**
- Generate models in SculptForge with any ComfyUI workflow on your own machine or GPU server
  ([docs/comfyui.md](docs/comfyui.md)).
- The **ComfyUI-SculptForge** node pack brings SculptForge into ComfyUI graphs: voxel remesh, decimate, LOD chains,
  and GLB / FBX / USD / OBJ / STL export, working with ComfyUI's own Hunyuan3D mesh nodes
  ([islandmed/ComfyUI-SculptForge](https://github.com/islandmed/ComfyUI-SculptForge); in ComfyUI-Manager use
  *Install via Git URL*).

**Game & animation pipelines**
- One-click **LOD chains**; blend shapes survive on every level.
- **Send to Unreal** (Ctrl+U) imports or updates the asset in a running Unreal Editor, on this computer or
  another one on your network, with no plugin to install. Static meshes come with LODs, skeletal meshes with
  morph targets. See [docs/unreal.md](docs/unreal.md).
- **FBX** (binary 7.4, LOD groups, blend shapes), **glTF/GLB** (morph targets), **OpenUSD** USDA/USDZ (UsdSkel
  blend shapes), OBJ, PLY and STL. Exports are checked with the Khronos glTF validator and OpenUSD's `usdchecker`.

**Blender bridge**
- The **SculptForge Bridge** add-on for Blender 4.2+ (`addons/blender`): select objects, Send to SculptForge,
  sculpt, Send to Blender. The objects update in place, keeping transforms, materials and modifiers; quads, UVs,
  colours, mask and face sets come across both ways. See [the guide](docs/guide/blender.md) and
  [docs/blender.md](docs/blender.md).

**ZBrush interchange**
- `.GoZ` import/export with face groups, mask, vertex paint and UVs, and Send to ZBrush (Send / Receive) on Windows and
  macOS. Every visible Part goes over as its own ZBrush SubTool. See [docs/goz.md](docs/goz.md); the live bridge hasn't been tested against a running ZBrush yet.

**Gaussian splats**
- Open phone captures (Polycam, Luma, Postshot, Scaniverse and others) as a sculpting reference, convert them
  into watertight meshes, or turn a sculpt into splats.
- Supports PLY (up to SH degree 3), `.splat`, glTF **`KHR_gaussian_splatting`** and OpenUSD
  **`ParticleField3DGaussianSplat`**.

**Automation**
- `sculptforge-cli` runs every operation headless, for build farms and pipelines.
- A built-in **MCP server** (`sculptforge-cli --mcp`) lets Claude Code, Claude Desktop or any MCP client
  sculpt, convert and export.
- Headless thumbnail and turntable rendering, plus a scripted demo recorder (`--demo … --record`).

<table>
  <tr>
    <td><img src="docs/images/screenshot-brush-lab.jpg" alt="Brush Lab: an AI-designed cracked-mud brush carved into a plane"></td>
    <td><img src="docs/images/screenshot-game-ready.jpg" alt="LOD chain preview with wireframe and the Game Ready panel"></td>
  </tr>
  <tr>
    <td align="center"><sub>Brush Lab: "dry cracked mud", designed by a local model</sub></td>
    <td align="center"><sub>One-click LODs, blend shapes and Send to Unreal</sub></td>
  </tr>
</table>

## For studios and pipelines

Built for studios with their own pipelines: **[VFX Reference Platform](https://vfxplatform.com) CY2026/CY2027
builds** (GCC 14, glibc 2.28/2.34, C++20, Python 3.13), a **Python API** (`import sculptforge`), a **Rez**
package, headless CLI and farm rendering, OpenUSD/FBX/glTF, native Linux, and no licence server. The GPL lets
you modify it in-house without publishing your changes. See **[docs/studios.md](docs/studios.md)**.

## Install

**[Download for Windows](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-windows-x86_64-setup.exe)** ·
**[Download for macOS](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-macos.dmg)** ·
**[Download for Linux](https://github.com/islandmed/SculptForge/releases/latest/download/SculptForge-linux-x86_64.AppImage)** (AppImage, x86_64)

Every link gets the newest version. **[install/](install/README.md)** has the full list (.deb, .rpm, ARM64,
portable .zip, Arch) and what to do after downloading. Or install with one command:

```sh
# macOS / Linux
curl -fsSL https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.sh | sh
```

```powershell
# Windows (PowerShell)
irm https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.ps1 | iex
```

The builds aren't code-signed yet. On **macOS**, right-click the app and choose **Open** the first time. On
**Windows**, if SmartScreen appears, choose **More info → Run anyway**. Studio builds (VFX Reference Platform,
[Rez](packaging/rez/package.py)), older versions and checksums are on the
[releases page](https://github.com/islandmed/SculptForge/releases).

## Building

Requirements: CMake 3.24+ and a C++20 compiler (GCC 11+, Clang 14+, Xcode 15+ or Visual Studio 2022).
Dear ImGui, GLM and (if it isn't installed already) SDL3 are fetched automatically. AI features use libcurl
on Linux and macOS, and the built-in WinHTTP on Windows.

```sh
# Linux only: install compilers and X11/Wayland/GL/curl headers (Debian/Ubuntu or Fedora/RHEL)
sudo ./packaging/linux/install-build-deps.sh

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sculptforge            # Windows: build\Release\sculptforge.exe · macOS: open build/SculptForge.app
```

`cpack` in the build folder makes the platform's packages (`.deb`/`.rpm`/`.tar.gz`, `.dmg`, or installer and
`.zip`).

## Pipelines & automation

```sh
# LOD chain for a game asset
sculptforge-cli -c 'open path=rock.obj; export_lods path=out/rock.glb levels=4 ratio=0.5'

# Concept art or a description -> sculptable 3D model (free, via Hugging Face)
sculptforge-cli -c 'image_to_3d path=hero.png; export_usd path=hero.usdz'
sculptforge-cli -c 'text_to_3d prompt="a stylised dragon head"; export_glb path=dragon.glb'

# Sculpt -> Unreal (static mesh with 4 LODs, 2 m tall)
sculptforge-cli -c 'open path=statue.obj; unreal_send destination=/Game/Props height_cm=200 lods=4'

# Phone capture -> watertight base mesh
sculptforge-cli -c 'open path=scan.ply; splat_transform rotate_x=180 fit=true; splat_clean; splat_to_mesh resolution=256; export_glb path=base.glb'

# Design a brush (free, no key) and save it for the team
sculptforge-cli -c 'brush_ai prompt="weathered stone edges"; brush_save path=stone.sfbrush'

# ...or with a private local model
sculptforge-cli -c 'brush_ai prompt="weathered stone edges" provider=openai-compatible model=qwen3:4b-instruct'

# Review-sheet turntable (8 frames)
sculptforge model.glb --render review/model.png --turntable 8 --size 1024x1024
```

Run `sculptforge-cli --list` for every command.

### Let an AI agent sculpt (MCP)

```sh
claude mcp add sculptforge -- /path/to/sculptforge-cli --mcp      # Claude Code
```

For Claude Desktop or other MCP clients, add a stdio server whose command is `sculptforge-cli` with the
argument `--mcp`. The agent gets every command as a tool and works on one live scene per session.

## Controls

The [user guide](https://sculptforge.app/guide/shortcuts.html) has the full list (it's also in the
app: F1 for Controls, Shift+F1 for the guide).

| Action | Input |
|---|---|
| Sculpt | Left-drag on the model |
| Subtract / invert | Hold **Alt** |
| Smooth | Hold **Shift** |
| Rotate | Left-drag background, or right-drag |
| Pan | Alt + left-drag background, or middle-drag |
| Zoom | Mouse wheel, Ctrl + middle-drag, or Ctrl + left-drag background when nothing is masked |
| Paint / erase mask | Hold **Ctrl** / **Ctrl+Alt** while stroking |
| Invert / clear mask | Ctrl + click / Ctrl + drag on the background |
| Isolate / hide face group | Ctrl+Shift + click / Ctrl+Shift+Alt + click |
| Select a Part | Alt + click it |
| Show all | Ctrl+Shift + click on the background |
| Hide outside / inside a rectangle | Ctrl+Shift + drag / Ctrl+Shift+Alt + drag |
| Pen pressure | Strength (and optionally size); curve and test area in **Preferences > Tablet** |
| Pen eraser end | Inverts the brush (Ctrl + eraser erases mask); or Smooth / Erase mask |
| Pen side buttons | Lower: right-click (drag to rotate), upper: Alt (invert), third: pan; remappable |
| Brush size | `[` / `]` |
| Select brush | `1`–`9`, `0` |
| Paint brush / pick colour | `P` / `C` |
| Toggle X symmetry / wireframe | `X` / `Shift+F` |
| Gizmo: Move / Scale / Rotate / back to Draw | `W` / `E` / `R` / `Q` (Alt+drag: the gizmo only; Shift snaps) |
| Frame model | `F` |
| Subdivide | Ctrl+D |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z |
| Open / Save project / Save as | Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Export GLB | Ctrl+E |
| Send to Unreal | Ctrl+U |

Graphics tablets (Wacom, XP-Pen, Huion ...) work through the system's tablet driver on Windows (Windows Ink),
macOS and Linux (Wayland / X11). Setup and mappings: [docs/tablet.md](docs/tablet.md).

On Linux, SculptForge runs as a native Wayland window (no XWayland, sharp at fractional scales) or on X11.

## Project layout

```
src/core     Mesh, BVH, brushes & recipes, layers, undo, subdivision, decimation,
             voxel remesh, Gaussian splats, image-to-3D, command registry (no GPU/UI)
src/io       File formats: OBJ, FBX, glTF/GLB (+ KHR_gaussian_splatting), PLY, .splat, USDA/USDZ
src/ai       AI services (free built-in and bring-your-own): HTTPS client, brush assistant, image/text-to-3D services, Gradio client
src/link     Live links to game engines (Unreal Python remote execution)
src/cli      sculptforge-cli and its MCP server
src/render   OpenGL 3.3 renderer, splat renderer, offscreen capture
src/app      SDL3 + Dear ImGui application, scripted demo recorder
src/python   Python module (import sculptforge)
packaging    Linux desktop/AppStream files, Windows resources, Arch PKGBUILD, Rez package
website      Project website (GitHub Pages)
tests        Unit tests
```

## Privacy

SculptForge has no telemetry, no account and no servers of its own. AI features are optional and talk
directly to the service you choose. See [docs/ai-services.md](docs/ai-services.md) for exactly what is sent
where, and for the licences of the AI models.

## Membership

SculptForge is free, and nothing in the app is ever paywalled: members pay for time and influence.
**[Supporter](https://sculptforge.app/support.html)** ($5 / month): your name in the
[credits](https://sculptforge.app/supporters.html), #members on Discord, a vote in the monthly feature vote and
the monthly pack. **Insider** ($20 / month): also proposing vote options, a daily drop (a brush, alpha or VDM),
preview builds of the next release about a week early and the monthly dev Q&A. Every release stays free for
everyone; bug-fix and security releases go out to everyone the same day. **[Studio](https://sculptforge.app/studios.html#membership)**
($300 / month; Founding Studio: the first 10 studios lock in $150 / month for good): Insider for every artist in
the company, a custom studio build and custom content, guided development, priority triage, a private team
channel and your logo here. Larger custom features are quoted separately.

<!-- Studio members' logos go here. -->

## Contributing

Contributions are welcome: bug reports, brushes, docs and code. Read [CONTRIBUTING.md](CONTRIBUTING.md)
and the [Code of Conduct](CODE_OF_CONDUCT.md). For questions, tips and showing your work, join the
[SculptForge Discord](https://discord.gg/gWrYkEmQhH); bugs and feature requests go to [GitHub issues](https://github.com/islandmed/SculptForge/issues). Please report security issues privately (see
[SECURITY.md](SECURITY.md)).

## License

Copyright © 2026 The SculptForge contributors.

SculptForge is free software, licensed under the [GNU General Public License v3.0 or later](LICENSE). You own
everything you make with it; the GPL covers the program, not your models. Third-party components and their
licences are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

<sub>SculptForge is an independent project and is not affiliated with or endorsed by Maxon, Epic Games,
Autodesk, Pixar, the Khronos Group, Hugging Face, Tencent, Microsoft, Stability AI, Anthropic, fal or Meshy.
ZBrush, GoZ, MatCap and other ZBrush feature names are trademarks of Maxon Computer GmbH. Unreal and Unreal Engine are trademarks of Epic Games, Inc. FBX is a trademark
of Autodesk, Inc. glTF is a trademark of the Khronos Group Inc. Other names are trademarks of their respective
owners.</sub>
