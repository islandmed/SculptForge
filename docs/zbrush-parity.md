# ZBrush parity audit

What a ZBrush sculptor does without thinking in the first hour, what SculptForge does, and how big the gap is.
Written for the v0.7 "ZBrush feel" pass (branch feat/zfeel, 2026-10-24) after a working ZBrush modeler tried
v0.2 and uninstalled it because it "didn't feel like ZBrush".

**Gap**: none / small (works, but differently enough to notice) / big (missing, or the hands do the wrong thing).
**Priority**: P1 trips a ZBrush user in the first hour, P2 noticed within a day, P3 nice to have.
**Status** is what the v0.7 pass did about it (*fixed*, *kept* = deliberately different, *open*).

ZBrush behaviour is checked against Maxon's official shortcut sheet [1] and the ZBrush documentation [2-5];
rows marked † come from ZBrush practice and community references [6] where Maxon's sheet is silent, and should
be confirmed by a ZBrush user.

## Navigation (Edit mode, a 3D mesh)

| ZBrush | SculptForge before v0.7 | Gap | Pri | Status |
|---|---|---|---|---|
| Drag on the background: free rotate [1 §3] | Same (free rotation is the default since v0.3, Turntable is a preference) | none | – | – |
| Alt+drag on the background: move (pan) [1 §3] | Same | none | – | – |
| Alt+click, release Alt, drag: scale (zoom) [1 §3] | Same | none | – | – |
| Shift pressed during a rotation: snap to 90 degree views [1 §3] | Same | none | – | – |
| Shift+click, release Shift, drag: rotate around the view axis (Z) [1 §3] | Shift+drag snapped; releasing Shift before moving just orbited | big | P2 | fixed: roll |
| Right-click navigation (Preferences): rotate, Alt pans, Ctrl scales, also over the model [1 §3.1] | Same, always on | none | – | – |
| F: fit the mesh to the view [1 §6] | Same (frames every visible Part) | none | – | – |
| Shift+Alt+click a SubTool: frame it [1 §6.1] | Missing | small | P3 | fixed |
| P: perspective on / off [1 §6] | P is Paint; Numpad 5 switches perspective / orthographic | small | P3 | partly (orthographic on Numpad 5; P stays Paint) |
| Mouse wheel | ZBrush does not zoom with the wheel by default; SculptForge does | none (extra) | – | kept |

## Sculpting modifiers

| ZBrush | SculptForge before v0.7 | Gap | Pri | Status |
|---|---|---|---|---|
| Alt held: ZAdd / ZSub swap [1 §5] | Same (and the ring turns red) | none | – | – |
| Shift held: Smooth brush [2] | Same | none | – | – |
| Smooth keeps its own Z Intensity: holding Shift, the slider shows and sets Smooth's value [2] | Smooth used the active brush's Strength, so a gentle Standard (0.1) smoothed at 0.1 | big | P1 | fixed: separate Smooth intensity, Shift shows it |
| Shift pressed after a stroke starts: the stroke is constrained to a straight line † [6] | Nothing (Shift mid-stroke ignored) | big | P2 | fixed: straight line, snapping to 45 degrees |
| Ctrl held: paint the mask (MaskPen) [1 §6.6] | Same | none | – | – |
| Ctrl+Alt held: erase the mask [1 §6.6] | Same | none | – | – |
| Ctrl+click on the mesh: blur the mask [1 §6.6] | Painted a dot of mask | big | P1 | fixed |
| Ctrl+Alt+click on the mesh: sharpen the mask [1 §6.6] | Erased a dot of mask | big | P1 | fixed |
| Ctrl+click the background: invert the mask [1 §6.6] | Same | none | – | – |
| Ctrl+click+drag the background: clear the mask [1 §6.6] | Same, but with nothing masked the drag zoomed the view | small | P2 | fixed: ZBrush style does nothing; SculptForge style keeps the zoom |
| MaskLasso / MaskRect (Ctrl+drag) [3] | Same (v0.3) | none | – | – |
| Ctrl+A mask all, Ctrl+Shift+A clear mask, Ctrl+I invert mask, Ctrl+H view mask [1 §6.6] | Buttons only, no keys | big | P1 | fixed |

