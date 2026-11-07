---
whats_new: **Creature bones**: **Append > Insert on the model > Creature bones** is a built-in set of original
  fantasy creature bones (skull, jaw, teeth, horns, spine, ribs, limb and wing bones, claws, tail): pick one
  and drag it onto your model, no file needed. Thanks @kenttumanda123-netizen (#154).
highlight: Creature bones: 13 original starting pieces for building creatures, in Insert sets (thanks @kenttumanda123-netizen, #154)
added: **Creature bones** (Append > Insert on the model, #154): a built-in insert set of 13 original fantasy creature
  bones (Skull, Jaw, Fang, Blade tooth, Swept horn, Spiral horn, Claw, Vertebra, Tail vertebrae, Rib, Limb
  bone, Wing bone, Wing finger), clean closed quad meshes for dividing and sculpting, generated in-house by
  tools/creature-bones. Commands: `insert_set_items set=builtin:creature-bones`, `insert_mesh set=builtin:creature-bones
  index=<n>`. Translated piece names. Thanks @kenttumanda123-netizen.
testers:
  - Any OS: Append > Insert on the model > Creature bones; the Insert set window shows 13 thumbnails. Click Skull, drag
    on a sphere: a skull stands on the surface. Try a Claw with Symmetry X on: two mirrored claws. Ctrl+Z removes them.
  - Windows: the set works on a fresh install (it's written to %APPDATA%\SculptForge\SculptForge\insert-sets).
  - Preferences > Interface > Language Spanish or Chinese: the menu item, window title and tiles are translated.
---
Pieces are designed from distance fields (tools/creature-bones/pieces.py), meshed with surface nets and
quad-remeshed by sculptforge-cli; build.sh refuses a piece that is open, non-manifold or in several parts.
