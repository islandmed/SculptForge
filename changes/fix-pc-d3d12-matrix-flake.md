---
changed: UI test harness (src/app/UiTestRender.cpp, tools/test): the render matrix's splat pre-pass check compares the colour channels
  only (one pixel's opacity wobbles by up to 4 between identical draws on OpenGL over D3D12, the colour never by more than 1), and a
  test that passed on its retry is listed with the line it failed on.
---
Nothing a user sees: the ready gate asks for a note when src/app changes, and src/app/UiTestRender.cpp is the test harness.