## Visibility and face groups

| ZBrush | SculptForge before v0.7 | Gap | Pri | Status |
|---|---|---|---|---|
| Ctrl+Shift+drag: show inside (green); press Alt during the drag: hide inside (red) [1 §6.9] | Same; the keys can be let go during the drag, as in ZBrush | none | – | – |
| Ctrl+Shift+click the background: show everything [1 §6.9] | Same | none | – | – |
| Ctrl+Shift+click a polygroup on a fully visible mesh: show only it [1 §6.9] | Same | none | – | – |
| Ctrl+Shift+click a polygroup on a partly hidden mesh: hide that group [1 §6.9] | Isolated the clicked group instead | big | P1 | fixed |
| Ctrl+Shift+click twice: hide the group (the second click inverts the isolation) [1 §6.9] | Same | none | – | – |
| Ctrl+Shift+click+drag on the background: reverse visibility [1 §6.9] | A rectangle around nothing failed with "Nothing visible inside" | small | P2 | fixed: a selection that encloses nothing inverts the visibility |
| Ctrl+Shift+Alt+click a polygroup: hide it | Same (SculptForge extra, harmless) | none | – | kept |
| Ctrl+W: group masked and clear the mask [1 §6.8] | Tool > Face Groups > From mask, no key, mask kept | big | P1 | fixed: Ctrl+W; with nothing masked it groups what is visible † |
| Ctrl+Shift+W | Not a default ZBrush shortcut in Maxon's sheet [1] | – | – | – |
| Shift+F: PolyFrame and polygroups [1 §6] | Same | none | – | – |

## Keys

