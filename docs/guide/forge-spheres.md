# Forge Spheres

Forge Spheres (ZBrush users: ZSpheres) block out an organic base mesh from connected spheres: start from one root sphere, grow a torso, a head,
limbs, fingers, a tail or branches from it, set the proportions, look at the skin over them, then turn it into a
mesh you sculpt as usual. It is the quickest way to get the structure of a character or creature before any detail.

> **ZBrush:** this works like ZBrush's ZSpheres with Adaptive Skin: Draw, Move and Scale on the spheres, Alt+click to delete,
> **A** to preview the skin, and **Make Mesh** in place of Make Adaptive Skin. Rotate mode, ZSketch, the
> Topology and Rigging uses of ZSpheres and the link-sphere (magnet) tricks are not there.

## Make a Forge Sphere Part

**Tool > Forge Spheres > New Forge Sphere** adds a Forge Sphere Part holding one root sphere at the centre and makes it active.
It sits in the Parts list like any other Part, so you can block out a creature next to a model you already
have.

While a Forge Sphere Part is active, dragging on it edits the spheres instead of sculpting. Dragging the background
still navigates. The brush ring is hidden; the sphere under the cursor gets an outline instead.

## Edit the spheres

Pick the mode in **Tool > Forge Spheres**:

| Mode | On a sphere | On a link |
|---|---|---|
| **Draw** | Drag to grow a new sphere from it. It starts on the surface where you pressed; how far you drag sets its size (a click makes one half the size of its parent) | Click to add a joint halfway along the link, and drag to place it |
| **Move** | Drag to move it across the screen. The links stretch to follow | Moves the sphere at the end of the link |
| **Scale** | Drag up or right to make it bigger, down or left to make it smaller | Scales the sphere at the end of the link |

In every mode, <kbd>Alt</kbd>+click a sphere to delete it together with everything that grows from it. The root
sphere (orange) always stays.

**Symmetry** (<kbd>X</kbd>) mirrors every edit: grow an arm on the right and the left one grows with it, move a
leg and its twin moves too. A sphere on the centre line stays on it.

Every drag is one step of the Part's undo history, so <kbd>Ctrl</kbd>+<kbd>Z</kbd> takes back the last sphere
you added, moved or scaled.

> **Tip:** build the structure first with few spheres (one per joint), set the proportions with Move and Scale,
> and add joints on links only where a limb needs to bend or swell.

## Pointer only

Everything also works with the pointer alone, without a key held (#8):

- **Drag places child**: with it on, a Draw drag on a sphere makes a new sphere half its size and moves it away
  from its parent as you drag, so one drag places the next joint of a limb. With it off, hold <kbd>Ctrl</kbd> while
  you drag for the same.
- **The selected sphere**: the sphere you last clicked or added is outlined in orange. **Delete Selected** deletes
  it and everything growing from it, **Insert Between** adds a joint halfway to the sphere it grows from, and **Set
  as Root** makes it the root: the armature keeps its shape and links and grows from there, so the skin doesn't
  change.
- **Right click** a sphere (a click, not a drag: a right drag still turns the view) for a menu with **Delete
  Sphere**, **Insert Between** and **Set as Root**.

Each is one undo step, and Symmetry mirrors Delete and Insert. From a script: `zsphere_set_root index=3`.

## Preview the skin

**Preview** (<kbd>A</kbd>) shows the skin over the spheres instead of the spheres themselves, so you can judge the
volumes. The skin is a smooth union of the spheres and the cones between them, with the joints filled out a little.
Turn Preview off to edit the spheres again; while it is on, clicks on the model do nothing.

## Make Mesh

**Make Mesh** builds the skin as a new Part right below the Forge Sphere Part and makes it active:

- It is watertight and made of quads, ready for the brushes, [Dynamic topology](dynamic-topology.md),
  [Divide](subdivision-levels.md) or [Quad Remesh](retopology.md).
- Each link gets its own [face group](face-groups.md), so you can isolate or mask a limb straight away.
- **Resolution** sets how dense it is: cells across the model's longest side. 128 suits a blockout; raise it for
  thin fingers or tails that come out too thin or broken.

The Forge Sphere Part is hidden and kept. To change the proportions later, show and select it, edit the spheres and
Make Mesh again. The armature is saved with the project.

> **Tip:** for clean edge flow, run [Quad Remesh](retopology.md) on the new mesh, then Divide and sculpt.

## Scripting

The same operations are commands (see [Scripting](scripting.md)): `zsphere_new`, `zsphere_list`, `zsphere_add`,
`zsphere_set`, `zsphere_delete`, `zsphere_insert`, `zsphere_preview` and `zsphere_make_mesh`. For example, a
simple figure:

```
zsphere_new radius=0.5
zsphere_add parent=0 position=0,0.8,0 radius=0.3
zsphere_add parent=0 position=0.9,0.1,0 radius=0.2 symmetry=x
zsphere_make_mesh resolution=160
```
