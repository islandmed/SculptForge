---
added: **Python package: methods for the common steps**: `sf.Document.open(path)`, `doc.save(path)` by file name
  (project, OBJ, GLB/glTF, USD, FBX, PLY, STL), `doc.stroke(brush, points)` with the path as a NumPy array,
  `remesh`, `decimate`, `subdivide`, subdivision levels (`divide()`, `subdivision_levels`, `subdivision_level`),
  `clear_mask`/`invert_mask`, `add_part`/`rename_part`/`remove_part`, `sf.command(name)`. Each runs the same command as
  the command line, with the same result; type hints throughout.
testers:
  - Any OS with the package installed: run examples/python/procedural_asset.py out, then open out/procedural.glb in the
    app: a ball with a ring of bumps above a smooth band and a crease along the band's top edge.
---
