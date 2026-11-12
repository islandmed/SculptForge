---
fixed: **Android**: a mesh shader variant the graphics driver refuses to build no longer leaves the model undrawn without a word: the app logs it once and draws with the next simpler variant, and the log now lists the GL vendor, shader language and limits (reports of a missing 3D model on two phones had no log to go on).
testers:
  - Android phone with a Mali or PowerVR GPU: the starting sphere and the brush thumbnails draw; if not, Help > Copy log to clipboard and send the lines naming "Mesh shader variant".
---
Reported on a Pixel and a Mali-G72 phone. Branch fix/android-gl-variants.
