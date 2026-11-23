---
whats_new: **Texture painting on Android**: a Part with many triangles can now show its texture on phones and tablets whose graphics chips have small texture buffers (it went dark and strokes left nothing). Where a chip still cannot show it, the status bar and the Texture Image panel say so.
fixed: **Texture painting on Android GPUs with small texture buffers** (Mali, 65536 texels): a Part over about 21,800 triangles got no UVs on the GPU, so a new texture showed nothing and strokes left no paint, and the mesh was re-sent to the GPU every frame. The UVs now go in a 2D texture past the limit, nothing is re-sent, a mesh past even that says "this graphics chip can't show the texture" in the status bar and the Texture Image panel, and the log names the GL call behind any error.
testers:
  - Android tablet or phone (any Mali, Adreno or PowerVR): **Sphere** at 5-6 subdivisions (60k+ triangles), **Texture Image > Auto UV**, **New Texture** (2K), paint a stroke: the sphere stays visible and the stroke shows. Roughness and Metallic channels on: still visible.
  - Same device, a 200k+ triangle Part: either it shows the texture, or the status bar says this graphics chip can't (never a dark, empty view). **Help > Copy log to clipboard** and send it if the model still disappears (look for a "GL error ... after" line).
---
Reviewers: Renderer::uploadFaces (UV 2D texture past GL_MAX_TEXTURE_BUFFER_SIZE, uvsWanted/uvsLost so no per-frame upload),
uploadTangents guard, GS uUvWidth. Tests: tests/e2e/texture_small_gpu.sfui, matrix cells "textured".
