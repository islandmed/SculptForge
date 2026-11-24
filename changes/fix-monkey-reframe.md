---
changed: UI test harness (tests/ui, the monkey): the monkey's way out frames the model (the Frame action) before the
  sculpt check, so a sequence that mirrors, rotates or views the model off the canvas fails only for a latch, never for the
  camera (ui_monkey seed 9 went red on every branch that added a checked toggle).
---
Nothing a user sees: the ready gate asks for a note when src/app changes, and src/app/UiTestModes.cpp is the test harness.
