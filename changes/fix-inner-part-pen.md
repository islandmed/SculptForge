---
whats_new: **A Part inside another Part** (an eye in its socket): the brush says why when the pointer is over the Part in front ("Head is in front: Alt+Q selects it"), and **Frame Part** says when the view ended up inside another Part.
fixed: Sculpting a Part that sits inside another (an eyeball inside a head): a press that lands on the Part around it only turned the view, with no word why, so the pen seemed unable to sculpt. The status line now names the Part in front (Alt+Q selects it; Solo or X-Ray shows the active one). Frame Part also says when the camera is inside another Part, whose inner wall then fills the view.
testers:
  - Any OS, a pen if you have one: append a Sphere Part, move it inside another Part, orbit round behind it, press Frame Part (Numpad .). The status line says you are inside the other Part; press on the wall beside the small Part and it says that Part is in front. Solo the small Part: sculpting works as before.
---
Reviewers: src/app/Input.cpp (noteViewInsideAnotherPart, notePressOnAnotherPart), App.cpp (the press path), core/Parts.cpp (partEnclosing); tests: ui_inner_part_pen, part_enclosing_finds_the_head_around_a_point.
