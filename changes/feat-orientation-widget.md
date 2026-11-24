---
whats_new: **Orientation widget**: a small cube in the corner of the canvas turns with the view. Click a face, edge or corner to turn the camera there with a short move, drag it to orbit, double-click to frame the model. On by default in the SculptForge layout; **Draw > Camera > Corner gizmo** picks the widget, the X / Y / Z axes or nothing.
highlight: Orientation widget: click a face, edge or corner to turn there
added: **Orientation widget** (Draw > Camera > Corner gizmo > Orientation): a small cube of dark glass in the corner of
  the canvas turns with the view, its faces tinted by axis (X red, Y green, Z blue), the side under the pointer lit in the
  accent and named under it. Click a face to look from that side (orthographic, as the numpad views; the same face again
  shows the opposite side), an edge or a corner for a three-quarter view, each with a short eased move (none with
  Preferences > Interface > Reduce motion); drag it to orbit; double-click to frame the model. The SculptForge layout
  shows it by default, the Classic layout keeps the X / Y / Z axes; **Corner gizmo** picks either or hides the corner,
  saved in the preferences. Requested by members.
member_request: true
testers:
  - SculptForge layout, any OS: the corner shows a small cube. Click its right face, an edge, a corner: the view glides
    there in under half a second and the side's name shows under the disc. Click the front face twice: the back view.
  - Drag the cube: the view orbits as a drag on the old axes' disc did; no view jump on release. Double-click it: the
    model fills the view.
  - Classic layout: the X / Y / Z axes are still there by default. Draw > Camera > Corner gizmo > Orientation swaps
    them for the cube; Hidden clears the corner; restart: the choice is kept.
  - Windows at 150% / 200% scaling and Gizmo size Small / Large: the face labels fit or hide, clicks land on the part
    you aimed at (face, edge, corner).
  - Pen: a tap on a face turns the view, a short drag orbits (no accidental turn when the pen wobbles on a tap).
---
Requester: Fenix (Discord #bug-reports, feature-queue.md: "Clickable 3D orientation widget ... to jump to front/side/top
views"); consent for a name credit is recorded in the queue, the public note says "members" (changes/README: @handle
only for GitHub people). Fenix asked for the top right; the widget sits where the axes have always been (top left) so
the Classic axes, the View Figure and the widget share one spot and one size setting; a corner choice is a follow-up.
