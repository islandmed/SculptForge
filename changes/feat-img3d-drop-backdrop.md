---
highlight: Image to 3D: remove the flat background plane a generated model stands on, in one click (thanks fenix on Discord)
whats_new: **Remove flat background**: Image to 3D drops the flat plane a picture's background often turns into
  (a wall behind or a ground under the subject) before the model lands. On by default in **Generate 3D**; Undo
  brings a removed plane back. Thanks fenix on Discord.
added: **Generate 3D > Remove flat background** (on by default): a thin wall behind or ground under the subject,
  covering most of the model's bounds, is removed from the generated mesh before remeshing, as its own undo step;
  a relief or a coin on its own is kept. `image_to_3d` / `text_to_3d` take `remove_background=false`, and the new
  `remove_flat_background` command does it on the active Part. Requested by fenix on Discord.
testers:
  - Any OS: Generate 3D from a photo with a plain wall behind the subject (Hunyuan3D 2.1); the result has no flat
    plate behind it and the status says "Removed a flat background plane". Ctrl+Z (twice with Quad Remesh) brings it back.
  - Turn **Remove flat background** off and generate again: the plane stays.
  - Tool > Relief from an emblem image, then run `remove_flat_background` in the console: "No flat background plane found".
---
Core: src/core/Backdrop.{h,cpp}. Tests: tests/test_backdrop.cpp (wall, ground, joined ground, relief, coin, plane alone).
