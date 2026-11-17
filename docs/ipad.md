# SculptForge for iPad (early)

The same app as on the desktop and on Android tablets, built for every iPad that runs iPadOS 16 or later (the base iPad
from its 5th generation, every iPad Air, mini and Pro since). Milestone 1: it installs from TestFlight, opens a starter
mesh, sculpts with a finger or the Apple Pencil (pressure and tilt), navigates with two fingers, undoes, and saves and
opens projects. Members get the TestFlight builds first.

## Build

On a Mac with Xcode 16 (GitHub's macOS runners have it; `.github/workflows/ios.yml` builds every change to the iPad side):

```sh
scripts/ios-build.sh sim --tests        # the simulator app and the core test suite (Ninja): build-ios-sim/SculptForge.app
scripts/ios-build.sh device --build 7   # the device archive (Xcode), unsigned: build-ios-device/SculptForge.xcarchive
scripts/ios-testflight.sh --build 7     # signed and uploaded to TestFlight (needs the App Store Connect key: docs/releasing.md)
```

The workflow's simulator job also runs the core tests inside the simulator (`tools/ci/ios-sim-run.sh` is ctest's emulator)
and takes screenshots on a base iPad and on the biggest iPad Pro (`tools/ci/ios-sim-shots.sh`: the start screen, then
`packaging/ios/smoke.sfui` opens the bust, sculpts and undoes).

## How the desktop code runs here

- **Drawing:** OpenGL ES 3.0, the most iPadOS offers (no Metal renderer yet). The renderer is written for ES 3.2, so
  `render/GL.cpp` adds the **ES 3.0 tier** (`gl::es30Tier()`): texture buffers become 2D textures 4096 texels wide (the
  shaders' `texelFetch` on them is rewritten; each copy follows the rows its buffer changed in), and a draw's base vertex
  re-points the vertex array's attributes. What has no stand-in in ES 3.0 is left out and the app says so beside its
  switch: face group colours, the texture preview and baked normal maps (they need a geometry shader and `gl_PrimitiveID`),
  and the wireframe's fade of sub-pixel edges. `SF_GLES_LEVEL=300` runs the tier on any driver, desktop GL included:
  `ui_render_matrix_es30` draws every shading mode on every mesh path that way in T0.
- **Pencil and touch:** SDL's pen events (pressure, tilt, roll; hover on the Pencils that have it), the same path as
  Android's styluses; the touch layout, palm rejection and the 1.5x touch scale are Android's.
- **Files:** settings, the log and autosaves in the app's Library; **Save** writes to the app's Documents folder, which the
  Files app shows as *On My iPad > SculptForge*; **Open** shows the system's document picker and keeps a copy of the chosen
  file in the app's storage. No cloud backup of its own, no permissions asked.
- **In the background:** the app saves the sculpt and the preferences, and draws nothing until it is in front again
  (iPadOS ends an app that draws with OpenGL in the background).
- **Left out:** the plugin runtime (iPadOS allows no JIT), the live face feed, the update check (TestFlight says when a
  build is out), the camera and game controllers (SDL's camera and HID support are off: no permission prompts), and
  everything that runs another program (splat training, photo scans, video, the local generator command, the command line).

## The base iPad (floor tier)

The floor is the base iPad: A13 to A16, 3 to 6 GB of memory (4 GB for the A14 iPad most people have). iPadOS gives one app
about half of the memory before it ends it, so about 2 GB there. What that means for the hardware tiers
(core/HardwareTier.h): every base iPad and iPad Air is **Floor** (Standard wants 16 GB), the 16 GB iPad Pro is Standard.

| | base iPad (4 GB) | iPad Pro M4 16 GB |
|---|---|---|
| Plain meshes | to a few million triangles | as the Standard desktop tier |
| Micro levels (Forge Ink detail) | Floor budgets: 512 MB resident cache, the levels past it streamed out | Standard budgets |
| Focus Detail, 1B scenes | no: past the app's memory limit | not yet measured |

The ES 3.0 tier keeps a second copy of every texture buffer (the 2D stand-in), so a Micro level's GPU data costs twice what
it does on the desktop: the memory guard steps the tier down earlier there. These are estimates until the first TestFlight
build runs on a base iPad (the budgets to measure: RSS at a 2M-triangle mesh and at a Micro level 3, and the frame time
while sculpting). Next: the increased-memory-limit entitlement (an App ID capability) would roughly double what fits.

## Testers

TestFlight testers are members first: the **Members** external group in App Store Connect, filled from the member list.
