# Try in 0.10.9: image to 3D out of the box
- With no Hugging Face token set: drop a sketch or character image on the window and press **Generate 3D model** with the defaults. It should run on Hunyuan3D 2.1 and finish without any setup.
- Pick **TRELLIS.2 - Best detail (needs token)** without a token: generation should still work (Hunyuan3D 2.1 runs) and the status line should point to **Preferences > AI**. Then add your free token there and try TRELLIS.2 again.
- The result should arrive as clean quads with 2 subdivision levels (Shift+D steps down to the quad cage). Sculpt it and divide it: does it behave well? How long did "Making it sculptable" take on your computer?
- Try **Remesh: Voxel remesh** and **Off (raw mesh)** too, and say which you would want as the default.
- The first Hunyuan3D run shows a one-time licence notice; hover **Model** to see it again.
