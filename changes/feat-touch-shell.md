---
added: **Touch layout (preview, Android)**: a tablet layout for the Sculpt room, on by default on Android
  (**Edit > Preferences > Interface > Touch layout**: Automatic, On, Off; On tries it on a desktop touch screen). Made for
  the pen: the model fills the screen (the desktop panels come back from the ... menu), the side button held while
  the pen hovers sets the size (drag sideways) and strength (up and down), and nothing sits where the pen hand rests.
  A rail beside your free hand holds a thumb pad (drag up and down for size, sideways for strength; tap it for the exact
  values) and hold keys Invert, Smooth, Mask and Lock (hold one while you stroke, tap to latch it; Lock stops finger
  navigation). A fixed dock down the pen-hand edge holds the current brush, 4 pinned brushes (long-press a slot to pin
  the current brush there), the recent ones and all brushes; a strip along the top shows the brush's name, Mirror X,
  falloff, alpha and brush settings.
  The chrome fades while you stroke, and the size and pressure follow the stroke. Finger mode on the canvas: one
  finger sculpts once it moves, two fingers navigate, a two-finger tap undoes, three fingers redo, and two fingers
  held still then slid scrub the undo history. Touches while the pen is near the glass are ignored.
testers:
  - Android tablet: open the app; the rail is on the left (right with Left-handed layout in the ... menu), the dock
    down the right edge, nothing in the bottom-right corner, every button at least a fingertip wide. Hold the pen's
    side button while hovering and drag: sideways changes the size, up and down the strength. Sculpt with the pen: the rail and dock fade, the size
    follows the pen.
  - No pen: one finger sculpts once it moves; tap two fingers: undo; three: redo; pinch: zoom, never undo.
  - Hold Smooth with the other hand and stroke with the pen: it smooths; let go: it sculpts. Tap Smooth: it stays on.
  - Thumb pad against two sliders: see TESTERS.md (... menu > Rail: two sliders).
---
Shots: ui-shots/touch_shell_1440, touch_shell_2880. Design: the tablet UI proposal (Forge Night, thumb pad, finger mode).
