---
testers:
  - 10M mesh, Predictive Strokes off: lift the pen on a long stroke and press Ctrl+Z at once. The status line says the undo is queued (Esc cancels), then that stroke is undone and nothing else comes back (gh#184 follow-up).
---
Tests only: UI tests can run strokes on the stroke thread (`interactive_strokes on`).
