---
highlight: Experimental: Live Move and Background Preview for very dense meshes
whats_new: **Live Move (experimental)**: **Preferences > Performance > Live Move**. The graphics card draws the
  Move brush's drag from where the surface started, so it keeps up with the pen on any mesh size, and the move is
  applied once, exactly, when you lift the pen ([Performance](performance.md)).
whats_new: **Background Preview (experimental)**: **Preferences > Performance > Background Preview**. On dense
  meshes, the dabs a big brush is still working out show as a preview under the pen, so the stroke no longer trails
  behind it. The finished sculpt is the same either way.
whats_new: **Fast Upload (experimental)**: **Preferences > Performance > Fast Upload**. Sculpted changes reach the
  graphics card through shared memory and a copy on the card, instead of a copy by the driver during the frame.
added: Experimental toggles in Preferences > Performance, all off by default. **Live Move**: the GPU draws a
  Move drag from the grab's start positions and the whole move is applied once at pen lift (one grab, equal to the
  per-frame grabs to 1e-6). It falls back to the normal Move with masks, hidden faces, sculpt layers, micro levels,
  Connected Only or View falloff (`SF_LIVE_MOVE=0/1`). **Background Preview**: the predictive preview is drawn for
  background dabs (meshes of 2M+ vertices), and the frame no longer waits for a batch; the solver's dabs are
  unchanged (`SF_BG_GHOST=0/1`). **Fast Upload**: stroke uploads are packed into persistently mapped staging
  buffers (GL_ARB_buffer_storage) and copied on the GPU (`SF_PERSISTENT_UPLOAD=0/1`). The research behind them,
  including a design for focus detail on billion-polygon assets, is in docs/design/perf-moonshots.md.
testers:
  - Any OS, Live Move: Preferences > Performance > Live Move (Experimental) on. Open Help > Benchmark's dense
    head, or any model over 10M triangles. Drag the Move brush big and fast. Good: the bulge follows the pen
    smoothly with no stutter, and when you lift the pen the shape stays exactly where the preview left it (no jump
    or flicker). Then Ctrl+Z / Ctrl+Shift+Z restores and redoes it in one step.
  - Any OS, Live Move with X symmetry on: both sides move in the preview and stay the same after the pen lifts.
    With a mask, a hidden part, a sculpt layer or a micro level, Move works exactly as it does with the toggle off.
  - Windows (tablet): Live Move with pen pressure and a fast flick. Good: no lag behind the pen, and no gap or
    dent where the drag ended.
  - Any OS, Background Preview: Preferences > Performance > Background Preview (Experimental) on. Use Clay or
    Standard at 200-300 px on a model of 15M+ triangles. Good: the stroke stays under the pen instead of trailing,
    lifting the pen is instant, and the result looks the same as the same stroke with the toggle off.
  - Any OS, Fast Upload: Preferences > Performance > Fast Upload (Experimental) on, then sculpt on a dense model.
    Good: nothing looks different (no flicker, holes, stale patches or black triangles), and strokes feel as fast
    or faster. macOS and older Intel drivers may lack the OpenGL feature; there it simply does nothing. Report your
    GPU and driver either way.
---
Research prototypes from docs/design/perf-moonshots.md (ideas 1-8). All three toggles are off by default; the
strokes they commit match the toggles-off result (tests/ui/live_move.sfui, ui_stroke_background_preview,
ui_stroke_persistent_upload with SF_MEASURE_VERIFY).
