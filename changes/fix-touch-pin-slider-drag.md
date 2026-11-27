---
fixed: On a touch screen or with a pen, a slow drag on a slider (Exposure, Roughness, Metallic and the rest) froze after half a second and the Add to My Panel / shelf menu opened over it. A finger that moves on a slider is now always dragging it; the menu still opens when the finger rests still.
testers:
  - Android or any touch screen: drag a slider slowly (a few pixels a second) for two seconds; it keeps following the finger and no menu opens. Rest a finger still on a slider for half a second; the menu opens and the value stays.
---
Branch fix/touch-pin-slider-drag. Cause (since 0.14.1): the long press (ui::TouchPin, kLongPress 0.5 s) allowed 8 px of travel and kept the slider
read-only while the finger was within it; a slow drag covers a few px in that half second, so it armed. A slider now allows only the finger's own
tremor (1.5 px, the pen 3 px; sliderSlop in Ui.cpp), so any real movement is a drag. Test: tests/ui/touch_slider_drag.sfui (verb touch_slide).
