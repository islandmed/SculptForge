# Graphics tablets

SculptForge reads pens through SDL3, which uses each system's own tablet support: **Windows Ink** on Windows,
the native tablet events on **macOS**, and **Wayland** (tablet-v2) or **X11** (XInput2) on Linux. Any pen the
system sees with pressure works: Wacom Intuos, Cintiq and One, XP-Pen, Huion, Gaomon, Surface Pen and others.
Nothing needs to be turned on in SculptForge: pressure is used as soon as the pen touches the canvas.

Open **Preferences > Tablet** to check the pen and tune it:

- **Pen**: whether a pen has been seen, whether it is in range or touching (and with which end), the display
  server, and on Linux the device's name. SDL 3.4 and later also says whether it's a pen display or a tablet.
- **Mouse mode (relative)**: the pen moves the cursor by how far it moves, like a mouse, and lifting it to put
  it down elsewhere on the tablet leaves the cursor where it was. It works with the tablet driver in pen mode or
  in its own mouse mode (Wacom: Mapping > Mode > Mouse; XP-Pen and Huion: Relative / Mouse mode). While the pen is
  in range SculptForge draws the arrow at the brush and hides the system's, which sits where the pen is on the
  tablet; clicks land at the arrow. Off (pen mode, the default): the cursor is where the pen is.
- **Pressure meter**: the pressure the tablet reports, and what brushes get after the curve below.
- **Draw-test area**: draw in it with the pen; the stroke is as thick as the brush pressure and the line along
  the bottom plots the pressure along the stroke. Good for checking a light touch and a full press.
- **Pressure curve** (all brushes): Soft / Linear / Firm presets, or set it yourself: **Curve** (below 1 soft,
  above 1 firm), **Min** / **Max** (brush pressure at the lightest touch and at full press), **Threshold**
  (pen pressure below it counts as the lightest touch) and **Full at** (pen pressure that already counts as a
  full press, for a light hand). It applies before each brush's own pressure curve (Brush Lab recipes).
- **Pressure > Strength / Size**: the same toggles as on the top shelf. Strength is on by default.
- **Tilt**: shown when the pen reports it (brushes don't use it yet).
- **Eraser** and the **side buttons**, below.

Settings are kept with the UI configuration: **Store Config (Ctrl+Shift+I)** saves them (as in ZBrush, nothing
is saved automatically).

## Eraser and side buttons

| Pen | Default | Choices |
|---|---|---|
| Eraser end | **Invert (Alt)**: brushes subtract; Ctrl + eraser erases mask | Invert, Smooth (Shift), Erase mask, Nothing |
| Lower side button (nearer the tip) | **Right-click**: drag to rotate the view (Shift: pan), context menus in the UI | Right-click, Invert (Alt), Smooth (Shift), Rotate view, Pan view, Nothing |
| Upper side button | **Invert (Alt)** while held | same |
| Third button (e.g. Wacom Pro Pen 3D) | **Pan view** | same |

The defaults follow the usual ZBrush setup: Alt on the pen to carve without reaching for the keyboard, and
right-click to turn the model. Invert and Smooth act exactly like holding Alt or Shift, so Alt + click on
another Part selects it and Alt + drag on the background pans. Rotate view and Pan view: hold the button
and move the pen above the tablet (or drag with the tip down).

SDL also turns pen buttons into mouse clicks, but which mouse button depends on the SDL version, so
SculptForge ignores those and uses the mapping above. If the tablet driver itself turns a side button into a
keystroke or a mouse click (Wacom Tablet Properties, the XP-Pen or Huion driver), SculptForge receives that
key or click instead of a pen button: a driver right-click still rotates the view, and a driver Alt still
inverts.

## Setup by system

**Windows**: SculptForge uses Windows Ink, and the driver's Wintab when Windows Ink is off (Wacom Tablet
Properties > Grip Pen > Mapping > **Use Windows Ink**): then the driver moves the cursor and SculptForge reads only
the pressure, tip and eraser from Wintab32.dll (loaded from System32). Windows Ink is preferred while it sends pen
events. XP-Pen and Huion drivers have a "Windows Ink" option too. Wacom Bridge (a tablet on a remote desktop)
works the same way. Surface pens work as they are. If strokes start late or the
cursor shows a ripple, turn off "Press and hold for right-click" and the pen feedback in Windows Settings >
Bluetooth & devices > Pen & Windows Ink.

**macOS**: install the tablet maker's driver and allow it under System Settings > Privacy & Security >
Accessibility (and Input Monitoring) so it can send pressure. Sidecar with an Apple Pencil works too.

