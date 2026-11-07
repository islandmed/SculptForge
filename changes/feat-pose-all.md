---
whats_new: **Pose all Parts**: **Tool > Parts > Pose all Parts** merges every visible Part into one light Pose Mesh; pose it with the Gizmo and masks, then **Apply Pose** gives every Part the pose at full resolution, as one undo step. Requested by @ABHITAR.
highlight: Pose a whole character at once
added: **Pose all Parts** (Tool > Parts, `figure_pose_begin` / `figure_pose_apply` / `figure_pose_cancel`): the visible
  Parts become one light Pose Mesh (each Part reduced to its share of 300k triangles, one face group per Part, so Mask
  by Face group picks Parts). Pose it with the Gizmo, masks, soft joints and symmetry; Undo steps back through the
  pose. Apply Pose moves every Part at full resolution the same way (sculpt layers, subdivision and micro levels
  follow), as one undo step across all Parts; Cancel Pose leaves everything as it was. Requested by @ABHITAR.
testers:
  - Any OS: open a project with several Parts (body, eyes, armour). Tool > Parts > Pose all Parts: the Parts hide and a
    grey Pose Mesh shows. Mask the body, rotate an arm with the Gizmo (R) and a falloff, Apply Pose: every Part (eyes,
    armour on that arm) follows; one Ctrl+Z puts every Part back, Ctrl+Shift+Z poses them again.
  - Any OS: a Part with subdivision levels: pose, Apply, then Higher Level / Lower Level: the detail follows the pose.
  - Windows: a 20M-triangle scene: Pose all Parts and Apply each finish with the time shown in the status line.
---
Design and patent notes: docs/design/pose-all.md. Brushes on the Pose Mesh are deliberately not built (patent flag).
