---
whats_new: **Shared script folders**: set `SCULPTFORGE_SCRIPT_PATH` (or a studio profile's `scriptPaths`) and the `.py` files in those folders are listed under **File > Script**, marked with their folder.
added: **`SCULPTFORGE_SCRIPT_PATH`** (Python scripts): folders of `.py` files listed under File > Script after the Scripts
  folder's own (which wins a shared name), marked with their folder, like `SCULPTFORGE_BRUSH_PATH` for brushes; a
  studio profile can set it with `scriptPaths`. They run only when clicked and can never be set to run at start-up;
  a policy that says Python is never allowed hides them. Requested by a community member.
testers:
  - Linux/macOS: `SCULPTFORGE_SCRIPT_PATH=/some/scripts:/more sculptforge`, Preferences > Scripts on; File > Script lists them with the folder's name; a click runs one.
  - Windows: the same with `;` between folders (set it in System Properties > Environment Variables, then restart).
  - Preferences > Scripts > Run at start-up lists only the Scripts folder's scripts.
---
A folder that is missing is skipped with one console line. Sub-folders are not read (the brush path reads them; the Scripts folder never has).
