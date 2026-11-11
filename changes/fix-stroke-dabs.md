---
whats_new: **A fast mouse or trackpad stroke right after a pen stroke** keeps its whole line.
fixed: **A fast mouse or trackpad stroke right after a pen stroke** laid only 2 dabs (a mark at each end instead of a
  line, #195): while the pen is in range the mouse's motion is ignored, because some tablet drivers send stray mouse
  events beside the pen's, and that also swallowed a stroke the mouse had started. A stroke the mouse began now keeps all
  of its motion.
testers:
  - Windows or Linux with a tablet: draw with the pen, then at once flick a short fast stroke with the mouse or
    trackpad (size 30, Clay); it should be a line of dabs, as the flick after it.
---
Test: tests/ui/stroke_dabs.sfui (a pen stroke, then the same mouse flick: same dabs).
