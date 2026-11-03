# Scan an object into a splat

Walk around a clay maquette, a prop or a toy with your phone, and SculptForge turns the video or photos into a
Gaussian splat in the viewport. You watch it sharpen while it trains, and you can orbit around it the whole time.
From there you can sculpt against it, [make it sculptable](scan-to-sculptable.md), or save
it for a splat viewer or a game engine. Nothing is uploaded: everything runs on your own computer.

## What you need

- **A phone or a camera.** Any phone that films 1080p or better works. You don't need a special app.
- **A graphics card.** Training runs on the GPU: NVIDIA, AMD or Intel on Windows and Linux, or an Apple silicon
  Mac. No CUDA is needed. A gaming GPU trains a **Fast preview** in a few minutes.
- **Three free tools**, which SculptForge runs as separate programs:

| Tool | What it does | How you get it |
|---|---|---|
| [Brush](https://github.com/ArthurBrussee/brush) (Apache-2.0) | Trains the splat | SculptForge offers to download it the first time (about 45 MB on Linux and macOS, 160 MB on Windows) |
| [COLMAP](https://colmap.github.io) (BSD) | Works out where each photo was taken | Windows: offered as a download (128 MB). Linux: your distribution's `colmap` package. macOS: `brew install colmap` |
| [ffmpeg](https://ffmpeg.org) | Reads frames from a video | Usually installed already; Linux: the `ffmpeg` package, macOS: `brew install ffmpeg` |

Captures from the SculptForge phone link that carry the phone's own camera positions, and COLMAP or nerfstudio
datasets, don't need COLMAP. Photo folders don't need ffmpeg.

## Capture tips

The capture decides the quality far more than the settings do.

- **Pick a matte object.** Clay, wood, stone, painted plastic and fabric scan well. Shiny metal, glass, mirrors,
  water and see-through plastic don't: the trainer can't tell reflections from surface.
- **Avoid thin and fuzzy parts** such as hair, fur, wire, antennas and leaves, or expect them to come out soft.
- **Use even, soft light.** An overcast day, a room with daylight from several windows, or two lamps bounced off
  the walls. Avoid hard shadows, and don't move the lights or let your own shadow fall on the object.
- **Give it texture.** A plain white object on a plain white table is hard to place; put it on a patterned cloth
  or a newspaper.
- **Walk all the way around.** Keep the object in the middle, filling about half the frame. Go round once at eye
  level and once a little higher, looking down. Every part of the object should be seen from several sides.
- **Move slowly and smoothly.** Each view should overlap the last by about two thirds. Blurred frames are
  dropped automatically, but a capture that is blurred everywhere can't be fixed.
- **Keep the object still**, and don't use a turntable: the background would not turn with it.
- **Length:** a 20 to 60 second video, or 40 to 150 photos.

## Train a splat

1. Open **Render > Gaussian Splats** (or press <kbd>F3</kbd> and type "train").
2. Under **Train from capture**, pick a preset: **Fast preview** or **Quality**.
3. Click **Video...** and pick the video, or **Photos Folder...** and pick the folder of photos. You can also drop a
   video file onto the window. Captures sent from the phone link start by themselves.
4. The first time, a window asks to download Brush. It says what it downloads, from where and how big it is;
   nothing is downloaded until you click **Download Brush**. The download is checked against its SHA-256 checksum
   before it is used. If you already have Brush, click **Choose Brush...** instead.

Training then runs in the background, in three stages:

| Stage | What happens |
|---|---|
| Frames | A video is read at 5 frames a second and the sharpest frame of each moment is kept (80 for Fast preview, 200 for Quality). Photos are used as they are |
| Camera positions | COLMAP finds where each photo was taken (skipped when the capture already knows) |
| Training | Brush trains the splat. Every few hundred steps the viewport shows the latest result |

The panel over the viewport shows the step, the quality so far, how long it has run and roughly how long is left:

- **PSNR** compares the splat with photos it was not trained on, in decibels. Under 18 is rough, 22 to 26 is good
  and over 26 is sharp. A PSNR that stops rising means more training won't help much.
- **Good enough - stop** ends training and keeps what it has learned so far as the result.
- **Cancel** ends training and saves nothing.

You can orbit, pan and zoom while it trains. While the splat trains, the sculpt is hidden so the splat isn't
inside it; tick **Show mesh** in **Render > Gaussian Splats** to bring it back.

## Presets

| Preset | Steps | Image size | Frames from a video | Colour |
|---|---|---|---|---|
| **Fast preview** | 5,000 | up to 1024 px | 80 | simple view-dependent colour (SH 1) |
| **Quality** | 30,000 | up to 1920 px | 200 | full view-dependent colour (SH 3) |

Start with **Fast preview** to check the capture, then train **Quality** from the same capture.

## Where the result goes

The finished splat is saved as a PLY next to the capture:

- for a folder: `<folder>/splat/<folder name>.ply`
- for a video: `<video name>-splat/<video name>.ply` beside the video

A copy goes into the **Splats** tab of the Library, so you can open it again later. The `work` folder beside
the result holds the frames, the camera positions and the logs (`brush.log`, `colmap.log`); you can delete it.

The splat comes in centred and upright: SculptForge uses the camera positions to put the object at the middle of
the scene with the cameras' "up" pointing up. Use **Flip**, **Turn 90** and **Fit** in **Render > Gaussian Splats** if it
still needs a turn. Then:

- [Make it sculptable](scan-to-sculptable.md).
- Lower the opacity and sculpt against it as a reference.
- **Save splats...** to `.ply`, `.splat`, `.glb` or `.usdz` for other apps.

## Tools and settings

The **Tools** section of **Train from capture** shows which tools were found. SculptForge looks for each one in
this order:

1. An environment variable: `SCULPTFORGE_BRUSH`, `SCULPTFORGE_COLMAP` or `SCULPTFORGE_FFMPEG` (the full path of
   the program). Studios can point every workstation at a shared, approved build this way.
2. The program chosen with **Choose Brush...** or **Choose COLMAP...**.
3. A download made by SculptForge, in the `tools` folder of SculptForge's settings folder.
4. `brush_app`, `colmap` and `ffmpeg` on the `PATH`.

There is no ready-made Brush for Linux on ARM (for example Asahi Linux on an Apple silicon Mac) or for Intel
Macs. Build it from its source with Rust (`cargo build --release` in a Brush checkout) and choose the
`brush_app` it builds.

## From the command line

`splat_train` works in scripts, `sculptforge-cli`, Python and the MCP server. There it waits until training has
finished, then loads the result:

```
sculptforge-cli -c "splat_train source=walkaround.mp4 preset=fast; splat_save path=walkaround.glb"
```

| Parameter | Meaning |
|---|---|
| `source` | A video, a folder of photos, a phone-link capture folder, or a COLMAP / nerfstudio dataset |
| `out` | Where to put the result and the work files (default: next to the capture) |
| `preset` | `fast` or `quality` |

In the app, `splat_train` starts training in the background and `splat_train_cancel` stops it
(`splat_train_cancel finish=true` is **Good enough - stop**).

## Captures with camera positions

A capture folder can say where every photo was taken, which skips COLMAP. The phone link writes this in its
`capture.json`; other tools can too:

```
{"kind": "splat", "device": "...", "files": ["IMG_0000.jpg", "..."],
 "fl_x": 1450, "w": 1920, "h": 1440,
 "frames": [{"file": "IMG_0000.jpg", "transform_matrix": [[...], [...], [...], [0, 0, 0, 1]]}]}
```

`transform_matrix` is the camera-to-world transform, row by row, with the camera looking down its -Z axis and +Y
up (the OpenGL and nerfstudio convention, the same as ARKit and WebXR). `fl_x`, `fl_y`, `cx`, `cy`, `w` and `h`
are in pixels and can also be given per frame. Folders that already contain a COLMAP model (`images` and
`sparse/0`) or a nerfstudio `transforms.json` are trained as they are.

## If it goes wrong

- **"COLMAP could not place the cameras"**: too few views overlap, the photos are blurred, or the object is shiny
  or plain. Capture again more slowly, with more texture around the object. The details are in `work/colmap.log`.
- **"Brush stopped with an error"**: often the graphics card ran out of memory. Try **Fast preview**, close
  other GPU-heavy programs, or update the graphics driver. The details are in `work/brush.log`.
- **The splat is cloudy or full of floaters**: train **Quality**, then use **Clean floaters**. Shiny and see-through
  parts stay cloudy whatever you do.
- **Only part of the object came out**: the other side wasn't seen from enough angles. Walk around again.
