---
added: **Dynamic on a painted Part keeps the paint inside its charts**: a triangle Dynamic makes never samples past the edge of
  a chart (no white specks on the paint where big brushes simplify a painted head), and no triangle's texture is turned over.
  Parts without UVs are unchanged.
testers:
  - Windows or macOS: Auto UV the Human head, paint a few colours, turn Dynamic on and drag big Smooth, Standard and Clay brushes across the eyes and forehead; no white specks or streaks appear on the paint.
---
Follow-up to the 0.13.14 entry on Dynamic keeping a painted Part's texture.
