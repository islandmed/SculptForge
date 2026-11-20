---
whats_new: **Timelapse and history on a Micro level**: strokes on a Micro level are saved with the project's history and show in its timelapse.
fixed: **Timelapse of a Micro level** (member report): a project saved with its history lost the strokes made on a Micro level
  (the saved history kept the steps without their detail, or was refused whole when it held only such steps), so the timelapse
  of the reopened project missed them or could not be made. They are saved with it now (history file version 2; older files still
  open). A step that moves the base mesh and writes Micro detail together is also seeked exactly (the fast path skipped the detail).
  Adding or removing a Micro level still cuts the saved history there, as a topology change does.
testers:
  - Any OS: Add Micro, sculpt a few strokes on it, turn on Save with project, save, reopen; Export timelapse: the strokes build up in the video.
  - Drag the strip's cursor back and forth over the Micro strokes: the surface follows each step.
---
Reviewers: core/Timeline.cpp (savable, writeMicro/readMicro, fastShape), tests timeline_history_sidecar_keeps_micro_strokes and
timeline_seek_swaps_micro_tiles_of_steps_that_move_the_base, ui_timelapse_micro.
