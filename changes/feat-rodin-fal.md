---
whats_new: **Rodin 2.5 in Generate 3D**: **Generate 3D > fal.ai > Model** now offers Hyper3D **Rodin 2.5** and
  **Rodin 2.5 Fast**, from an image or a description, with your fal.ai key.
added: **Rodin 2.5 (Hyper3D) via fal.ai** (#123, thanks ElgoogUdiab): **Generate 3D > fal.ai > Model** lists
  **Rodin 2.5** and **Rodin 2.5 Fast**. Image and text both work; the untextured GLB goes through the usual import
  and remesh. Uses your fal.ai key (`FAL_KEY`), nothing else is contacted. Commands:
  `image_to_3d service=fal model=fal-ai/hyper3d/rodin/v2.5`, `text_to_3d service=fal model=fal-ai/hyper3d/rodin/v2.5`
  (other fal models keep Hunyuan3D 3.1 Rapid for text).
testers:
  - Any OS with a fal.ai key: Generate 3D > fal.ai > Model > Rodin 2.5, drop teddy-sketch.png, Generate: a sculptable
    teddy arrives (quad remeshed). From text tab, "a cartoon fox sitting": a fox.
  - Rodin 2.5 Fast: same, faster and coarser.
---
In-house re-implementation from the description of outside PR #123 (network code from outside PRs is rebuilt, not
merged). fal.ai only; no direct Hyper3D API, no new hosts, no built-in keys.
