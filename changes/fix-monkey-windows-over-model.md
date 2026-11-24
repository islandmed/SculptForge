---
changed: UI test harness (tests/ui, the monkey): unlatch closes the Community brushes and AI Brush windows, the monkey's way
  out closes the floating windows (close_floating), and expect_sculpts reads the window over the stroke where the stroke starts, so a slow runner can't flip the
  verdict either way (ui_monkey was red on the Actions runner only).
---
Nothing a user sees: the ready gate asks for a note when src/app changes, and src/app/UiTestModes.cpp is the test harness.