| ZBrush | SculptForge before v0.7 | Gap | Pri | Status |
|---|---|---|---|---|
| B then two letters: B S T Standard [1 §5.1] | Same pop-up | none | – | – |
| B C B ClayBuildup, B D S DamStandard, B M V Move, B I N Inflate, B P I Pinch, B S M Smooth, B S H SnakeHook † | Letters were "first free letter": B C A, B D A, B M O ... | big | P1 | fixed: ZBrush letters for the brushes both apps have |
| S: Draw Size at the cursor [1 §5] | Same | none | – | – |
| U: Z Intensity at the cursor [1 §5] | Missing (I was Strength) | big | P1 | fixed |
| I: RGB Intensity at the cursor [1 §5] | I opened Strength (= Intensity); paint had no intensity of its own | big | P1 | fixed: RGB Intensity for paint; SculptForge style keeps I = Strength |
| O: Focal Shift at the cursor [1 §5] | No focal shift | big | P2 | fixed: Falloff (falloff), inner ring shows it |
| [ and ]: Draw Size steps [1 §5] | Same (10% steps) | none | – | – |
| Space (or right-click): QuickMenu at the cursor [1 §1] | Missing | big | P1 | fixed: Space Quick Menu (right-click stays navigation, as with ZBrush's RightClick Navigation on) |
| L: Lazy Mouse on / off [1 §5.3] | Missing | small | P2 | fixed |
| C: pick the colour under the cursor [1 §5.2] | Same | none | – | – |
| V: switch main and secondary colour [1 §5.2] | Clicking the swatch only | small | P2 | fixed |
| Q W E R: Draw, Move, Scale, Rotate [1 §5] | Same (Gizmo instead of the Transpose line) | small | – | kept (Gizmo, documented) |
| T: Edit mode on / off [1 §5] | No 2.5D canvas mode: always in Edit | none | – | kept (documented) |
| X: symmetry [1 §6] | Same | none | – | – |
| Ctrl+Z / Ctrl+Shift+Z [1 §1] | Same (Ctrl+Y too) | none | – | – |
| Ctrl+D divide, D / Shift+D higher / lower res [1 §6.2] | Same (subdivision levels since v0.5) | none | – | – |
| , LightBox, Tab hide palettes, Ctrl+Alt+click assign a hotkey, Ctrl+Shift+I Store Config, Ctrl+L load UI [1] | Same | none | – | – |
| Ctrl+Shift+Alt+I: save the UI configuration [1 §12] | Missing | small | P3 | fixed |
| N: list all SubTools [1 §6.1] | Missing | small | P3 | open (the Parts palette is in the right tray) |
| 9: QuickSave [1 §1] | 9 was the Sharp Crease brush (1-0 pick brushes) | small | P2 | fixed: ZBrush style 9 = Save Snapshot, digits free; SculptForge style keeps 1-0 brushes |
| 1, 2, 3, Shift+1: replay / record strokes [1 §5.3] | Missing (1-0 picked brushes) | small | P3 | open (no stroke replay yet) |
| F1-F6 pop-ups (tools, brushes, strokes ...) [1 §1] | F1 Controls, F3 command palette | small | P3 | kept (B, M and the left-shelf tiles pop the same lists) |
| H: ZScript window [1 §13] | ` opens the console | small | P3 | kept |
| Ctrl+E edge loop, Ctrl+U reload ZScript | Ctrl+E export GLB, Ctrl+U Send to Unreal | small | P3 | kept (no edge loop tool) |

## Feel and defaults

| ZBrush | SculptForge | Gap | Pri | Status |
|---|---|---|---|---|
| Draw Size in screen pixels, constant on screen while zooming [4] | Same (radius in pixels, default 60; ZBrush 64) | none | – | – |
| Pen pressure drives Z Intensity, not size, by default [4] | Same (Pressure > Strength on, Size off; global curve in Preferences > Tablet) | none | – | – |
| Dab spacing: even along the stroke, independent of frame rate | Same: every input sample counts, dabs every 15% of the radius | none | – | – |
| LazyMouse with a small radius on by default for most brushes † | Dots stroke, no lazy; L now toggles Lazy Stroke | small | P3 | kept |
| Cursor: outer ring = Draw Size, inner ring = Focal Shift [4] | The inner ring showed Strength | small | P2 | fixed: inner ring shows Falloff |
| Brush falloff: smooth curve, Focal Shift 0 by default [4] | Smoothstep; Falloff added (0 = unchanged maths) | none | – | – |
| Open mesh borders | Smooth treats border vertices like the others, so an open edge pulls in a little with every pass. ZBrush's Smooth holds open borders better † | small | P3 | open (brush maths, out of scope for this pass) |
| Alt+click a SubTool selects it [1 §6.1] | Same | none | – | – |
| Paint without sculpting: Zadd off, Rgb on | P toggles the Paint brush (SculptForge) | none | – | kept |

## The "SculptForge" input style

The few places where SculptForge's own defaults differ and some users like them are kept behind
**Preferences > Input > Style** (ZBrush-style by default at the time of this audit; new installs now start with the SculptForge keys):

| | ZBrush (default) | SculptForge |
|---|---|---|
| 1 ... 0 | free (9 = QuickSave) | pick the ten built-in brushes |
| I | RGB Intensity at the cursor | Strength (Intensity) at the cursor |
| Ctrl+drag on the background with nothing masked | nothing (clears the mask) | zooms |

## Sources

1. Maxon, *ZBrush Shortcuts and Mouse Actions* (PDF):
   <https://help.maxon.net/zbr/en-us/Content/Resources/Uploads/ZBrush-Shortcuts.pdf> (section numbers in brackets)
2. Maxon ZBrush documentation, *Brush* and *Smooth brushes*:
   <https://help.maxon.net/zbr/en-us/Content/html/reference-guide/brush/brush.html>
3. Maxon ZBrush documentation, *Masking Essentials*:
   <https://help.maxon.net/zbp/en-us/Content/html/5_sculpting-and-painting_masking-essentials.html>
4. Maxon ZBrush documentation, *Draw palette* (Draw Size, Falloff, Z / RGB Intensity):
   <https://help.maxon.net/zbr/en-us/Content/html/reference-guide/draw/draw.html>
5. Maxon ZBrush documentation, *Masking sub-palette*:
   <https://help.maxon.net/zbr/en-us/Content/html/zbrushcore/reference-guide/tool/masking/masking.html>
6. ZBrushCentral and Polycount threads on Shift-constrained strokes (behaviour changed with Lazy Stroke 2.0 in
   4R8): <https://www.zbrushcentral.com/t/4r8-drawing-straight-lines/206837>,
   <https://polycount.com/discussion/comment/833245>
