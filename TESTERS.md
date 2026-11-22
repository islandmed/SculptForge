# Testers: Focus Detail on Windows (very dense Micro levels streamed from disk)
Needs a real Windows 10 or 11 PC (the automated tests run on CI machines without a real graphics card). An SSD helps but isn't required. Tell us how much RAM you have; 16 GB or more is best.
- Start from a head (**File > New from Starter Mesh...**, or your own head scan). Press **Add Level** (Ctrl+D) until it has about 60,000 triangles or more, then press **Tool > Geometry > Add Micro** until the panel says "Streamed: full detail loads where you look and sculpt" (about 300 million micro triangles). How long did that take, and did the app stay responsive?
- Orbit around the head and zoom into an eye, an ear, the lips: does the full detail fill in where you look within a second or two? Any holes, cracks or flickering patches?
- Sculpt where you look with Clay, Standard and Inflate, with varied pen pressure and overlapping strokes. Does each stroke land under the cursor, and feel as quick as on a smaller mesh?
- **File > Save As** a new project in a folder of your own (try a name with an accent or Japanese characters too). Next to the .sfproj there should be a .sfdetail file with the same name. Save again after a few more strokes: is the second save quick?
- Close SculptForge, open the project again, and sculpt where you left off: is every stroke from before there?
- If you have a Linux or macOS machine too, open the same project there (and the other way round: a project saved there, opened on Windows). Does it look exactly the same?
- Keep Task Manager open while you orbit and sculpt: what is the most memory SculptForge used, and how big is the .sfdetail file?
- Tell us your graphics card, CPU, RAM and Windows version, and anything that felt slower than on a smaller mesh.
