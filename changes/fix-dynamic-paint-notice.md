---
added: "**Dynamic** on a Part with a painted texture asks once per session before its first stroke: Dynamic rebuilds triangles and drops the Part's painted texture, and Undo brings it back. **Continue** goes on (then stroke again), **Cancel** makes no dab, and **Don't show again** hides it (Preferences > Hints shows it again)."
testers:
  - "Any OS: Sphere, Texture Image > Auto UV > New Texture, paint two strokes, turn Dynamic on, stroke: the note appears and nothing changes. Continue, stroke again: the paint disappears from the Part. Ctrl+Z: it comes back exactly. The next Dynamic stroke doesn't ask again until you restart."
---
