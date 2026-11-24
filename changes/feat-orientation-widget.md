---
whats_new: **Orientation widget**: the axes in the corner of the canvas, through a ball of dark glass with a dot at each end. Click a dot to look from that side with a short move (the dot facing you again: the opposite side), drag it to orbit, double-click to frame the model. On by default in the SculptForge layout; **Draw > Camera > Corner gizmo** picks the widget, the X / Y / Z axes or nothing, and the top left or top right corner.
highlight: Orientation widget: click a dot to turn there, drag to orbit, double-click to frame
added: **Orientation widget** (Draw > Camera > Corner gizmo > Orientation): the world axes through a ball of dark glass in
  the corner of the canvas, a dot at each of the six ends (X red, Y green, Z blue; no letters), the dot under the pointer
  lit in the accent and its side named under the disc. Click a dot to look from that side (orthographic, as the numpad
  views; the dot facing you again shows the opposite side) with a short eased move (none with Preferences > Interface >
  Reduce motion); drag it to orbit; double-click to frame the model. The SculptForge layout shows it by default, the
  Classic layout keeps the X / Y / Z axes; **Corner gizmo** picks either or hides the corner, and **Position** puts it at
  the top left (as before) or the top right. Saved in the preferences. Requested by members.
member_request: true
testers:
  - SculptForge layout, any OS: the corner shows a glass ball with six dots. Click the right dot, the top dot: the view
    glides there in under half a second and the side's name shows under the disc. Click the front dot twice: the back view.
  - Drag the ball or the disc: the view orbits as a drag on the old axes' disc did; no view jump on release. Double-click a
    dot: the model fills the view and the view stays where it was.
  - Draw > Camera > Corner gizmo > Position > Top right: the widget moves to the top right; clicks still land; restart:
    the choice is kept. Classic layout: the X / Y / Z axes are still there by default.
  - Windows at 150% / 200% scaling and Gizmo size Small / Large: the dots are easy to hit, the far ones smaller.
  - Pen: a tap on a dot turns the view, a short drag orbits (no accidental turn when the pen wobbles on a tap).
---
Requester: Fenix (Discord #bug-reports, feature-queue.md: "Clickable 3D orientation widget (top right) ... to jump to
front/side/top views"); consent for a name credit is recorded in the queue, the public note says "members" (changes/README:
@handle only for GitHub people). The top right corner he asked for is the Position setting (default stays top left).
