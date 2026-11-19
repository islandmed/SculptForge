---
whats_new: **Layers round trip**: shape keys sent from Blender arrive as sculpt layers (names, values, order kept), and layers go back as shape keys; glTF, FBX and USD files exported from SculptForge open with the same layers.
added: **Sculpt layers survive a round trip.** Blender shape keys sent with the add-on arrive as sculpt layers (name, value
  and order kept, offsets placed by the object's transform) and return as shape keys; glTF morph targets, FBX blend shapes
  and USD blend shapes already opened as layers, and a SculptForge export now opens with the same layers, in the same order,
  with the same weights and offsets within 1e-5 (tested for each format, the 52 ARKit names included).
fixed: **Layer names and vertex order in USD and glTF.** A USD export renamed layers with spaces or other characters ("Smile 2" came back
  as "Smile_2"), and a glTF export with UV seams opened with its vertices renumbered; both now come back as they were.
testers:
  - Blender with the add-on: add two shape keys to a mesh, Send to SculptForge; the Layers panel lists them with their names and values. Sculpt a key's layer, send back; the shape key follows.
  - Export a layered sculpt as GLB, FBX and USD, open each: the same layers, names and weights are there.
---
Review notes: Blender -> SculptForge shapes file (manifest `shapes`, docs/blender.md), SculptForge side in
BlenderBridge.cpp (setLayersFromShapes), add-on side shape_key_offsets / write_shapes. Invariant tests:
tests/test_layers_roundtrip.cpp, tests/blender/test_exchange.py, tests/blender/test_in_blender.py (needs Blender).
