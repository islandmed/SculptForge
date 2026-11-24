---
whats_new: **AI agents (MCP)**: restoring a checkpoint that is no longer in the undo history now fails cleanly and leaves your scene exactly as it was.
fixed: **MCP `checkpoint_restore`**: restoring a checkpoint whose steps were no longer in the undo history (dropped by the undo memory budget, or replaced by a newer edit) returned an error but had already undone your current changes on the way. The restore is now checked before any step is taken, and a restore that still ends off the checkpoint puts the scene, the undo and redo steps and the active Part back as they were. The error reads "checkpoint no longer in undo history" and says the scene was not changed. Reported by a member.
testers:
  - Any OS, with an AI agent connected: checkpoint_save "a", make several edits with the undo memory budget set very low (Preferences > Performance), then checkpoint_restore "a": the error says the checkpoint is no longer in the undo history, and the scene and Undo stay exactly as they were.
  - checkpoint_restore of a checkpoint that is still in the history still goes back and forward as before, and one undo takes it back.
---
