---
added: **iPad (early, for testers)**: SculptForge builds for iPadOS 16 and later, with the Apple Pencil's pressure and tilt,
  the touch layout, saving to *On My iPad > SculptForge* and opening through the Files picker (docs/ipad.md). Members get
  the TestFlight builds, by invitation. On the iPad's graphics (OpenGL ES 3.0) face group colours, the texture preview and baked
  normal maps don't show yet, and the panels say so.
testers:
  - Desktop, any OS: nothing changes; the Groups switch and a textured Part look as before.
  - Linux with SF_GLES_LEVEL=300 (the iPad's drawing path): sculpt, Micro levels, Focus Detail and splats draw as usual;
    turning on Groups shows the note under the switch and once in the status line.
---
The ES 3.0 tier (render/GL.cpp) is the iPad's renderer and runs on any driver with SF_GLES_LEVEL=300; T0 runs the render
matrix that way (ui_render_matrix_es30). Without the variable desktop and ES 3.2 draw exactly as before.
