# Selection sets and morph targets

A selection set is a named snapshot of a Part's mask, face groups, vertex paint, visibility (which faces are
hidden), UVs and shape. Store as many as you like, bring any of them back later, combine them, and use a stored
shape as the target of the **Morph** brush. Sets belong to the Part, travel with the project, and work across its
subdivision levels: store at one level, restore at any other.

## Store a set

Open **Tool > Selection Sets**.

1. Type a name in the field next to **Store** (it suggests "Set 1", then "Set 2" and so on).
2. Tick the channels the set should hold:

   | Channel | What it keeps | Command name |
   |---|---|---|
   | **Mask** | The mask, soft edges included. | `mask` |
   | **Groups** | The face groups. | `polygroups` |
   | **Visibility** | Which faces are shown and which are hidden. | `visibility` |
   | **Vertex Paint** | The vertex colours. | `paint` |
   | **UVs** | The UV layout (only when the Part has UVs). | `uvs` |
   | **Shape** | The surface itself, as a morph target. | `morph` |

3. Click **Store**. Storing again under the same name replaces that set.

**From Visible** is the quick way: it stores the faces shown right now as a set called **Visible** (then
**Visible 2**, ...). Isolate a part with the <kbd>Ctrl</kbd>+<kbd>Shift</kbd> gestures
([Face Groups and visibility](face-groups.md)), click **From Visible**, and you can get back to exactly that view
whenever you need it.

There is no limit on the number of sets. They are kept compactly: masks, vertex paint and visibility store only the
parts that differ from an empty mask, white paint and a fully shown Part, and face groups are run-length coded, so a
mask on a few million vertices usually costs a few kilobytes. Shapes and UVs are stored as they are.

## Restore, rename and delete

Select a set in the list (hover it to see its channels and the level it was stored at), then:

- **Restore** puts its channels back. The shape is not restored here: **Morph to Set** does that, so you can keep
  a set with a shape in it and still restore just its mask. Double-clicking a set restores it too.
- **Rename** edits its name; **Delete** removes it.

The menu next to **From Visible** sets how **Restore** meets what you have now, for the mask and visibility:

| Mode | Mask | Visibility |
|---|---|---|
| **Replace** | The set's mask. | The set's shown faces. |
| **Union** | Masked in either. | Shown in either. |
| **Intersect** | Masked in both. | Shown in both. |
| **Subtract** | Masked now but not in the set. | Shown now but not in the set. |

Every store, restore, rename, delete and combine is one undo step.

A set greyed out in the list no longer fits the Part: its topology changed since (a remesh, Dynamic topology,
Delete hidden). Undo the change, or delete the set.

## Combine sets

Pick a second set in the menu under **Restore**, choose **Union**, **Intersect** or **Subtract** above, and click
**Combine**: a new set ("Head + Arms", "Head & Arms" or "Head - Arms") holds the mask and visibility of the two
combined that way. Your current mask and visibility are not touched until you restore it.

## Subdivision levels

A set remembers the level it was stored at, and restores at any level of the same Part:

- going down, each vertex and face takes the value of the matching one on the level above, as the levels themselves
  do;
- going up, masks and vertex paint are interpolated, face groups and visibility are inherited from the face they lie
  in, and UVs are interpolated across each face;
- a shape travels as its difference from that level's own surface, so the detail of the levels in between stays
  where it is.

So you can store a mask at SDiv 1, sculpt at SDiv 5 and restore the mask there, or keep a high-resolution face group
set and use it at the base cage. Sets survive **Divide**, **Del Lower**, **Del Higher** and **Reconstruct
Subdiv** as long as the level they were stored at still exists.

## Morph targets

Store a set with **Shape** ticked to keep the surface as it is now. Then:

- **Morph Target** makes it the target of the **Morph** brush and picks that brush. Strokes pull the surface back
  toward the stored shape, as much as the brush's strength; <kbd>Alt</kbd> pushes away from it. The set's name
  shows **[morph]** in the list; **Clear Morph Target** turns it off.
- **Morph to Set** moves the whole surface toward the set's shape by **Amount** (1 = all the way, 0.5 = halfway,
  negative values push away), respecting the mask, as one undo step.

Both work at any subdivision level, not only the one the shape was stored at, and with sculpt layers (the change
goes into the active layer). Use them to undo a sculpting session locally, to blend between two versions of a
form, or to keep a neutral pose while you sculpt an expression.

## Projects, files and other apps

Sets are saved in the project with each Part (older versions of SculptForge open such projects and leave the sets
out). To move them between Parts, scenes or tools, use the commands:

```
selection_sets_export path=head.sfsets            # every set of the active Part (name=... for one)
selection_sets_import path=head.sfsets            # into the active Part
```

A `.sfsets` file is a ZIP with a JSON manifest and one small binary file per set. Importing a set that was stored on
a different mesh keeps it, but it shows greyed out and can't be restored until the Part has a level it fits.

**USD** (`export_usd`) writes the sets of each Part inside its Mesh prim: the shown faces of each set as a
`GeomSubset` (`sculptforge_set0_visible`), its face groups as a partition of `GeomSubset`s (`sculptforge_set0_group7`),
and its mask and vertex paint as vertex primvars (`primvars:sculptforge_set0_mask`, `..._color`), with the names in
`sculptforge:selectionSets`. Houdini, Maya, Blender and Omniverse read these as ordinary face sets and primvars, and
SculptForge turns them back into selection sets when it imports the file. UVs and shapes are not written to USD.

**OBJ, glTF / GLB, FBX, PLY and STL** have no place for named selections, so sets are not written to them (OBJ
still writes the current face groups as `g` groups). Export the sets to a `.sfsets` file alongside the model instead.

## Scripting

Every button is a command, so scripts, `sculptforge-cli`, the MCP server and the AI assistant can use sets:

| Command | What it does |
|---|---|
| `selection_set_store name=Head channels=mask,visibility` | Store (or replace) a set. `channels` takes `mask`, `polygroups`, `paint`, `visibility`, `uvs`, `morph` or `all`. |
| `selection_set_restore name=Head mode=union` | Restore; `channels=` restores some of them (add `morph` to restore the shape). |
| `selection_set_rename name=Head to=Face` | Rename. |
| `selection_set_delete name=Head` | Delete. |
| `selection_set_list json=true` | List the sets with their channels and levels. |
| `selection_set_combine a=Head b=Ears op=union into=HeadEars` | Combine two sets into a new one. |
| `selection_set_from_visible` | The quick set from the visible faces. |
| `selection_set_morph_target name=Rest` | Make a set the Morph brush's target (empty name: none). |
| `selection_set_morph name=Rest amount=0.5` | Morph to Set. |
| `selection_sets_export` / `selection_sets_import` | `.sfsets` files. |

In Python (`import sculptforge as sf`), push and pull sets as NumPy arrays:

```python
doc.set_selection_set("Eyes", mask=eye_mask, hidden=hidden_faces)   # store from arrays (one undo step)
doc.selection_sets                                                  # [{'name': 'Eyes', 'channels': [...], ...}]
arrays = doc.selection_set("Eyes")                                  # {'mask': ..., 'hidden': ...} at this level
sf.run(doc, "selection_set_restore", name="Eyes")
```

See [Scripting and automation](scripting.md) for running commands.
