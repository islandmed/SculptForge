---
whats_new: "**Move** honours **Front faces only**: with View falloff on, Move now leaves the far side of a one-piece model alone when Front faces only is on."
fixed: "**Move** ignored **Front faces only** (a member's report): with View falloff on, a Move drag on a one-piece head also pulled the back of it, and Connected only could not help there. Move now takes only the surface facing the camera when Front faces only is on, on plain meshes and Micro levels alike; with it off nothing changes. The guide's View falloff row says which to use."
testers:
  - "Any OS: Sphere or a head, Draw > View falloff on, Move with a big brush over the front: the back moves along (as before). Turn Front faces only on (Brush panel): the same drag moves the near side only; the back stays."
  - "Any OS: the same on a Micro level, and with X symmetry: nothing facing away from the camera moves. Front faces only off: Move behaves as before."
---
Reviewers: Sculptor::beginGrab (src/core/Sculpt.cpp) drops grab vertices whose normal faces away from the view, as dabSingle does; MicroStroke::beginGrab (src/core/MicroSculpt.cpp) gives such grid points weight 0 (MicroDab::frontFaces + viewDir, replayed by MicroHistory). App::strokeTo passes frontFacesOnly_ into the grab and turns Live Move's preview off with it (the preview ghost reaches the whole region). tests/test_move_front_faces.cpp.
