---
---
fixed: **Undo history and memory** (#202, slice 1; thanks @TheLycanFenrir): the undo history now fits the computer's free memory
  (a quarter of its total memory, never below 256 MB) instead of a fixed 1 GB that could run an 8 GB computer out of
  memory next to a very dense mesh, and a 64 GB computer keeps far more history. When steps must go, the status line says
  "Oldest N undo steps removed to free memory" (English, Spanish and both Chinese) instead of dropping them silently, and
  Preferences > Performance shows the live budget. Saving older steps to disk and unloading hidden Parts are planned for the rest of #202.
---
Slice 1 of #202 (adaptive undo budget). Disk spill (slice 2) and hidden-Part unload (slice 3) follow.
