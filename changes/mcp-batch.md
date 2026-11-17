---
whats_new: **Batch and checkpoints for AI agents**: an agent can run a list of commands as one undo step that rolls back whole if one fails, and name moments in the undo history to come back to.
added: **Transactions for AI agents** (headless MCP server): `batch` runs up to 64 commands as one undo step and, at the first error, rolls back everything it did (the scene is exactly as before) and reports which command failed and why, with each command's result; `checkpoint_save`, `checkpoint_restore` and `checkpoint_list` name places in the undo history and walk back or forward to them, across Parts, without copying the scene. Every command in a batch passes the studio policy and the audit log as if called alone. `undo` and `redo` take a whole batch or restore at once.
testers:
  - Any OS, with an AI tool connected to `sculptforge-cli --mcp`: ask it to run a batch whose last command fails (for example a stroke with bad points); the sphere must look exactly as before.
  - Then ask it to save a checkpoint, sculpt a little, add a Part, and restore the checkpoint: the Part is gone and the sculpt is undone; `undo` brings it all back.
---
Needs mcp-structured (structuredContent) merged first only for the shared reply code; no other branch touches these files.
