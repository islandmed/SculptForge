# Quick tour for experienced sculptors

If you have sculpted in another app, most habits carry over. This page shows where things are and how to set
SculptForge up the way your hands expect.

## Keys and layout

At the first start, pick **Classic sculpting keys** when SculptForge asks what you are coming from. It turns on the
classic keys and gestures and the **Classic layout**: a title strip, the alphabetical palette bar and a two-row top
shelf. Change either later in **Edit > Preferences > Input** and **Edit > Preferences > Interface > Look**.

With the Classic sculpting keys you sculpt with a left drag, hold <kbd>Alt</kbd> to subtract and <kbd>Shift</kbd>
to smooth, drag on the background to rotate, <kbd>Alt</kbd>+drag to pan, and hold <kbd>Ctrl</kbd> to mask.
<kbd>S</kbd>, <kbd>U</kbd>, <kbd>I</kbd> and <kbd>O</kbd> put the Draw Size, Intensity, RGB Intensity and Falloff
sliders under the cursor, <kbd>Space</kbd> opens the Quick Menu, <kbd>B</kbd> opens the brush pop-up with the same
letter keys (B, S, T picks Standard), <kbd>,</kbd> opens the Library and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>
stores your interface. Every key is listed in [Keyboard and mouse](shortcuts.md#classic-sculpting-keys).

## Where things are

| Looking for | In SculptForge |
|---|---|
| Separate meshes in one scene | **Parts**: **Tool > Parts** ([Parts](parts.md)) |
| Blocking out from spheres | **Forge Spheres**: **Tool > Forge Spheres** ([Forge Spheres](forge-spheres.md)) |
| Colour on the vertices | **Vertex Paint** ([Vertex paint and materials](vertex-paint.md)) |
| Groups of faces to mask, hide and split by | **Face Groups** ([Face groups and visibility](face-groups.md)) |
| Viewport materials from a lit-sphere image | **Lit Spheres** (**Material**) |
| Your projects, brushes, alphas and materials | **Library** (<kbd>,</kbd>) |
| An even, watertight remesh | **Tool > Remesh > Voxel Remesh** ([Geometry](geometry.md)) |
| Automatic quad retopology | **Tool > Geometry > Quad Remesh** ([Retopology](retopology.md)) |
| Sculpting that adds detail where the brush goes | **Dynamic topology** ([Dynamic topology](dynamic-topology.md)) |
| Subdivision levels | **Tool > Geometry > Divide** ([Subdivision levels](subdivision-levels.md)) |

The command palette (<kbd>F3</kbd>) finds any feature by what it does: type "remesh", "groups" or "folders".

## Bringing your work over

Export OBJ, FBX, glTF or USD from the other app and open it here: quads, UVs and groups come along. Or use the
**Bridge** (**File > Bridge**) to send Parts back and forth with face groups, mask and vertex paint; see
[Exporting, Unreal and Bridge](exporting.md#exchange-models-through-the-bridge).

## See also

- [The interface](interface.md)
- [Keyboard and mouse](shortcuts.md)
- [Glossary](glossary.md)
