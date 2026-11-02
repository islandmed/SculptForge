# Proof mode results (`sculptforge-benchmark/1`)

Proof mode (**Help > Benchmark**, `sculptforge --benchmark`; user guide: [Performance](guide/performance.md))
measures how responsive sculpting is on a given computer and writes the result as one JSON file, next to an MP4
of the run with the live numbers burned in. This page describes that file, how to submit one, and how the
website's [benchmarks page](../website/benchmarks/index.html) lists them.

## Standard scenes (what the website lists)

`sculptforge --benchmark standard-15m` (or `standard-33m`, or `standard` for both; Help > Benchmark offers the
same choice) runs the **standard scene**: a procedural head (`makeBenchHead` in `src/core/Benchmark.cpp`: skull,
brow, eye sockets, nose, cheekbones, jaw, ears and neck as a displacement of a cube-sphere, then divided into
subdivision levels like the spheres below) of about 15.0 or 33.2 million triangles. It is built from a formula,
not loaded from a file, so every build on every platform sculpts the same surface. The stroke set
(`settings.stroke_set` = `standard/1`; changed strokes get a new value) is the figure eight below, once per brush,
on the same mesh in this order:

| Phase | Brush | Radius | Strength |
|---|---|---|---|
| `clay small` | Clay | 25 px | 0.35 |
| `clay medium` | Clay | 60 px | 0.35 |
| `clay large` | Clay | 120 px | 0.35 |
| `move` | Move | 80 px | 0.25 |
| `smooth` | Smooth | 60 px | 0.5 |
| `240 px brush` | Clay | 240 px | 0.35 |
| `orbit` | (the view turns once in 3 s) | | |

Each run adds `worst_stall_ms` (the longest frame, or lifting the pen if that took longer, over all phases; per
phase too), `gpu_buffers_mb` (the most the renderer's vertex and index buffers held, an estimate that works on
every GPU) and, where the driver reports free graphics memory (`GL_NVX_gpu_memory_info`, `GL_ATI_meminfo`),
`vram_used_mb` (how far it dropped from before the mesh was built; on unified-memory GPUs this is system memory
and other programs move it). `peak_ram_mb` is as below. The JSON's `settings.scene` names the scene.

The sphere sweep below is what `sculptforge --benchmark` with no scene name runs.

## What is measured

For each mesh size (default 5, 20 and 33 million triangles; 100 million when the free memory exceeds about
150 bytes per triangle with a quarter to spare; other features can add sizes through `proofExtraSizes()` in
`src/app/ProofMode.h`, e.g. a 300 million triangle micro-displaced mesh):

1. A sphere with subdivision levels is built (`makeBenchSphere`, as `--bench` does) and framed.
2. **stroke**: a figure eight across the model, three times round in `stroke_seconds`, with the Standard brush
   at 60 px (X symmetry on, as the app starts). Pointer samples are generated at 240 Hz and passed to
   `App::onMouseDown` / `onMouseMove` / `onMouseUp`, the handlers SDL's mouse and pen events call, once per
   frame (the samples that arrived since the last frame, in order), so the dab queue, its time budget, the
   undo step, the Sculpt Timeline and the renderer all run as when a person sculpts.
3. **large brush stroke**: the same with a 240 px brush.
4. **orbit**: the view turns once in 3 seconds.

Per frame, at the moment the frame is handed to the display (just before `SDL_GL_SwapWindow`):

- `frame_ms`: the time since the previous frame was handed over.
- `lag_ms`: from the time of the pointer sample the newest drawn dab belongs to (the nearest sample to it) to
  now. The display's scan-out and the compositor add to this on a real screen (about one refresh).
- `lag_px`: the distance along the stroke path from that sample to the newest sample (where the pointer is).

Vertical sync is off unless `settings.vsync` is true. Recording the video (`settings.video`) reads every frame
back through pixel buffers and encodes on another thread; it costs about 1 to 2 ms per frame. The machine is not
otherwise controlled: other programs running at the same time slow it down.

## The file

```json
{
  "schema": "sculptforge-benchmark/1",
  "sculptforge": { "version": "0.9.7", "commit": "930f814" },
  "date": "2026-11-01T05:46:51Z",
  "hardware": {
    "cpu": "Apple MacBook Pro (16-inch, M1 Pro, 2021)",
    "logical_cores": 10,
    "ram_gb": 30.9,
    "os": "Arch Linux ARM (Linux 7.1.13-3-1-ARCH aarch64)",
    "gpu": "Apple M1 Pro (G13S C0), OpenGL 4.6 (Core Profile) Mesa 26.1.8",
    "framebuffer": [1920, 1080],
    "ui_scale": 1
  },
  "settings": {
    "vsync": false, "video": true, "pointer_hz": 240, "stroke_seconds": 6,
    "input": "...", "lag": "..."
  },
  "runs": [
    {
      "label": "20M", "triangles": 20971520, "vertices": 10485762,
      "build_seconds": 9.1, "ram_mb": 2410, "peak_ram_mb": 2702,
      "phases": [
        {
          "name": "stroke", "brush_radius_px": 60, "seconds": 6.0, "frames": 512, "fps": 85.3,
          "frame_ms": { "p50": 10.8, "p95": 19.0, "p99": 26.4, "max": 41.0, "mean": 11.7 },
          "lag_ms":   { "p50": 18.0, "p95": 35.0, "p99": 48.0, "max": 70.0, "mean": 20.1 },
          "lag_px":   { "p50": 12.0, "p95": 25.0, "p99": 33.0, "max": 48.0, "mean": 13.0 },
          "dabs": 1530, "pointer_samples": 1440, "path_px": 4300, "pointer_speed_px_s": 716
        },
        { "name": "large brush stroke", "brush_radius_px": 240, "...": "..." },
        { "name": "orbit", "seconds": 3.0, "frames": 600, "fps": 200.0, "frame_ms": { "...": "..." } }
      ]
    }
  ],
  "video": "sculptforge-benchmark-20261031-224651.mp4",
  "notes": "only present when something went wrong or the run was stopped"
}
```

Field notes:

- `sculptforge.commit` is the commit the build was configured from (`unknown` for builds outside git).
- `hardware.cpu` is the processor's name, or on ARM Linux the machine's model from the device tree.
- `runs[].ram_mb` is the resident memory at the end of that size's phases, `peak_ram_mb` the most during them
  (Linux; 0 elsewhere).
- Percentile objects always have `p50`, `p95`, `p99`, `max` and `mean`, in milliseconds or pixels.
- New fields may be added; readers should ignore fields they don't know. An incompatible change gets a new
  `schema` value.

## Submitting a result

1. Run proof mode on a quiet machine (close other heavy programs) with a release build or an official package.
2. Check the file: `python3 tools/benchmark/submit.py check mine.json` validates it and prints a summary.
3. Add it to the list: `python3 tools/benchmark/submit.py add mine.json --name "Your name or handle"
   [--machine "the computer"] [--video https://...]` appends a compact entry to `website/benchmarks/results.json`. Open a pull request with
   that change, attach the full JSON (and a link to the video, e.g. uploaded to YouTube) in the description.
   Or open a **Benchmark result** issue (`.github/ISSUE_TEMPLATE/benchmark_result.yml`) and paste the JSON;
   a maintainer checks it with `submit.py check` and adds it. Only SculptForge results are listed: we don't run,
   test against or publish other sculpting applications' numbers or output.

The benchmarks page (`website/benchmarks/index.html`) reads `results.json` and shows one row per mesh size and
submission: frame time and lag percentiles per phase, hardware, version.
