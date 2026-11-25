---
fixed: **Android**: on graphics chips with a small buffer limit (some Mali tablets), a model with more than about 65,000 triangles no longer disappears, with or without a texture.
testers:
  - Android with a Mali GPU (Mali-G72 first): Sphere at 5-6 subdivisions, Texture Image > Auto UV > New Texture, paint a stroke; the sphere stays visible and the paint shows.
---
The face group sampler had no texture bound when the mesh was past the GPU's buffer-texture limit (65536 texels on a Mali-G72); the driver rejects a draw with an empty buffer sampler. It now binds a one-texel stand-in. Regression: tests/e2e/texture_mali_buffers.sfui (gl_limits texels=65536; empty_buffer_samplers and texbuffer_over_limit stay 0).
