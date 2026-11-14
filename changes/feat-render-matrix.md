---
added: **Tooling**: a render matrix (`tests/render_matrix`, test `ui_render_matrix`) draws a model offscreen in every shading mode (Lit Sphere, PBR,
  flat, wire) on every mesh path (plain, dense Micro, Focus Detail, and the last two past a lowered GPU buffer limit) and fails, naming the cell,
  when the model is not in the picture. `sculptforge --bench` with `SF_BENCH_RENDER_CHECK=1` does the same on the real GPU (nightly).
---
Test-only; no behaviour change. New test steps `gl_limits` and `render_cell` (src/app/UiTestRender.cpp).
