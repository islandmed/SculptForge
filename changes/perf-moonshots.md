---
fixed: Orbiting a dense model right after sculpting no longer hitches on slower processors. The lighter copy that is
  drawn while the view moves was rebuilt in batches that a slow CPU took about 160 ms to finish, so the first frames of
  the orbit froze. Batches are now sized to fit a few milliseconds a frame. After a stroke the copy is also brought up
  to date while the view is still, so the next orbit starts with it ready (15M triangles, simulated integrated-graphics
  profile on slow cores: worst orbit frame 166 -> 24 ms).
testers:
  - Windows laptops with Intel UHD / Iris Xe or AMD Radeon integrated graphics (also Apple M1 / M2 base on macOS):
    we need your numbers most. Close other apps, plug in power, and run Help > Benchmark > standard-15m twice: once as
    installed, then again with Live Move, Background Preview and Fast Upload switched on in Preferences >
    Performance. Post both result files with the **Benchmark result** issue form on GitHub and name your graphics
    chip and processor. Good: it finishes without crashing. The second run's stroke stays under the pointer in the
    video.
  - Windows laptops with integrated graphics first, any OS: open a model of 10M+ triangles (or Help > Benchmark's
    head), sculpt a long stroke, then orbit straight away. Good: the view turns smoothly from the first frame (no
    freeze at the start of the orbit), and the model sharpens to full detail when you stop.
---
Orbit-start hitch fix (Renderer::buildProxies batches sized to the frame budget; stale proxies rebuilt on still frames
after a stroke). The experimental toggles shipped in 0.11.0-beta.1; this asks integrated-graphics owners for numbers.
