# Panels in their own windows (multi-monitor)

Member Request, November 2026: requested by Richard Throgmorton (MakIt3D), Founding Member #1. Two monitors and a
small pen display: drag the User Guide, Controls and the tool panels out of the main window onto another monitor,
and find them there next time.

## Choice: one OS window per detached panel, not ImGui multi-viewports

We build Dear ImGui 1.91.9 from the master line. Its multi-viewport feature (`ImGuiConfigFlags_ViewportsEnable`)
needs the docking branch, and with viewports on **every ImGui coordinate becomes a desktop coordinate**: the main
viewport sits at the window's screen position. The shell (`computeShell`, `beginShellWindow`), the brush ring, the
stroke popup, radial menus, overlays on the foreground draw list and every `canvasRect()` test mix SDL window
coordinates with ImGui ones. Moving all of that to desktop space is a large, risky change for a feature most people
never turn on, and the docking branch would be a dependency swap in five pins (Fetch.cmake, Flatpak, AUR, upstream).
Viewports also cannot place windows on Wayland, and the SDL3 backend turns them off there.

Instead `App` owns a small list of **panel windows** (`src/app/PanelWindows.cpp`):

- each is an `SDL_Window` (OpenGL, resizable, high pixel density, a child of the main window so it minimises and
  stacks with it) with **its own ImGui context** that shares the main context's font atlas and style;
- every window draws with the **one GL context** (`SDL_GL_MakeCurrent(panel, glContext_)`), so textures, the font
  texture and the OpenGL3 backend's shader are shared: the panel context borrows the main context's renderer
  backend data (`io.BackendRendererUserData`) and gives it back before it is destroyed; it has its own SDL3
  platform backend, which already filters events by window ID;
- the panel's ImGui frame draws the same `App` code the tray does (`drawPaletteBody`, `UserGuide::draw`,
  `drawControlsWindow`), filling the window (`fillPanelHost`: pos 0,0, the window's size, no title bar; the OS title
  bar names it and its close button docks it back).

The main window's coordinates, shell and overlays do not change at all.

## What can be detached

- **Any palette** (Tool, Brush, Stroke, Alpha... the tray and drop-down palettes): a new header icon
  (`ICON_LC_SQUARE_ARROW_OUT_UP_RIGHT`, "Open in its own window"). While detached the palette is skipped in its tray
  and its drop-down raises the window instead; closing the window puts it back where it was (tray or bar).
- **User Guide** and **Controls**: a button in each, and commands `panel.detach_guide`, `panel.detach_controls`.
- **Reset panel windows** (`panel.reset`, Preferences > Config and the command palette): every panel back in the main
  window, the stored geometry forgotten. "Reset Layout and Shortcuts" does it too.

## Platforms

| | own window | position restored | notes |
|---|---|---|---|
| Windows | yes | yes (desktop coordinates, per monitor) | an owned window: never under the main one |
| macOS | yes | yes | child window: follows the main window's Space, and moves along when the main window is dragged |
| X11 | yes | yes | transient for the main window (tiling WMs float it) |
| Wayland | yes | **no**: the compositor places it | size restored; the user drags it to the other monitor once per session (Hyprland: window rules can pin it) |
| offscreen (UI tests), no window made | **no**: an in-window floating ImGui window | ImGui's own (imgui.ini) | same draw code, so the tests cover it |

A stored position is used only when a display still shows the window's title bar (same rule as `window.json`,
#105); otherwise the panel opens beside the main window.

## Persistence

`<user data>/panels.json`, machine-local like `window.json` (never in ui.json: an exported interface must not carry
one desk's monitor coordinates): `{"panels": [{"id": "palette.tool", "x":, "y":, "w":, "h":, "placed": true}]}`.
Written on detach, dock, after a move or resize settles (0.5 s), and at quit. Not read or written by tests, renders,
recordings or proof mode (`rememberWindow_`). Unknown ids are dropped; at most 32 entries; sizes clamped.

## Input and focus

- Events carry a window ID. `App::handleEvent` hands every event for a panel window to that panel **first**, so a
  click in a panel can never start a stroke on the canvas (the main code reads `e.button.x/y` as canvas pixels).
- Pen events on a panel become ImGui pen mouse events in that context (SDL's pen-to-mouse is off: #95).
- Keys in a panel go to its ImGui first; unless it is typing into a text field, they also go to the main window
  (window ID rewritten), so shortcuts (B, S, Ctrl+Z, F1...) work with a panel focused. Key-ups always go through, so
  nothing sticks.
- **Never steal focus mid-stroke:** panels restored at start-up are shown with `SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN`
  0; a detach requested while a stroke or navigation drag runs waits for it to end. The main window keeps the mouse
  captured during a stroke, so passing over a panel doesn't interrupt it.
- Closing a panel window docks it; closing the main window quits (panels never keep the app alive).
- Cursor: panels don't set the system cursor (`NoMouseCursorChange`); `updateMouseCursor` takes the shape from the
  panel under the pointer, so the one SDL cursor isn't fought over.

## Drawing and performance

- The main loop draws on demand. A panel redraws when an event reaches it, and after each main frame. While a
  stroke or navigation drag runs, panels redraw at most 30 times a second.
- Hover-only events in a panel (motion with no button held, enter/leave, expose) redraw **only that panel**, not the
  3D view: the floor tier never redraws a 1M-poly scene because the pointer crossed a palette.
- Panels swap with swap interval 0 (set on their drawable), so N panels never wait N vsyncs; the main window keeps
  its interval 1.
- Cost to measure (floor tier, bench.sh): main frame time with 0, 1 and 3 detached panels, idle and during a stroke.
  Each panel is one ImGui frame (~0.1-0.5 ms CPU for a palette) plus one small swapchain. `SF_BENCH_PANELS=
  palette.tool,palette.brush,controls` opens those panel windows at start, offscreen too, so one build is its own
  baseline: `SHA_ENV=SF_BENCH_PANELS=... BENCH_TAG=panels3 bench.sh quick SHA SHA`.
- With no panel detached the cost is one empty-vector check per event and per frame.

## Per-monitor DPI

Panels use the app's UI scale (Preferences > Interface > UI scale, or the main window's display). On macOS and
Wayland, pixel density is handled per window (`FramebufferScale`), so text stays sharp on a Retina or 2x panel
monitor. On Windows with monitors at different scaling, a panel shows at the main window's scale; a per-window scale
needs per-scale font atlases and is a follow-up if members ask.

## Not in this slice

Docking panels into each other (tabs), detaching shelves or the Library, a panel spanning a different UI scale,
restoring Wayland positions (no protocol for it).
