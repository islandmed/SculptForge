---
whats_new: **Organise your Lit Spheres**: right-click one of your own in the material picker, **Render > Materials** or
  the Library and choose **Rename...** or **Move to folder**; sub-folders of the Lit Spheres folder show as headings.
  Drop an image onto the Library's Material tab to load it. Search for "material capture" finds them too.
added: **Rename and organise your Lit Spheres** (#124, thanks Pumaman101): **Rename...** and **Move to folder**
  (existing folders, **New folder...**) in the right-click menu of the material picker, **Render > Materials** and the
  Library's Material tab rename or move the image in the Lit Spheres folder; a name in use is refused, nothing is
  overwritten, and projects that use a renamed one still find it (by its pixels). Sub-folders of the Lit Spheres
  folder (one level) are headings in all three lists. Pack, studio-folder and project-only Lit Spheres show the items
  greyed out with the reason. Scripts: `rename_litsphere`, `move_litsphere`.
added: Dropping an image onto the Library's Material tab loads it as a Lit Sphere (#124); dropped on the canvas it
  does what it did before.
changed: The command search and the Library filter also find Lit Spheres by "material capture" and "material image".
testers:
  - Any OS: Render > Materials > Load Lit Sphere... a PNG; right-click it > Rename..., type a name another material
    has: a red message, nothing changes; type a new name + Enter: the list and the file in Documents/SculptForge/Lit
    Spheres show the new name; reopen a project saved with it: same look, no duplicate in the list.
  - Right-click > Move to folder > New folder... "Metals" + Enter: a Metals heading in the picker (M), the palette and
    the Library's Material tab; the file is in Lit Spheres/Metals.
  - Windows: drag a JPG from Explorer onto the Library's Material tab: it loads and becomes the material.
---
Issue #124 (Pumaman101). Brief: BRIEF.md (not committed).
