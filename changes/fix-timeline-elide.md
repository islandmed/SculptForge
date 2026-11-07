---
fixed: The Sculpt Timeline strip's header no longer runs under **Save with project** in a narrow window or at a high UI
  scale (a 1024 px window at 200 %): the step count, branches, step name and message are shortened with "..." to fit.
testers:
  - Windows at 200% scaling on a small window: Edit > Sculpt Timeline open, sculpt a few strokes; the header text
    ends with "..." before the Save with project checkbox, never under it.
---
Controls are measured instead of a fixed 250 px; UI test tests/ui/timeline_narrow.sfui (timeline_overlaps).
