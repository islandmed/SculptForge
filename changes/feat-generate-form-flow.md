---
whats_new: **Pick the strongest form**: **Generate 3D** makes up to three candidates at once and shows each as a
  thumbnail over a black silhouette. Pick one and it becomes a Part; **Use as sculpting base** then rebuilds it as a
  clean, even mesh to sculpt on, and keeps the model as generated hidden for Project Detail.
highlight: Generate 3D: pick the best of three, then one click to a clean sculpting base
added: **Generate 3D candidates**: **Candidates** (1-3) makes several shapes in one go, side by side as a shaded
  thumbnail over a flat black silhouette, so the strongest form is picked before any sculpting. Free and local
  services start at 3; fal.ai, Meshy and Hyper3D start at 1 (each candidate is one generation on your own key, and
  the window says how many are billed). The pick becomes a Part (only an untouched untitled scene is replaced); the
  others are discarded or, with **Keep the others as hidden Parts**, kept hidden. "Same shape as A" when a service
  returns one shape twice.
added: **Use as sculpting base** (Generate 3D): one click after picking runs Voxel Remesh, then Quad Remesh
  (**Then Quad Remesh**, on by default), for a clean, even mesh with one subdivision level. **Keep a high-detail copy
  (hidden)** keeps the model as generated as a hidden Part below it; **Project Detail from the high-detail copy**
  brings its detail back after a Divide.
changed: Generate 3D no longer remeshes as part of generating: generate for form, then Use as sculpting base for
  topology. The Remesh drop-down is replaced by the Sculpting base section; the CLI's image_to_3d / text_to_3d
  (remesh=quad|voxel|false) are unchanged.
testers:
  - Any OS, Hugging Face (free): drop a sketch, Generate 3D model with Candidates 3; three pictures appear side by side
    within a few minutes, each a thumbnail over a black silhouette. Pick one: it lands as a Part named Generated.
  - Then Use as sculpting base: the status says "Sculpting base ready: remeshed to ... quads (2 subdivision levels)";
    the Parts list has a hidden "Reference" right below it.
  - With a sculpt already open: generate and pick; your sculpt stays and the model is a new Part beside it.
  - Choose fal.ai: Candidates shows 1; slide to 3 and the window says "3 are billed".
  - Preferences > AI off: the Generate 3D window closes, and dropping an image doesn't open it.
---
Clean room: our own words and layout; no third-party names. Variations are N ordinary requests to the same service
on the same key (no new host, key or server). UI test tests/ui/generate_candidates.sfui runs on a stub provider
(generator_stub); http_requests stays 0.
