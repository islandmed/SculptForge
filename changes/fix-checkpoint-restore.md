---
fixed: A failed headless MCP checkpoint restore now leaves the current edit and undo history intact. Fix by @surrealbydesign.
whats_new: Restoring a checkpoint whose history was discarded or evicted leaves your current work in place.
testers:
  - In headless MCP, save A, translate a sphere, save B, restore A and translate in a different direction. Restoring B should fail. Undo should still take back the new translation.
---
