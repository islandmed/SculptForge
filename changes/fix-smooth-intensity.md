---
whats_new: **Shift smoothing keeps Smooth's own Intensity**: set Smooth's Intensity with Shift held, and every stroke type (Drag Stamp too) smooths at that value from the first dab, not at the brush's Intensity. The Intensity slider keeps showing it while you smooth.
fixed: **Shift-smooth used the wrong Intensity** (#180, thanks @nekirk11): Shift with Drag Stamp strokes smoothed at the main brush's Intensity instead of Smooth's own, and the Intensity slider turned back to the main value as soon as the stroke began (so Smooth Intensity 10 looked like 80). Every stroke type now reads Smooth's value when Shift made it a smooth stroke.
testers:
  - Any OS, mouse or pen: set Intensity 80, hold Shift and set Smooth Intensity 10. Shift+drag over a bumpy area, once with Dots and once with Drag Stamp: it smooths gently both times, and the slider keeps reading "Smooth Intensity 10" while you hold the button.
  - The reverse (Intensity 10, Smooth 80): Shift smoothing is strong.
---
Drag Stamp built its dab from the main Intensity whatever the stroke's brush was (Slide Stamp, Dots and Spray already
read Smooth's). One helper, App::strokeIntensity(), now feeds every stroke type. Test: tests/ui/shift_smooth_intensity.sfui.
