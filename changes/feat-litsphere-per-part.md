---
whats_new: **A material per Part**: in the material picker (<kbd>M</kbd>) or **Render > Materials**, switch **Scene | This Part** and pick a Lit Sphere for the active Part alone: chrome eyes in a clay head. The **Scene** tile puts it back on the scene's material; saved in the project. *Requested by members*
highlight: A Lit Sphere material per Part (requested by members)
added: **A Lit Sphere material per Part** (requested by members): the material picker and **Render > Materials** have a
  **Scene | This Part** toggle. **Scene** (the default) is the one material every Part shares, as before; **This Part** gives
  the active Part a Lit Sphere or Studio of its own (right-click any material: **Use for this Part only** / **Use for the
  whole scene**), the **Scene** tile puts it back, and **Material > This Part > Scene material** does the same. Hover
  previews the material on that Part alone. Saved in the project (a custom Lit Sphere a Part uses travels inside it);
  duplicates, split pieces and linked copies inherit it; the Parts list tooltip names it. PBR viewport shading, vertex paint
  and Unlit work as before. Scripts, Python and the AI assistant: `part_material material=Chrome|studio|scene [index|name]`.
testers:
  - Any OS: two Parts (Mesh > Add a sphere); select one, press M, click This Part, click Chrome; only that Part turns chrome. Save, reopen; it stays chrome and the other Part keeps the scene's material.
  - Any OS: with This Part on, hover materials in the picker; only the active Part previews them; Esc puts it back.
  - Any OS: Render > Materials > Viewport Shading > PBR: Parts shade PBR as before (Lit Spheres don't apply); back on Lit Sphere the Part's own material returns.
member_request: true
---
Scene | This Part is a session toggle that starts on Scene (Classic defaults unchanged); the Library's Material tab keeps setting the scene's material.
