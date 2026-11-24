---
whats_new: **Generate 3D** now asks one question under **Result**: **Sculpting base** (moderate density, clean quads,
  made the moment you pick a candidate; the model as generated stays hidden as a Reference) or **Reference** (high
  fidelity, exactly as generated). Every Part an AI made carries a provenance tag (provider, model, date, app version,
  pass): hover it in the Parts list, it is saved in projects and kept in glTF, USD and OBJ exports.
highlight: Generate 3D: one clear choice, and AI output tagged with its provenance
added: **Generate 3D > Result**: one clear choice for what the picked candidate becomes. **Sculpting base: moderate
  density, clean quads** (the default) remeshes the pick at once (Voxel Remesh, then Quad Remesh, about 32768 quads with
  one subdivision level) and keeps the model as generated hidden as the Reference for Project Detail; **Reference: high
  fidelity** keeps the pick exactly as generated. The remesh knobs (Then Quad Remesh, Resolution, Keep a high-detail
  copy) moved under **Advanced**; **Use as sculpting base** remains to do it again after Undo or for a pick made as a
  reference. Existing services only; nothing new is sent anywhere.
added: **AI provenance tag**: a Part made by Generate 3D (or `image_to_3d` / `text_to_3d`) records the public provider
  (Hugging Face, fal.ai, Meshy, Hyper3D, Tripo, ComfyUI, your generator command), the model when it is a built-in choice,
  the local date, the SculptForge version and the pass (generated, sculpting base, reference); never a host, URL, path,
  workflow or key name. It shows in the Parts list
  tooltip and `scene_overview`, follows Duplicate, Split and linked copies, is saved in projects (`ai_provenance`, an
  optional per-Part key older versions ignore) and kept on export where the format has a place for it: glTF node
  `extras.sculptforge_ai`, USD prim `customData` (`sculptforge_ai`), an OBJ comment under the object line. FBX, PLY
  and STL have no place for it. Nothing in the tag leaves your computer on its own.
testers:
  - Any OS: Generate 3D, pick a candidate with Result on Sculpting base: the remesh starts at once, the status line
    reports "Sculpting base ready", and a hidden "Reference" Part sits below it. Undo brings the model as generated back.
  - Any OS: Result on Reference, pick: the Part stays as generated (no remesh), the status line says so.
  - Any OS: hover the generated Part in the Parts list: the tooltip ends with "AI: <model>, <provider>, <date>, SculptForge <version> (<pass>)".
    Save a project, reopen it: the line is still there. Export .glb, .usda and .obj and look inside for "sculptforge_ai";
    export a Part you sculpted yourself: no tag.
---
Reviewers: areas/ai.md backlog #2 and #3 (member TETSUYA cites AI-tool integration). The role of the tag is not part of
the undo history (Undo after a sculpting base leaves "sculpting base" on the raw mesh until the next pass); the mesh
itself undoes as before.
