# src/render

Drawing: the OpenGL renderer, the camera, and the CPU-side pieces of drawing that tests can check without a GPU.
It uses `core` (meshes, splats, micro levels) and never includes anything from `app`: the app owns the window and the
GL context and hands them to the renderer.

OpenGL 3.3 core on desktop, OpenGL ES 3.2 on Android (`SCULPTFORGE_GLES`). `GL.cpp` is a small loader that covers
exactly the functions we use, so there is no generated loader to depend on. Shaders are strings inside the `.cpp`
files that use them.

| File | What it does |
|---|---|
| `Renderer` | Draws the scene: meshes, wire and face-group overlays, PBR viewport shading, splats, masks, vertex paint; settings in `RenderSettings`; offscreen capture for thumbnails and screenshots |
| `Camera` | The orbit/pan/zoom camera and the standard views. In `sculptforge_appshared`, so tests use it |
| `Clusters` | The CPU side of mesh drawing: the compact 12-byte GPU vertex, clustering, culling. Kept apart from GL so `tests/test_render_clusters.cpp` can check it. Also in `sculptforge_appshared` |
| `Ghost`, `ScreenStroke` | Predictive stroke preview: dabs the solver has not placed yet are drawn as a displacement while a worker thread catches up. `ScreenStroke` is research, off by default (`SF_SCREEN_STROKE=1`) |
| `MicroRenderer`, `MicroFocus`, `MicroTilePages` | Draws micro-displacement levels (`core/Micro.h`) without ever building their triangles, including streaming from a Focus Detail file and adaptive tiles |
| `Occlusion` | View-space ambient occlusion and cast shadows, one pass after the view is resolved |
| `UvView` | The UV Editor's 2D view, drawn on the GPU into an offscreen texture |
| `BufferUpload.h` | How buffers get to the GPU |

## Rules

- No UI code, no SDL window handling (only SDL's log), no file or network access.
- A change to what is drawn needs a look at it: run the app offscreen with a UI test script that takes screenshots
  (`--ui-test script.sfui --ui-out DIR`, see [tests/README.md](../../tests/README.md)) and check them, at the normal and
  the 2x scale.
- Performance counts: a frame at 5 to 15 million triangles must not get slower. Run the benchmarks in
  [bench](../../bench/README.md) and the `--bench` option of the app for anything on the draw path.
- Keep the data the GPU holds compact and uploads incremental (only changed vertices or clusters).
- Anything that can be computed without GL (layout, culling, sorting, packing) belongs in a CPU function with a test.

## Test it

`tests/test_render_clusters.cpp`, `tests/test_camera.cpp`, the `tests/test_micro*.cpp` files, and the UI scripts in
`tests/ui` that take screenshots; `tests/render_matrix/matrix.sfui` shows the viewport in its main modes. How to run
them: [tests/README.md](../../tests/README.md).
