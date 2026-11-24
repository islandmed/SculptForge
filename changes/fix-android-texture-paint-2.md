---
whats_new: **A model never disappears after New Texture on Android**: if a phone or tablet's graphics driver rejects drawing the texture, the model now stays visible without it and the status bar and the Texture Image panel say so. Painting still goes into the texture.
fixed: **Android: the model vanished after Texture Image > New Texture on some Mali tablets.** The first textured draw is now checked for a GL error: on one, the Part draws without its texture (status bar and Texture Image panel say so) instead of not at all. The UV samplers of the textured shader always have a texture bound, and the log names the call behind each GL error (start-up, mesh upload, texture upload, textured draw) and the driver's own message (debug output, ES builds), so **Help > Copy log to clipboard** shows what failed.
testers:
  - Android tablet or phone (Mali first): **Sphere** at 5-6 subdivisions, **Texture Image > Auto UV**, **New Texture** (2K), paint a stroke. Good: the sphere stays visible and the stroke shows. If it shows without the texture, the status bar says the chip rejected it; then **Help > Copy log to clipboard** right after, and send the lines containing "GL error", "GL driver message" or "Texture not shown".
---
Reviewers: Renderer::drawSlot (first textured draw check, texturedFailed fallback), noteGlError (labelled once per call and code),
gl::enableDebugLog (KHR_debug, ES builds or SF_GL_DEBUG=1), dummy UV samplers. tools/dev/check_gles_shaders.py (ctest gles_shaders:
glslangValidator on every program as GLSL ES 3.20, stages linked; t0.yml installs glslang-tools). Test: tests/e2e/texture_rejected.sfui
(gl_limits reject_textured=1). No root cause yet: the vanish does not reproduce on Mesa GLES with his limits.
