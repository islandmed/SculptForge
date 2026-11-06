---
whats_new: **Hyper3D Rodin, direct**: **Generate 3D > Hyper3D Rodin (direct)** runs Rodin Gen-2.5 on Hyper3D's own service with your own Hyper3D key, from an image or a description, in five tiers from Extreme Low to Extreme High. It stays off until you add your key, which is kept in the system keychain; your image or description goes to Hyper3D only, and generations are billed to your Hyper3D account. Rodin through fal.ai stays as it was.
added: **Hyper3D Rodin (direct)** in Generate 3D and the `image_to_3d` / `text_to_3d` commands (`service=hyper3d`,
  `model=` a Gen-2.5 tier): Hyper3D's own API with your key (system keychain only; no environment variable, no
  shared or trial key). One pinned https host for the API, the key sent nowhere else; the GLB only from Hyper3D's
  domains, capped at 200 MB and checked before import, kept in a private temporary folder deleted after import;
  polling backs off to 30 s. Studios: provider id `hyper3d` in the policy, one audit log line per generation.
  Thanks to ElgoogUdiab for suggesting it (#123); re-implemented in house from Hyper3D's public API documentation.
testers:
  - Any OS, with your own Hyper3D key: Generate 3D > Hyper3D Rodin (direct), tier Gen-2.5 Low, paste the key, Save
    settings, generate from an image and from text; the model opens and remeshes. Restart: the key field is empty but
    "Using your key from the system keychain" shows, and generating still works.
  - Without a key: Generate says to add your Hyper3D key and nothing is sent.
  - Preferences > AI > Enable AI features off: Generate 3D can't send anything.
---
Re-implemented from the PR #123 description and docs.hyper3d.ai only; the PR's code was not read or run.