**Linux**: the kernel (wacom, hid-uclogic for many XP-Pen / Huion / Gaomon models) and libinput do the work.
On Wayland, GNOME (Settings > Wacom Tablet) and KDE Plasma (System Settings > Drawing Tablet) map the tablet to
a screen and set the pen buttons; on X11 the xf86-input-wacom driver and `xsetwacom` do the same. To check
that pressure arrives at all, run `libinput debug-events` and look for `TABLET_TOOL_AXIS` lines with a
`pressure` value.

## Hot-plugging

A tablet plugged in while SculptForge runs works at once: SDL finds a new pen at its first proximity, and the
tablet's mouse device (SDL_EVENT_MOUSE_ADDED / _REMOVED) says when it came and went; the status bar shows "Tablet
connected" / "Tablet disconnected". Each new pen starts afresh (its name, whether it has pressure); unplugging
releases the buttons it held, so no Alt stays down. On Windows the Wintab context is opened again when a device
is added. The logic is `TabletHotplug` in `core/Tablet.h`.

## Troubleshooting

- **No pressure** (the meter stays at 1.00 or never moves): the system doesn't pass pressure on. On Windows
  turn Windows Ink on (above); on Linux check `libinput debug-events`. A pen that never reports pressure
  still draws, at full strength.
- **Strokes too strong or too weak**: pick Soft or Firm, or lower **Max** / raise **Min**. A heavy hand that
  always ends at full pressure: lower **Full at**.
- **The first dab is missing at the very start of a stroke**: SculptForge waits for the touch's first real
  pressure value, so a stroke begins where the pen starts pressing, never with a leftover value from the last
  stroke. This is a few milliseconds at most.
- **Lifting the pen out of range mid-stroke**: the stroke ends there and is one undo step.
- **The view shakes or jumps when you drag with the pen, or pen taps on menus miss** (0.9.4 and earlier, with
  Windows Ink, Ugee and other drivers that also send mouse events): fixed in 0.9.5. While the pen is in range,
  SculptForge follows only the pen, so a mouse takes over once the pen leaves the tablet's range (or a second
  after its last event, for drivers that never say it left).

- **The cursor jumps back to where it was when you put the pen down elsewhere** (the tablet in mouse or
  relative mode, #54): turn on **Mouse mode (relative)** in Preferences > Tablet. Windows Ink reports where the
  pen is on the tablet even in mouse mode, which SculptForge 0.10.2 and earlier followed.
- **Mouse mode felt the same as pen mode** (0.10.8, #72): it followed the system cursor, which with the driver
  in pen mode is where the pen is. Since 0.10.9 only the pen's movement counts.
- **The pen picks one brush, then stops clicking the panels and the canvas until you use the mouse** (0.10.8 and
  earlier, #95; Huion, display tablets, Wacom with Windows Ink on or off): SDL 3.2 stopped making mouse clicks
  for every pen once Windows had taken one pen away while it touched. Since 0.10.9 SculptForge makes the pen's
  clicks itself from the pen's own events, and ignores the extra mouse presses some drivers send beside them.

## For developers

The settings and the curve are in `src/core/Tablet.h` (unit tests: `tests/test_tablet.cpp`); the pen event
handling and the preferences panel in `src/app/Tablet.cpp`. UI tests drive a simulated pen with real SDL pen
events plus the mouse events SDL 3.2 synthesizes from them (which the app ignores: it makes the pen's mouse events
itself, `App::penMouseEvents`): `pen_stroke`, `pen_drag_item`, `pen_button`, `pen_hover`, `pen_enter`, `pen_drag`,
`pen_pressure`, `pen_out`, `pen_new`, `pen_lost`, `pen_stream`, `sdl_pen_mouse`, `tablet_plug` and `tablet_unplug`
(documented in `src/app/UiTest.cpp`); `tests/ui/pen_ui_windows.sfui` replays #95 (pen taps on brushes, out of
range and back, a second brush, back to the canvas) for Windows Ink, a Huion-style pen + mouse stream, SDL's stuck
pen state and Wintab-style mouse input; `tests/ui/tablet.sfui`
covers pressure, the curve, Pressure > Size, the eraser, side buttons, leaving proximity, mouse mode and the panel, and
`tests/ui/pen_navigation.sfui` view drags with the pen tip, with and without a driver's stray mouse events, and
`tests/ui/pen_windows_ink.sfui` pen taps on menus, the Move button and the S slider with Windows Ink's stray
mouse events, and `tests/ui/tablet_hotplug.sfui` plugging in and out, a pen without pressure, the pen leaving and
coming back mid-drag, side buttons, the eraser and Alt+click on a Part.
