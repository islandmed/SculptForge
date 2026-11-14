# Changelog

All notable changes to SculptForge are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.13.1] - 2026-11-14

### Fixed
- **Voxel Remesh** (Tool > Remesh, #193): areas laid in with Clay came out grainy. The tiny facets a stroke leaves in
  the mesh (invisible under smooth shading) were sampled by the grid and showed up as blotches; the distance field is
  now low-passed where the surface is smooth, keeping tips, creases and thin parts sharp, and the volume is held
  (0.02% or less). Face groups are carried to the new surface as well (they used to reset); mask and vertex paint
  already were.
- Moving or resizing Tube points (Fixes #215, thanks @maratcgartist) closed the app on a long tube (one whose full
  mesh has over 60,000 triangles): while a point is dragged the Part shows a lighter preview, and the World Size
  cursor still cast its pick ray into the picking tree of the full mesh, reading triangles past the end of the
  preview. A stale tree is now skipped and World Size waits for the rebuild. Also, one tube is swept within 2 million
  triangles (a Rope or Braid with a point dragged far could run to tens of millions), a drag or edit that cannot be
  swept keeps the last good shape, and autosave waits while a Tube point is being dragged.

## [0.13.0] - 2026-11-13

### Added
- **Tripo in Generate 3D** (with your own key): Tripo v3.1 / v3.0 from an image or a description (shape only, about
  $0.20 / $0.10). The key lives in the system keychain only; each service has one pinned address, a timeout, a capped
  wait and plain error messages; the model file is accepted only from the provider's own servers and checked before
  import (docs/ai-services.md). **Recommended models** list: price per generation, a quality note and a link to the
  provider's key page for each. **Free models that heal themselves**: the update check's feed now says which free
  Hugging Face Spaces are up, slow to start or switched off (no extra request); a switched-off Space is greyed out
  with the reason and a run moves to one that is up. When a paid service can't be reached or times out, the free
  models you already allowed run instead (never another paid service). **Remove flat background** now also cuts a
  plain backdrop (a flat colour, a vignette, a rounded icon plate) out of the picture before it is sent, so the models
  don't build the backdrop too; a picture with a busy background gets the tip "Best results: a single object on a
  plain or transparent background."
- **Say what you want** (F3 search): plain words with numbers run existing actions. `clay 40` (brush and size, `30%`
  for intensity), `intensity 30`, `size 40`, `mirror x`, `symmetry off`, `wireframe on`, `remesh 200k` (about 200 000
  triangles) or `remesh 300` (voxels), `hide all but head`, `show all`, `isolate body`. Typo-tolerant, matches the
  names in every shipped language and Part and custom brush names, shows "Clay - Size 40" before Enter and the
  action's shortcut in the result line afterwards. The last 50 commands are kept (Up arrow). Offline, no scripting:
  unknown input stays a normal search.
- **Shadows & AO** (Material > Viewport Shading in the PBR view, Preferences > Display): cast shadows from the key
  light (in PBR, the environment's brightest direction) and screen-space ambient occlusion, worked out from the drawn
  view, blurred with a depth-aware filter. Shadows and Ambient occlusion are each On, Off or Auto (the default: on
  from the Standard hardware tier up, off on the Essential tier and on phones and tablets); AO strength, AO softness
  and Shadow softness sliders; "With Lit Sphere shading" adds them to the Lit Sphere and studio shading. While you
  orbit or sculpt they step aside if the frame would fall behind, and come back when the view settles. Saved with the
  preferences; scriptable as `view_shadows`. Both off draws the view exactly as before.

## [0.12.16] - 2026-11-13

### Added
- **Pop-up colours** (Preferences > Interface > Look): pop-ups get a colour by role (a strip across the top, an edge,
  a tinted title bar and an icon after the title, on a surface one step lighter than the panels): tips and help blue,
  the welcome and the quick tour teal, What's New, the new-look card and the membership cards the SculptForge orange
  in every look, licence and merge confirmations amber, deletes and discards red. The colours follow every look and
  scheme (Classic, Forge Terminal, Phosphor, Blueprint, Match my Omarchy theme, light themes too), and each one is
  checked for AA contrast on its pop-up. **Plain** draws pop-ups as before; nothing moves either way. The User Guide
  (in the app and on the website) colours its callouts the same way, each with an icon and its label (Tip teal, Note
  blue, Not yet and Warning amber, Important red), and gives each part of the guide its own accent: the contents, the
  headings, See also and the previous / next cards.

### Changed
- **Subdivide** writes a line to the log (triangles before and after, and how long it took), so a report that ends in
  a refused Subdivide shows what the presses before it made.

### Fixed
- **A thin shell no longer shows dark specks while you orbit.** Make Shell on a dense model and a thin Thickness:
  while the view moved (Fast Navigation) or the model drew its lighter preview, the two faces of the shell were
  treated as one surface, so the inner face's dark side showed through as specks, or the shell collapsed to a single
  sheet; they cleared up at rest. A thin shell now keeps its faces apart in the moving preview and draws in full where
  the lighter preview would merge them. A user report.
- A mesh with a micro level (Geometry > Micro) drew nothing in the PBR view (Material > Viewport Shading > PBR): the
  environment and its lookup table shared texture units with the micro level's own data, so the graphics driver
  refused every draw. Lit Sphere and the other shadings were not affected.

## [0.12.15] - 2026-11-13

### Changed
- The crash log now notes a session that was ended from outside (Force Quit, the out-of-memory killer, a closed
  session) and records the terminal, quit and CPU-limit signals; a closed pipe can no longer end the app.

### Fixed
- **Android tablets** (reported on 0.12.14-beta.2): the Right Tray never opened on a tablet window under 1280 logical
  px (a 2000x1200 screen at UI scale 2.62 is 763), and neither tray opened upright. A narrow window now holds one tray
  at a time (at most 40 % of its width); a tap on a tray's handle opens it (closing the other) and a second tap closes
  it. A timeline track narrower than its margins no longer loops forever. Wider windows and the mouse behave as
  before.
- **Image to 3D / Text to 3D from an AI tool, the console or a script** no longer froze the whole window for the 30
  seconds to several minutes a hosted model takes, with no sign of progress (it looked like a crash, and
  force-quitting left no trace). The call now runs in the background with live status; the result opens when it
  arrives, and if the scene changed meanwhile nothing is replaced.
- **Free Hugging Face models** that are down, asleep or not answering no longer wait up to 15 minutes: after 4 minutes
  queued (or 7 minutes running) it says which model isn't answering and the next free Hunyuan3D model runs instead,
  with a note.
- Support, the website and other links opened from SculptForge on Linux (the AppImage on Omarchy) no longer start a
  pile of crashing Chromium windows. The browser now starts in the environment SculptForge was launched with, without
  the AppImage's own library paths, once per click (the same link twice within 2 seconds opens once), and also for the
  folders the app opens in your file manager.

## [0.12.14] - 2026-11-13

### Fixed
- **Android**: the welcome window on a phone held sideways ran off the bottom of the screen with **Close** below the
  fold, and a finger could only drag the window, not scroll it, so only the small X in its title bar closed it
  (Hameron on Discord). It is now a sheet inside the screen's safe area (camera cut-out, bars, gesture area) with a
  finger-sized **Close** pinned at the bottom, content that scrolls with a swipe (a swipe that starts on a button
  scrolls, it doesn't press it), and Back closes it, the membership card, What's New and the quick tour.
- **Help > Copy log to clipboard** was only in the command search (F3); it is now in the Help menu too, where an
  Android tester who can't open the app's folder looks for it.
- A bottom-shelf slot for one of your brushes still showed "missing" after a restart when the brush's name held one of
  the old words SculptForge renamed in 2026 (rand_gogh on Discord; the 0.12.13 fix covered renames and moves, not
  this). The shelf, the brush's key and its saved id now keep a brush's name exactly as you typed it, and a slot that
  an earlier version already changed finds its brush again by itself. If a slot still finds nothing, the log now says
  which name and id it looked for and the closest brush names.

### Changed
- **Android**: a mesh is drawn through the extension's multi-draw only when the driver lists the extensions that
  define it (otherwise one core draw call per range, and never a missing entry point), and the log records the first
  GL error a view draw raises and the first frames' triangle counts, for the empty 3D view some Mali tablets show (not
  confirmed fixed: testers).
- **Android**: the welcome's phone sheet needs a touch screen (a short desktop window with a mouse keeps its movable
  window).
- The view-draw GL error log (0.12.14 beta 2) stops after the first 300 frames on every draw path; the direct window
  draw makes no GL error query after that.

### Added
- **Tooling**: the UI test runner's step dispatch is a verb table and a switch (a 121-link `else if` chain hit MSVC's
  block-nesting limit, C1061, on the Windows build), and `scripts/check_else_if_chains.py` fails any `else if` chain
  of 100 or more under src/.

## [0.12.13] - 2026-11-12

### Added
- **Help > Copy log to clipboard**: copies the last 60 lines of the log as text, to paste into a problem report where
  the file can't be found or attached (Android hides its app folder from the Files app).
- **Human (male, A-pose)** starter mesh (File > New from Starter Mesh, Library > Tool > Starter meshes,
  `new_starter_mesh name=human-male`): an athletic adult man in A-pose, 180 cm, all quads (13 378 at the base cage)
  with edge loops round the eyes, mouth, shoulders, elbows, knees, fingers and toes, UVs, exactly symmetric, opened
  with two subdivision levels; the muscle definition (pec shelf, abs, obliques, deltoids, quads, calves) is a sculpt
  layer called Definition you can turn down. Built from the CC0 MakeHuman base mesh and its shape targets by
  tools/starter, the definition sculpted with our own brushes (not sculpted from scratch); docs/starter-meshes.md has
  the recipe and provenance.
- **Japanese interface** (日本語): menus, palettes, shelves, tooltips, Preferences, first-run and messages (3,200 texts),
  with the kana and kanji drawn from a bundled Noto Sans CJK JP subset (+170 KB). Brush, tool and product names stay
  in English. Computers whose language is Japanese start in it (OS-locale pick on the first start). Machine-translated
  and cross-checked; native review wanted.
- Japanese and Chinese text wraps at line ends without a closing mark (、。」) starting a line, an opening mark (「（)
  ending one, or a Latin name such as "Forge Spheres" cut in the middle.
- **Fine detail for AI agents (MCP)**: the server's instructions and the creature-head recipe walk an agent through
  block-out, secondary forms at about 1M triangles and detail at 3-6M, with brush sizes for each stage.
  **detail_pass** (pores, skin, wrinkles, scales, scars over a region or the whole Part; seeded, one undo step)
  refuses a mesh too coarse for the detail and says how far to subdivide. Detail presets (Detail Crease, Detail
  Wrinkle, Detail Pores, Detail Skin, Detail Scales, Detail Scar) work as brush names in stroke and stroke_screen
  without changing the user's alpha. **view_closeup** renders one spot up close along the surface, lit low, and
  reports the triangles there and the finest detail the mesh holds (read-only; in live mode the user's view doesn't
  move). Both servers, local only.
- **`--ui-test x.sfui --record out.mp4`**: a UI test script also writes every frame to a small mp4 (crf 23, `--fps`
  and `--size` honoured; needs ffmpeg), for before and after clips of a bug. `--ui-record` keeps the real-time
  launch-clip mode.
- Windows laptops with two graphics chips (Intel and NVIDIA or AMD) now start SculptForge on the faster one without a
  setting; **Preferences > Performance > Graphics card** and Windows' own graphics settings still decide if you pick
  one.
- **Shelf sliders** (masterfull95 on Discord): in Customize Shelves, Ctrl+Alt+drag on Guide Strength and Target Quads
  (Tool > Remesh) or Level (Tool > Geometry) drops the slider onto a shelf; it works there and is kept across
  restarts.

### Changed
- **Tool > Geometry > Micro Detail**: before Add Micro, the slider reads **New Micro 2 (83M)**: what Add Micro would
  make, not a level the mesh already has.
- A Japanese Get started page (sculptforge.app/getting-started-ja.html: download, switching the interface to 日本語, the
  first sculpt, basic keys, community) and a languages line under the download buttons.

### Fixed
- **Android**: a mesh shader variant the graphics driver refuses to build no longer leaves the model undrawn without a
  word: the app logs it once and draws with the next simpler variant, and the log now lists the GL vendor, shader
  language and limits (reports of a missing 3D model on two phones had no log to go on).
- **Android**: the welcome window fits a phone screen (its X and Close button were off the screen on a Pixel), no side
  panel opens on a screen as narrow as a phone held upright (one opened by dragging an edge took the whole screen and
  could not be closed again), the starting model is framed into the 3D view on a phone held upright (it filled the
  narrow view edge to edge) and again when the phone is turned, the launcher icon is the same cube on its dark tile,
  shaped by the launcher (it sat inside a white circle), and a touch whose lift was lost (switching apps, rotating,
  split screen) no longer leaves later touches ignored.
- **The Clay preview drew too much while you sculpted** (below the default Intensity, up to 1.4x the final pull): the
  stroke changed when the result caught up. The live preview now uses the same pull and plane as the final result.
  Clay Block's preview now follows its square tip and pull too, so it changes less when the result catches up.
- **Masks on Micro levels**: Move, the elastic Kelvin brushes and Detail Clone deformed the base mesh under a Micro
  level, which did not see the mask painted on the Micro level, so masked areas (a sphere's eyes) still moved. Masked
  areas now stay exactly where they are, with the soft edge of the painted mask, and a partial mask scales the move. A
  Move that reaches a mask moves the surface along its normals for that stroke (as Move along normals does); the
  Kelvin brushes and Detail Clone do nothing near a mask on a Micro level (a hint in the status line says so; they
  work as before elsewhere). A mask set on the Part itself (not painted on the Micro level) also protects the Micro
  detail. Reported by a user.
- **Vertex paint across subdivision levels**: a low-opacity stroke at a low level darkened the fine detail painted on
  a higher level to black and left a pale halo around it, because the low level's colour difference was added on top
  of detail that was not that colour. The stroke now goes up as the same blend (old colour toward the stroke colour at
  the stroke's amount), so detail it does not reach is unchanged and no channel ends outside the old and stroke
  colours.
- A bottom-shelf slot for one of your brushes no longer shows "missing" while the brush file is still on disk
  (rand_gogh on Discord). Each brush file now carries a hidden id, and the slot follows that id, so renaming the
  brush, moving its file into a folder inside the brush library, or a change of upper/lower case in its name no longer
  breaks the slot. Brush files from older versions get their id the first time the library loads (the file is saved
  again, with a copy in backups/). A brush you really deleted still shows "missing" and the slot stays until you
  remove it.

## [0.12.12] - 2026-11-12

### Added
- **Proof mode**: `sculptforge --benchmark standard-100m` runs the standard strokes on the same procedural head at 104
  million triangles (about 16 GB of free memory), and a result made with `SF_HW_SIMULATE` set records it in
  `settings.hw_simulate`, so a simulated hardware tier says so in its JSON.

### Changed
- 25 User Guide pictures re-taken on the current version, so they show today's panel names.
- The User Guide (in the app and on the website) reads like the site: each chapter opens with a hero picture and an
  "In short" box, long paragraphs are split into short ones, menu paths such as Edit > Preferences > Input show as
  chips, and Welcome starts with a card grid (Your first sculpt, Quick tour, What's new, Install, Get help). The
  membership details moved to a new Support and membership chapter. The guide's colours follow the current look, so
  light desktop themes read too.
- **Performance guide**: the "What to expect" table now shows dab times and view speeds measured with `sculptforge
  --bench` (4, 16 and 33 million triangles, median of three runs on an Apple M1 Pro laptop) and links their raw
  output; the figure for 100 million triangles is removed because it was not measured.
- The four home-page demo clips (UDIM tiles, layer transform, vector stamps, view keys) are re-recorded on the current
  version and back on the page.

### Fixed
- **Large Clay strokes showed each dab as a ring** (#196): on a form that curves away under the brush (a 120 px brush
  on a sphere) the middle of the dab stood above Clay's plane and stayed untouched, the dab lifted a ring, and the
  ring's edge was a crease, one per dab along the stroke. The plane now reaches the surface under the cursor and the
  pull is scaled by how much higher that is, so the middle lifts as it does on a flat surface and a stroke builds up
  about as much as before (a flat surface is unchanged). Light Clay strokes no longer leave a raised edge: on a
  strongly curved form they build up a little more softly than they did, because the ring they used to lift is gone (a
  heavy stroke moves the same amount of clay). Brush-pack Clay brushes follow the same plane.
- **Symmetry strokes whose two sides overlap** (a big brush near the mirror plane): the second side's plane now reads
  the surface the first side just left, so a Fast Big Brushes stroke matches the exact one.
- **Voxel Resolution in Customize Shelves** (masterfull95 on Discord, #191): Ctrl+Alt+drag on the Remesh palette's
  Resolution slider only moved the palette; it can now be dropped onto a shelf like any button, works there, and is
  kept across restarts.

## [0.12.11] - 2026-11-12

### Added
- **Palm rejection** (Android): fingers that land while the pen is in range, or in the 0.3 s after it leaves, are a
  resting palm and do nothing; a trackpad's fingers are never ignored. Off on desktop touch screens.
- **Android (early, members first; free for everyone on 26 November)**: SculptForge builds for Android tablets, phones and Chromebooks (arm64 and
  x86_64, Android 9 or later, OpenGL ES 3.2) as a sideloaded APK. Sculpt with a finger or a stylus with pressure,
  navigate with two fingers (pan, pinch to zoom, twist to roll), undo, save to the app's Projects folder and open any
  file through the system's document picker. The desktop layout for now; a tablet layout of its own comes next
  (android/README.md).
- **Palm rejection** (Android): fingers that land while the pen is in range, or in the 0.3 s after it leaves, are a
  resting palm and do nothing; a trackpad's fingers are never ignored. A pen that never reports leaving stops counting
  after 3 s. Off on desktop touch screens; **Preferences > Tablet > Ignore fingers while the pen is near** turns it on
  or off.

### Changed
- **Braid** (Tool > Tube) now builds round strands (two to begin with) that twist over and under each other round the
  curve, instead of flat ribbons. **Spread** sets the weave: 0 packs the strands together so they touch, 1 opens them
  up with longer turns. Saved braids look different. Requested by Artizmo on Discord.
- Release tooling: the members' beta is created in the insiders repository with its own tag target, so a beta no
  longer stops at "tag has not been pushed".
- Why page: the agents are described as testing for speed, stability and feel, with artists deciding what gets built.
- **Guide**: the install page now says Android is in early access for members, public on 26 November, instead of "no
  Android version".
- The start-up splash shows on every interactive start for at least 0.5 s (overlapped with start-up work), centred and
  floating, and fades into the main window. Turn it off in Preferences > Interface > Show startup splash.
- Four home-page demo clips and 25 guide screenshots showed old interface labels; the site leaves them out until they
  are re-recorded on current main.
- **Website home page**: a release's news appears once, in the announcement bar under the menu; the hero and the
  What's new heading no longer repeat it. The release card check (scripts/release.sh) looks for the bar, and a
  colon-chain tagline fails the guide check.
- **Website**: the home page and the About page now show what is built in: Blender round-trip, Gaussian splats with
  Blender 5.3, capture to sculptable mesh, quad retopology and baking, garments and cloth, and the optional AI
  brushes. Blender and splats lead in the hero on desktop and phone, with a six-tile "Built in" band below it.

### Fixed
- **Nudge and Palette Knife folded the surface over itself** (#198, #199): a second stroke across an area they had
  dragged, and a single Nudge stroke, left thousands of triangles folded over their neighbours (dark creases and
  specks). The material they push is now spread out again behind the brush; Palette Knife drags a little less per dab
  when its steps are long (at the default spacing it drags as far as before); Nudge slides along the plane of the
  brush instead of each vertex's own, noisy normal (which also keeps the two halves of a mirrored stroke identical),
  and holds back a move that would turn a triangle over while its brush covers up to about 12,000 vertices. In the
  close-up session the folded triangles fall from 1,444 to 607 (Palette Knife) and Nudge adds none (it added 4,972);
  on a replay of the same strokes Nudge goes from 5,347 to 0 and Palette Knife from 1,381 to about 620. Palette Knife
  still leaves creases where two strokes overlap. Cost: Palette Knife takes about 0.2 ms more per dab with a small
  brush and 40 to 50% more time with a very large one; Nudge about 1.2 ms more per dab with a small brush (the fold
  check), little more with a large one. Nudge reaches about 6% less far on a long stroke. Tendril is unchanged.
- The start-up splash never appeared on Wayland ("Window framebuffer support not available"); it is now drawn through
  an SDL renderer when the window has no surface, and fully drawn before the window is shown.
- **Spikes and streaks from stacked Crease and Inflate strokes** (#194): on a divided mesh (about 5 million triangles)
  Crease packs the triangles across its line, and Inflate, moving every point along its own normal, then flipped them
  (twenty overlapping strokes left about 2,000 folded triangles and needles). A dab that reaches such a packed grid
  now swells along the brush's normal instead; Inflate on an ordinary surface is unchanged.
- **Undo history and memory** (#202, slice 1; thanks @TheLycanFenrir): the undo history now fits the computer's free
  memory (a quarter of its total memory, never below 256 MB) instead of a fixed 1 GB that could run an 8 GB computer
  out of memory next to a very dense mesh, and a 64 GB computer keeps far more history. When steps must go, the status
  line says "Oldest N undo steps removed to free memory" (English, Spanish and both Chinese) instead of dropping them
  silently, and Preferences > Performance shows the live budget. Saving older steps to disk and unloading hidden Parts
  are planned for the rest of #202.

## [0.12.10] - 2026-11-11

### Added
- **Mouse orbit speed** in Preferences > Input (thanks hullalmiah on Discord): 0.1x-2x for mouse and trackpad rotation
  drags in every input style, including orbiting around the pointer; 1x is the turn as it was. The pen keeps its own
  View speed. Saved in the preferences.
- **Two-key window** (Preferences > Input): how long the second key of a two-key shortcut has, 0.35 to 1 s (0.4 s by
  default). The keys of a two-key shortcut can be pressed together, either one first, or one after the other. A key
  that is part of one runs its own action once the window is up; every other key runs at once.

### Changed
- Release tooling: the Discord release post has a header, the headline first and the release card as its image, with
  masked Download and What's new links instead of raw URLs.
- Release tooling: the release stage warns when the version's launch headline is missing, or when its keywords are not
  in the first highlight line and the What's New tagline; the promote puts the headline first in the GitHub release
  body, and stages the README's "New in" line.
- Release tooling: a staging CI run that fails in a test step gets one rerun of its failed jobs, and a test that
  passes on the rerun is logged as flaky instead of failing the release. A test that fails twice stays red.

### Fixed
- **Brush Size and Intensity** keep their value when the pointer goes past the canvas edge while you adjust (S / F, U
  / Shift+F), instead of snapping back to the old one; only a click on another control cancels.
- Crisp Flatten only cuts down now and never adds clay (thanks Hameron on Discord). Pen pressure no longer scales the
  Crisp Flatten cut depth (it left terraces across a facet), so a light-pressure stroke lands on the same plane.
  Surface below the plane is left alone at any depth, which also removes the scattered spikes, debris and stair-steps
  along the edges of its facets. The regular Flatten brush is still the one that fills dips. The brush's shelf preview
  is a little wider so the cut shows.
- **Generate 3D** (free Hugging Face models): Hunyuan3D 2, 2 mini Turbo and 2.1 and TRELLIS.2 now find their image and
  prompt inputs by name, so the Spaces adding or reordering inputs (Hunyuan3D now takes 13, TRELLIS.2 15) no longer
  breaks them. Everything else uses each Space's own defaults.
- **Symmetry with textured brushes** (#197): with X or radial symmetry the mirrored strokes of the brushes that stamp
  a pattern or an alpha (Dragon Scales, Skin Pores, Stone Carve, Wood Grain, Woven Cloth and about 25 more) now carry
  the pattern's mirror image, where they used to lay a different pattern on each side.
- two-key shortcuts also fire when both keys are pressed together (thanks Artizmo on Discord)

## [0.12.9] - 2026-11-11

### Changed
- **Tests** (contributors): a CMakeLists.txt edit that only adds new source files or a guarded `add_test` runs those
  files' tests in T0, not the whole suite; the mode matrix runs as four shards side by side; `stale_tree` runs on
  every change; a per-push tier (`SF_T0_TIER=push`: direct picks + start, sculpt, save and open) runs in about half
  the test time and is never the READY verdict; known flakes go in `tests/quarantine.txt` with an owner and a date,
  after which they block again.

### Fixed
- **Brush Size and Intensity** follow the mouse: press S (or F in the Blender style) or U, move the mouse sideways and
  the value changes live, with the ring drawn at the live size where you started (Intensity shows a disc inside it).
  Click or Enter keeps it, Esc or a right click puts it back (thanks hullalmiah on Discord).
- **Undo / Redo of a stroke on a dense model (#181).** Every Undo and Redo made the viewport send the whole model to
  the graphics card again and rebuild its distance-view cache, so the time grew with the model instead of the stroke
  (5.4M triangles with 3 subdivision levels: 470-600 ms per Ctrl+Z, now 20-30 ms in the frame; the upload itself 500
  ms, now 4-8 ms). Only the vertices the stroke changed are sent now. Thanks @TheLycanFenrir and @Devon0209 for the
  reports.

## [0.12.8] - 2026-11-11

### Added
- **Two-key shortcuts** (Preferences > Hotkeys, the >> button beside an action's key): press one key, then a different
  one within half a second (A then S). Added to the action's other keys; the same key twice is refused; a sequence
  another action uses asks first (Replace / Cancel); Backspace in that box removes only the two-key shortcuts. A first
  key that is bound on its own still works: it waits half a second and runs by itself when no second key follows.
  Requested by Artizmo on Discord.
- **Reference images** (View > Reference images; idea by Oslen on Discord): add one or more PNG or JPEG images (or
  drop them on the window while the section shows). Each sits **Behind the model** (fixed to the view) or **In the
  scene** (a plane at the front, side or top that turns with the view and fades out edge-on), with opacity, flips,
  lock and hide; **Adjust** moves it and scales it from its corners (Shift keeps its proportions). **Front + side...**
  makes a front and a side plane from two files in one click. They never take strokes or picks and are left out of
  renders, exports, thumbnails and the timelapse unless **Show in renders** is on. Projects keep them, embedded or by
  path; images over 4096 px are scaled down, files over 64 MB, 64 megapixels or 8192 px a side refused (read from the
  header, before decoding), 8 per project. An image a project names by path opens linked, with Embed off, so saving
  never copies a file from your computer into it unless you tick Embed. Scriptable: `reference_add`, `reference_set`,
  `reference_remove`, `reference_pair`.

### Fixed
- **App would not start on some Linux NVIDIA drivers** (0.12.5 regression): most likely the start-up splash left its
  own OpenGL context current, so the main shaders compiled in the wrong context ("Shader compile error" with garbage
  text, thanks @dCo3lh0 (#192)). The main context is made current again after every splash step, the splash draws a
  plain software surface, and a start-up shader error now says "no current OpenGL context" or "(empty driver log)"
  instead of random bytes.
- **A fast mouse or trackpad stroke right after a pen stroke** laid only 2 dabs (a mark at each end instead of a line,
  #195): while the pen is in range the mouse's motion is ignored, because some tablet drivers send stray mouse events
  beside the pen's, and that also swallowed a stroke the mouse had started. A stroke the mouse began now keeps all of
  its motion.

## [0.12.7] - 2026-11-11

### Added
- "**Alpha pool and jitter** (Sculpt > Alpha): **Scale jitter** and **Position jitter** per dab, kept per brush, and
  an alpha pool (`brush_alpha_pool`): a Library folder of images or a few picked alphas, one drawn at random for each
  dab or Drag Stamp, repeatable per stroke. Three procedural wood-grain alphas (Procedural Wood Straight, Cathedral,
  Weathered) are named as procedural, not scans. Each stroke now starts its Angle jitter from its own seed, so two
  strokes no longer jitter identically. Thanks Hameron."
- **Gaussian splats in the Blender bridge** (Blender 5.3+, which draws and imports splats but cannot export them):
  **Send to SculptForge** sends a selected splat object as a standard 3DGS PLY with its transform; **File > Blender >
  Send to Blender** sends the loaded splats back and updates the same object in place. Positions, scales, rotations,
  opacity and spherical harmonics (up to degree 3) are kept, nothing is baked. The add-on also adds **File > Export >
  Gaussian Splats (.ply)** and a drop handler for `.splat` files; older Blender versions get a message and the mesh
  bridge is unchanged. The exchange manifest has an optional `splats` key (older versions skip it). The CI add-on job
  now also runs bpy 5.3.0.
- **Crisp Flatten** brush in the Hard Surface pack (thanks _tfx on Discord): the plane is taken where the stroke
  starts and held for the whole stroke, and the edge is hard with a thin anti-aliased band (about 2.5 polygons wide),
  so one stroke cuts one flat facet and two meet in a crisp bevel. It cuts what is above the plane and fills only
  shallow dips, so it never builds a wall past a curving form. Works with symmetry, masks and Dynamic topology. Brush
  Lab recipes get **Lock plane** (off: the plane follows the surface) and **Crisp edge**, also as `planeLock` /
  `crispEdge` in .sfbrush files and `brush_create`.
- **Warnings for more destructive operations** (#173, thanks @TheLycanFenrir): Delete Masked, Remove Hidden, Split,
  Mirror, Make Symmetric and Merge by Distance rebuild the mesh and drop the subdivision levels, so on a Part with
  levels they now ask first, like Voxel Remesh, Quad Remesh and Decimate do (Freeze Levels and Proceed / Cancel).
  Ctrl+Z still brings the levels back, a Part without levels is not asked, and "Keep original" splits don't ask.
  **Preferences > Interface > Ask before dropping subdivision levels** turns the asking off or back on for all of them
  (and Subdivide).
- **Shift and Ctrl brushes**: Shift+click a brush on the bottom shelf (or in the brush picker) to make it the Shift
  brush, Ctrl+click one on the shelf for the Ctrl brush; the main brush stays. Each keeps its own Intensity, the tile
  shows a Shift / Ctrl badge and the status line names it. Shift+click Smooth, a mask brush picked with Ctrl held, or
  **Reset Shift brush** / **Reset Ctrl brush** in the command palette go back; Smooth's alternate smooth (#40),
  Ctrl+Alt unmask, the background mask rectangle and Ctrl+Shift are unchanged. Kept in the preferences; scriptable
  with `set_modifier_brush shift=trim ctrl=flatten`. Asked for by Cytrick on Discord.
- **Orbit around the pointer** (Edit > Preferences > Input, every key style; on for new installs, off for existing
  settings so their orbit doesn't change): a rotation drag that starts over the model turns the view about the surface
  point under the pointer instead of the scene centre, without the view jumping; the target moves to that point's
  depth, so zooming afterwards works there too (auto depth). Started over the background or on the axis gizmo's disc,
  it turns about the target as before. Turntable, orbit locks and Shift snapping are kept. The point is picked once
  per drag (thanks dm4nd on Discord).
- **Booleans between Parts** (Tool > Parts): each Part's **Boolean** (None, Add, Subtract, Intersect) is set from the
  mark in its row or its right-click menu; the shown Parts with one combine in list order, previewed live in the view
  (as you sculpt or move them on lighter meshes, on release on heavy ones). **Make Boolean Mesh** writes the result
  into a new Part, one undo step, with options to weld the seams, keep each source's face groups and give the cut
  faces a face group of their own. The Boolean is exact (watertight when the Parts are; faces away from the cut, with
  their face groups, vertex paint and UVs, are kept as they are; flush and coplanar faces handled), with a voxel
  fallback for Parts that aren't closed (the status line says which it used). Saved in projects. Commands
  `part_boolean`, `make_boolean_mesh`, `boolean_parts` (scripts, Python, MCP). Requested by foirgairwolf and GABO on
  Discord.
- "**My Panel** (Edit > My Panel; Requested by Hameron): hold Alt and drag a button or a section's title from any
  palette or shelf into a panel of your own; a section keeps all its live controls. It sits in a tray or its own
  window like any palette, or floats at the cursor on F4 (rebindable; Esc closes it). Alt+drag reorders, dragging an
  entry out or right-click removes it, and it is saved with the layout."
- "**Smooth Peaks** brush: smooths like Smooth, but only brings down the vertices that sit above the average of their
  neighbours (along their normal); valleys are never filled. Works on the mesh and on micro levels, with masks,
  symmetry and alphas. Shift-smooth still uses the plain Smooth. Requested by ABHITAR."
- **Strands** (Tool > Tube): Rope and Braid take 1 to 8 strands (3 as before); a rope of one strand is a spring, and
  Spread opens it. `tube_set` and `tube_new` take `strands`.
- **Repeat along curve** (Tool > Tube, with a Tube or Curve Part active): pick another Part and its copies are laid
  end to end along the curve as a new Part, with Spacing (1 end to end, less overlaps them like chain links) and Turn
  (each copy turned that many degrees more than the one before; 90 for chain links). One undo step; the curve and the
  Part are kept. New command `tube_repeat`. Requested by Artizmo on Discord.
- **A leaner Welcome**: about half the words, the 10-minute lesson first with the Quick tour beside it, the same
  looks, keys, starter meshes and AI On/Off. The privacy line is one sentence ("Your art is never uploaded or used for
  training.") with Details for the full text. Beside the User Guide, small grey icons for Discord, GitHub, X, Bluesky,
  Instagram and Facebook (also in the About header): a click opens that page in your browser, and the app makes no
  request to any of them on its own.
- Layout autosave (thanks raffaele_3d_design on Discord). `ui.json` keeps the layout in use ("layout") and the Save
  Layout one ("layoutSaved"); older files read as both. Revert to Saved Layout returns only the layout, not shortcuts
  or preferences. Reset Layout, `--reset-layout` and Reset Layout and Shortcuts still win, and Revert still returns to
  the Save Layout one afterwards. The console, Library and Customize Shelves being open are not part of it.
  `ui.prev.json` and `ui.prev2.json` keep the two copies of `ui.json` from before the last sessions that changed the
  layout.

### Changed
- **Braid** strands are now flat ribbons that pass over and under each other instead of three round strands like a
  rope. A saved braid keeps its stored mesh until the tube is edited, then it is rebuilt in the new look. Requested by
  Artizmo on Discord.
- The membership links in the app (Become a member, the yearly Member checkout, Founding Studio) carry a source tag,
  `client_reference_id=app-card`, so a checkout started in the app is attributed to it. Nothing else is sent; no
  tracking, no new preferences.

### Fixed
- Layout edits (Customize Shelves, trays, palettes) were lost on quit, a crash or Revert unless Save Layout was
  pressed first (reported by raffaele_3d_design on Discord).
- SpaceMouse (#110, thanks @fenixpoints): a SpaceMouse that SDL listed but could not open (3DxWare held it) was never
  tried again until it was replugged; SculptForge now retries every 3 seconds. Running alongside 3DxWare and the Wacom
  conflict are still open.
- **Threads above 16** (#184, thanks @HG4Science): the saved count was read after the built-in stamps had already
  started the thread pool at the default 16, so a 48-thread machine kept using 16. The saved count is now read before
  anything starts the pool.
- **Undo / Redo of a whole-model step on a very dense mesh (#181).** A step that replaces the mesh (Voxel Remesh and
  the other tools that remesh) now keeps the old mesh's BVH with it, so undo and redo swap the two instead of building
  a tree again (0.7 s of a 12M-triangle undo, now 0 ms; the tree isn't counted against the undo byte budget, so it
  doesn't push the remesh step out of the history). Stroke undo and redo were already O(touched vertices); tests now
  hold that (time and step size). Undoing a Dynamic Topology stroke no longer builds the tree again either: only the
  triangles the stroke changed are updated (115 ms to 23-30 ms at 1.3M triangles, and it no longer grows with the
  whole mesh there). Thanks @TheLycanFenrir and @Devon0209 for the reports.

## [0.12.6] - 2026-11-11

### Added
- **Garments: drape on the body** (Tool > Garments > Drape on the body, `garment_place`, `garment_drape`): each panel
  placed by a click on the body (in front of it, facing the view; Turn, Wrap, Gap), saved with the pattern; the sewn
  pattern simulated live on the visible Parts (rest shape = the flat pattern, seams pulled shut).
- **Garments: Convert to Part**: the draped garment as the Part "Garment": seams welded, UVs from the flat pattern,
  the pattern's thickness, one undo step.
- "**Save Visible Parts As...** (File): saves a new .sfproj with only the visible Parts (their materials, layers,
  folders and linked copies kept, the cameras and settings too). The open project and its file stay as they are. Parts
  with streamed detail need Save As for now. Requested by phoenix."
- **Featured creator** card in the Welcome window: a picture, the video's title and creator, and Watch (opens the
  browser; no embedded player). The list is `featured.json` beside `latest.json` on the same host, read with the daily
  update check and kept with its thumbnails in the preferences folder; a different entry each start, the x hides one
  for good. Watch links go only to https youtube.com, youtu.be and vimeo.com, thumbnails only from sculptforge.app;
  nothing is shown when the list is empty, the site can't be reached or update checks are off. First featured: askNK.
- **Reset Layout** (Preferences > Saving & Layouts, command palette): the built-in shelves and trays with every panel
  back in the main window, shortcuts and preferences kept. `sculptforge --reset-layout` does the same at start. A
  shelf widget (colour picker, brush tiles, sliders) taken off a shelf is listed there with a Put back button.
- **Mask colour** (Edit > Preferences > Interface): choose the colour masked areas show in the viewport; **Mask
  darkness** sets how much of it shows, **Reset** goes back to the default darkening look. Saved in the preferences.
  Requested by ABHITAR.

### Changed
- **Startup splash**: the start-up is also checked just before the first frame (a slow first frame or panel restore
  showed no splash), and once the splash is up it stays at least 0.7 s so it is seen, not flashed. A start that is
  over within 0.3 s still shows no splash and gets no added wait. `SF_SPLASH=1` (environment variable) shows it on
  that run whatever the start-up time or the preference, for 0.7 s at least, to see it on a fast computer.

### Fixed
- **Alt+click to select a Part** (also Triple-click and Alt+Q) often missed thin, small or distant Parts, and a
  trackpad click that moved 4 points began a stroke instead. The pick now takes a Part within 8 points of the pointer
  when it is in front of the Part being sculpted, and a click may move 6 points (reported by phoenix on macOS).
- **Drape**: cloth resting on a faceted body no longer creeps sideways
- **Drape** on a figure modelled in centimetres or millimetres (the Full Body starter, most scans): the Units per
  metre slider now starts from the size of the scene (centimetres above 20 units tall, millimetres above 600) instead
  of 1, so the cloth falls and folds at a real-world speed instead of as if the figure were 100 times its size. Move
  the slider to set it yourself; the `cloth_drape` command still defaults to 1.
- A project whose Part list is empty no longer fails with "Project has no valid Part list"; it opens as an empty scene
  (settings, lights, cameras, views and shelves load) and saves back the same.
- The guide's 3D printing section pointed to File > Bridge > Export STL; Export STL is under **File > Export**.
- **Pan and zoom keys did nothing** (0.12.5: the keys' glide ran in the test runner's frame loop only, never in the
  app's own; reported by Artizmo on Discord). A tap now moves the view about a tenth of a second's worth, a hold
  glides, on the default and on rebound keys. Tests: `ui_key_nav_loop` drives the app's real loop
  (`--measure-key-nav`), `ui_key_nav` the rest.
- Revert to Saved Layout, Reset Layout and Import Interface run between frames instead of inside the palette that
  asked, so they can no longer swap the layout under it (reported by phoenix and raffaele_3d_design on macOS,
  Discord). Unsaved work is autosaved first; the first autosave of a session keeps the previous session's as
  `autosave.prev.sfproj` (Library > QuickSave), and the Restore prompt offers it.
- **Vertex paint lost on a subdivision round trip**: fine colour painted at the highest level was overwritten with the
  interpolated low-level colour after painting at a lower level and stepping back up. A low-level stroke now goes up
  as a blended delta added to the colour the higher levels already hold (vertex paint, Base and paint layers). Undo
  across levels and Voxel Remesh colour transfer are covered by tests. Reported by masterfull95 on Discord.

## [0.12.5] - 2026-11-10

### Added
- **Startup splash**: when start-up takes longer than about 0.3 s, a small window with the SculptForge bust, the
  wordmark and the version shows until the main window has drawn its first frame; the main window starts hidden
  instead of empty. Never in headless runs, tests or benchmarks (SDL_VIDEO_DRIVER=offscreen). **Preferences >
  Interface > Show startup splash** (on by default). Splash design idea by @lowenski (#176).

### Changed
- **Pan and zoom keys** (Ctrl+Numpad 4, 6, 8, 2, Zoom in/out) glide: a held key eases in and out (about 0.12 s each)
  and moves by speed x time every frame, so a 1-second hold covers the same distance at 30 and 144 fps, instead of a
  step, a quarter-second pause and then the hold. No OS key repeat is involved (requested by Artizmo on Discord).
- The guide's Welcome chapter says membership early builds (each release about two weeks early) start with SculptForge
  2027, the name of the official launch. Version numbers are unchanged.
- **Testing**: end-to-end modelling scenarios (tests/e2e) run inside the existing test suites: sculpting, Voxel Remesh
  at 2M triangles, Extract, shelves, paint, tubes, cloth, Parts, export round trips, preferences and quitting. Nothing
  changes in the app.

### Fixed
- Generate 3D: when a free Hugging Face model finishes without a model, or refuses the job, the panel now says so in
  red under Generate, with the service name (it used to show only in the status bar, so it looked silent). Text to 3D
  on the two free Hunyuan3D Spaces is switched off by their owners; the message says that and points to Image to 3D or
  another provider. Photos from the phone use the same message.
- **Member builds** (Preferences > Updates): the GitHub sign-in always ends with a visible message, including an
  account that is not yet on the member list or has an invitation to accept, a declined sign-in and an expired code
  (reported by a member). The sign-in's polling also retries a GitHub 5xx or rate limit instead of stopping. The
  device flow is now one tested function (all outcomes, `slow_down`, expiry, cancel).
- **Quitting with Alt+F4 on Linux** (#183, X11): the window now stays until Alt and F4 are released (2 seconds at
  most). Closed while they were still down, the held key repeat or the release reached the desktop behind it, where
  Alt+F4 can be Log out. Stopping a helper program (splat trainer, COLMAP, ffmpeg) also can only signal that helper
  now: never our own process group, never everything (a pid of 0 or -1), never one that was already gone.
- With **Rgb** on, a Sub stroke (Alt, or Sub toggled) laid no colour: it painted back towards white instead, so only
  Add strokes coloured the Part. Sub now lays the colour like Add, with the same alpha and Colour intensity, for every
  brush and stroke type. The Paint brush itself still paints back to white on Alt.
- **A custom brush lost from the bottom shelf** (rand_gogh): a slot whose brush wasn't loaded kept its name and looked
  usable. It now shows a missing mark with a tooltip, finds the brush again if its file comes back (or is in the new
  backups folder), and Remove from shelf clears it.
- **Brush files are saved more safely**: a save that doesn't fully reach the disk no longer replaces the good file,
  the file being replaced and a deleted brush are copied to `backups/` in the brush library first, a brush file that
  can't be read is kept (and listed) and restored from that copy, and brushes that exist only in this session survive
  a library reload (installing or switching a pack).
- **Thread count capped at 16** (#184): a Threads value set by hand (Preferences > Performance, or
  `set_performance_override threads`) above 16 is used at the next start, up to the computer's hardware threads; the
  default is still every hardware thread up to 16. The paint-layer, transform, multires and texture loops follow the
  same count.

## [0.12.4] - 2026-11-10

### Added
- Shortcuts ask before they take a key. Press a key that another action uses and a Replace / Cancel box names that
  action (Enter replaces, Esc cancels); before, the key moved silently. Requested by Artizmo on Discord.
- A changed shortcut has a reset arrow beside it in Preferences > Hotkeys that puts its default back (and takes that
  default from whoever holds it now).
- Pan left, Pan right, Pan up and Pan down (Ctrl+Numpad 4, 6, 8, 2 by default, in every key style, bindable in
  Hotkeys). A press steps; holding the key keeps moving, and so do Zoom in and Zoom out. Preferences > Input > Pan and
  zoom key speed sets how fast.

### Changed
- **Front faces only** (Brush palette) is easy to leave on, and then Smooth and every other brush skip the inner side
  of a thin or Extract-made Part, so spikes inside look unreachable. While it is on, the brush cursor carries a "Front
  faces only" tag and the Brush palette starts with "Front faces only is ON" (a tooltip says it skips hidden and inner
  surfaces) and a Turn off button. Reported by rand_gogh, who found Shift-smooth "does next to nothing" until the
  option was unticked.

### Fixed
- Ctrl+Alt+click on a shelf button no longer starts a shortcut change when the pointer moved (a customize drag rebound
  keys such as F). Reported by Fenix on Discord.

## [0.12.3] - 2026-11-10

### Added
- **Language on the first start** (#44): the first start reads the operating system's interface language (Windows
  display language, macOS preferred language, `LC_ALL` / `LC_MESSAGES` / `LANG` on Linux) and, for Spanish,
  Traditional Chinese (zh-TW, zh-HK, zh-MO, zh-Hant) and Simplified Chinese, starts in it with a one-time note,
  "SculptForge is in Español. Change", in that language and in English. Any other language stays English, and the
  welcome (also Help > Welcome) has a Language row with the four names in their own script that switches live. Never
  on a headless display, in tests, renders or demos, or twice.
- **Turn a Slide Stamp with a key** (#175, thanks @Devon0209): with the Slide Stamp stroke, hold Shift after the drag
  has begun and the stamp stays where it is while its alpha turns toward the cursor; let go and it slides on with the
  turn kept. The turn belongs to that stroke and the brush's Angle is not changed. Shift held at the press still
  smooths. **Edit > Preferences > Input > Slide Stamp rotates with** picks Shift, Ctrl, Alt or Off.

## [0.12.2] - 2026-11-10

### Added
- **Curve Part** (Tool > Tube > Result > Curve; `tube_new curve=true`): a guide line drawn on or over the surface,
  with editable points, **Resample** (`tube_resample count=N`, exactly N evenly spaced points) and **Tube from curve**
  (`tube_from_curve`), which uses the Tube tool's sweep. New Tube profiles **Rope** and **Braid** (three strands round
  or across the curve) and **Sleeve** (hollow, open ends), and the **Twist**, **Spin** and **Spread** modifiers
  (`twist`, `spin`, `spread` in `tube_new`, `tube_set`). Requested by Hameron, GABO and Artizmo.
- **Origin tools** in the Gizmo panel (requested by Ziel on Discord, with the Origin tools idea from @Devon0209):
  **Center on origin** moves the Part's bounding-box centre to 0,0,0 and **Drop to floor** rests its lowest point on Y
  = 0, each one undo step per Part, with **All Parts** together. Also the commands `center_on_origin` and
  `drop_to_floor` for scripts and the AI assistant.
- **Frame keys** (idea by fenix on Discord): with the Classic sculpting keys and more than one Part, F frames the
  active Part and F twice within **Frame double-tap** (Preferences > Input, 400 ms, 0 = off) frames the whole scene. A
  scene with one Part frames as before. **Frame scene** is a bindable action.
- **Unlit view** (#168, thanks @nagdav853): <kbd>Alt</kbd>+<kbd>L</kbd>, Render > Materials > Unlit and the right
  shelf's View list (`set_unlit`; the right shelf has no room for another default button, but Customize can add one)
  show vertex paint, texture paint and the PBR base colour with no lighting, Lit Sphere or PBR shading. The mask tint
  and wireframe still draw; the status bar shows Unlit while it is on. Saved with the project.
- **Wood brush pack** (a seventh built-in pack, ten brushes: Weathered Planks, Raised Grain, Splinters, Wood Chaff,
  Grain Cracks, Check Split, Knot, End Rings, Plank Seams, Board Gap) with six new procedural alphas (Weathered Grain,
  Splinters, Grain Cracks, Knot, End Rings, Plank Seams) that work with any brush. Requested by Hameron.

### Fixed
- **Customize Shelves** (Ctrl+Alt+drag): buttons that don't apply right now (the Front option, Vertex / Texture paint
  without a texture) and the shelf's value fields (Size, Intensity, Detail) can be moved again. Reported by fenix on
  Discord and MasterFULL on Discord.
- **Right shelf** with the console open: the view toggles fold into the View button only when the shelf really is too
  short, and unfold only for a taller shelf; the buttons grow back only for a taller shelf, so the shelf should hold
  still (reported by Hameron; please say if it still moves).
- **Drape**: sewn seams stay closed where the cloth rests on the body (a T-shirt's waist and armpit seams stay within
  about 4 mm instead of opening to 6-9 mm), and a drape with one particle flipping between two spots a millimetre
  apart now comes to rest.
- **Shift-smooth used the wrong Intensity** (#180, thanks @nekirk11): Shift with Drag Stamp strokes smoothed at the
  main brush's Intensity instead of Smooth's own, and the Intensity slider turned back to the main value as soon as
  the stroke began (so Smooth Intensity 10 looked like 80). Every stroke type now reads Smooth's value when Shift made
  it a smooth stroke.
- **Stencil** in the SculptForge layout: Sculpt > Alpha > Stencil shows Adjust and Tile again, and the Remove (X)
  button (they sat past the panel's right edge, so only the Classic layout had them). Thanks MasterFULL on Discord.

## [0.12.1] - 2026-11-09

### Added
- Focus Detail in projects: **Divide to Micro** past 300 million micro triangles streams the new level from a detail
  file (`.sfdetail`) instead of holding it all in memory; full detail loads where you look and sculpt. Save writes
  `<project>.sfdetail` beside the `.sfproj` (only the changed tiles, compacted once half the file is garbage) and
  opening the project streams from it again. Project format 9 (only for projects with a streamed level). Linux and
  macOS for now: Windows keeps such levels in memory, and a project with a streamed level opens on Linux and macOS
  only until Windows streaming lands.
- **Panels in their own windows** (multi-monitor): any palette, the User Guide and Controls open in a window of their
  own from the new icon in their header or toolbar, to keep on another monitor or a pen display. Where each window was
  is remembered between sessions (per computer, panels.json, not in shared interface files); a monitor that has gone
  brings the panel back beside the main window. Shortcuts keep working while a panel window has the keyboard. **Reset
  panel windows** (Preferences > Saving & Layouts, command palette; also part of Reset Layout and Shortcuts) puts
  every panel back. On Wayland the compositor places the windows (their size is remembered, not their position).
  Requested by Richard Throgmorton (MakIt3D), Founding Member #1's Member Request.
- **Voxel Remesh up to 4096** (foirgairwolf on Discord): **Tool > Remesh > Resolution** and the `remesh` command go
  from 32 to 4096 (was 512). The slider keeps two thirds of its travel for 32-512 and a sixth each for 528-2048 and
  2112-4096; Ctrl+click types a number. Above 512 only the voxels near the surface are stored (8x8x8 bricks), so
  memory grows with the surface, not the volume, and the result is the same surface the full grid would make. A
  sculpted head remeshes at 1024 into about 5.6M triangles in about 3 s, at 2048 into about 22M in about 9 s.
- **Triangle estimate** under the Voxel Remesh Resolution slider (*About 1.6M triangles*), live as it moves; above 512
  it also shows the memory needed, in red when more than is free.
- **High resolution remesh** confirmation: the first remesh above 512 shows the estimated triangles and memory (Remesh
  / Cancel); it asks again after the Resolution goes back to 512 or less. A remesh that won't fit in the free memory
  doesn't start (the app and the `remesh` command both say how much it needs); it is cancellable with progress
  throughout.
- Voxel Remesh, Quad Remesh and Decimate ask before they drop your subdivision levels ("This drops 3 subdivision
  levels"), with Freeze Levels and Proceed or Cancel, and Don't ask again. Nothing changes on a Part without levels.
  Thanks @TheLycanFenrir (#173).

### Changed
- **Forge Spheres** Draw builds without switching modes: a click on a sphere grows a new one, a drag on its body moves
  it and a drag on its rim resizes it (the cursor and a tag beside it say which); each drag is one undo step. Move and
  Scale work as before, and Drag places child (or Ctrl+drag) still grows and places a new sphere in one drag. Thanks
  @tristratos (#167)
- **Founding Studio** is now for the first 5 studios (was 10): a flat $150 a month, locked, on the Help > Support
  card, the user guide's Studio membership and docs/studios.md.
- **Keyboard and mouse** guide: Option is Alt on macOS, and how to make Option Alt and Cmd Ctrl when the Windows
  version runs under CrossOver or Wine on a Mac (their Mac driver sends Cmd as Alt by default; thanks @fenixpoints).
- Focus Detail (experimental, `--focus-detail`): the GPU now keeps a small record per tile (coarse grid and edges) and
  full-resolution grids only for the tiles drawn close up, within a fixed pool; coarse tiles are shaded at their own
  level, so a streamed level opens shaded at once instead of shading every full grid first.
- Website: the Membership page thanks every Founding Member by their credit name (the same list as the Members page),
  and `build_site.py --supporters` rewrites that line with the list, so a newly credited founder shows up in both.

### Fixed
- Focus Detail: saving a project with a streamed level kept only the coarse detail; bake, displacement export, surface
  noise and copies of a streamed level now use full detail.
- **Find Problems > Fix all** left non-manifold edges and opened holes on messy meshes, such as a rough remesh
  (foirgairwolf on Discord). Solids touching along an edge now come apart closed, Fix all repeats until only holes are
  left, the Holes Fix never makes other problems or leaves more holes than it found, and the panel says what couldn't
  be fixed and why. The inner wall of a hollow (or an eye inside a head) is no longer counted as flipped.
- **Alpha stamps leaving gaps on curved meshes** (#174, thanks @Devon0209): a large Drag Stamp or Slide Stamp (any
  alpha, sculpt or paint) only reached the vertices within its radius in space, so where the surface curved away from
  the view part of the stamp's footprint stayed untouched. The stamp now also takes the visible surface under its
  footprint.
- Clay, Clay Block, Flatten and Trim on micro levels have a safeguard against stretched triangles on big moves: points
  facing away from the brush plane stay put, and no point moves more than a quarter of the brush per dab. Ordinary
  strokes look and feel the same.
- **Drape** settles where it used to keep twitching: cloth pressed into a narrow fold of the body (an armpit, the
  inside of an elbow) stays put instead of bouncing between the two sides, and sewn seams no longer pull the cloth
  through the body while they close. It now runs until the cloth has really stopped, which can take a moment longer
  (still under 10 s for a 2M-triangle body).
- Solo in the Parts panel now remembers which Parts were shown when it began and restores exactly those on exit, so
  Parts hidden beforehand stay hidden (also across save and load while in Solo). Thanks MasterFULL on Discord

## [0.12.0] - 2026-11-08

### Added
- A first-use notice before the first request to each third-party AI host (Pollinations.ai, each Hugging Face Space
  family, fal.ai, Meshy, Hyper3D, a remote OpenAI-compatible server or ComfyUI), drawn inside the AI Brush or Generate
  3D panel (no dialog): what is sent, the host, who pays, and **Allow** / **Use local instead** (a running Ollama, LM
  Studio or ComfyUI) or **Set up local** / **Cancel**. Nothing is sent before Allow: the request functions refuse on
  their own, so scripts, `sculptforge-cli` and AI tools get the same refusal until the service is allowed in the app.
  Allow is kept per service in `ai.json` (`sendAllowed`); **Preferences > AI > Online AI services** lists each one as
  Allowed, Asks first, or set by the studio, with **Revoke**. Servers on this computer (localhost, 127.0.0.1, ::1)
  never ask. A studio policy pre-allows what its `ai.providers` / `ai.endpoints` list and its "no" wins over an
  artist's Allow. The guide's interface chapter has a plain "What leaves your computer" table. No new network host,
  key or server.
- **Cloth brush**: simulates the surface near the stroke as fabric (our own XPBD cloth solver, docs/design/cloth.md):
  Drag pulls it along and it buckles into folds that keep their length, Push presses it in (Alt pulls), Settle drapes
  it under gravity, Grab holds what it took and drags it (Move-like), Expand / Pinch spread or gather it, Inflate adds
  fabric so it balloons and wrinkles. Soft body springs it back toward its shape; self-collision keeps folds from
  passing through each other. Fabric presets Silk, Cotton, Denim and Leather; collides with the other visible Parts;
  masked areas hold like pins; symmetry, sculpt layers, one undo step per stroke. Asked for on YouTube.
- **Drape** (Tool > Drape, `cloth_drape`): the active Part simulated as cloth (our XPBD solver) until it rests on the
  other visible Parts; fabric presets, real-size scale (units per metre), pins from the mask, Simulate / Pause / Keep
  / Reset with a live view, one undo step. The cloth doesn't pass through thin, sharp shapes (needle tips, fingers,
  curls) or through the body when it falls fast. Tip: drop the cloth from in front of overhanging shapes (beards,
  chins), or it settles behind them.
- **Crease** / **Uncrease** in Tool > Geometry > Edges (#108): a crease value (0..1, 1 = fully sharp) on the selected
  edges of the base level, one undo step each, saved in the project (an optional blob: older versions open the file
  without creases) and kept on the base while stepping levels. Creased edges are drawn in blue on the base level in
  every mode. The `edge_crease` command does the same from scripts. **Add Level honours creases** (Catmull-Clark and
  Loop semi-sharp creases: sharpness 10 x crease², s - 1 per level, fractional blending): creasing at Level 1 under
  existing levels reshapes them with their detail; Remove Below keeps what is left of the creases on the new base.
  Without creases subdivision is bit-for-bit unchanged. **USD** export / import carries them (`creaseIndices` /
  `creaseLengths` / `creaseSharpnesses`, `subdivisionScheme` catmullClark or loop on a creased cage); glTF, OBJ, FBX,
  PLY and STL have none (documented). Thanks _tfx on Discord for asking; the plan for edge creases is @NicTanghe's
  (#108).
- **Edges mode** (Tool > Geometry > Edges, #108): picks edges of a Part's base level (Level 1; a Part with levels
  steps down to it) on the model: click selects the edge under the cursor, Shift+click adds, Ctrl+click removes, a
  double-click takes the edge loop (across quads, or along an open border). **Select by Angle** selects the edges
  whose faces meet at more than the Angle (30 degrees by default), optionally **only within the selection**; **Grow**,
  **Shrink**, **Open Edges**, **Invert** and **Deselect All**. Quad diagonals are never edges. The `edge_select`
  command does the same from scripts. Thanks _tfx on Discord for asking for select by angle; the plan for edge creases
  is @NicTanghe's (#108). Creases themselves come next.
- **Pattern window: Templates and Print** (Tool > Garments): **Templates** lists the T-shirt, trousers, straight and
  A-line skirts, hoodie, dress, cloak and gloves, with **Add beside the pattern** for an outfit and an editable **Size
  chart (mm)** (hover a measurement to see how to take it); **Print** picks A4 or Letter, portrait or landscape, shows
  how many pages it takes, and saves one PDF or one SVG per page. Both open under their buttons. Spanish and Chinese
  included.
- **Garments, part 2: the Pattern window** (Tool > Garments > Pattern): the garment's panels flat at real size over a
  10 / 100 mm grid with a scale bar. Start from a T-shirt or a rectangle, or import an SVG; drag points (snapping to
  the 5 mm grid and other panels' points; Ctrl drags freely), curve handles or whole panels; Alt+click an edge to add
  a point, Delete removes it, C curves an edge. Sew by clicking two edges: every seam is listed by its edges' names
  ("Front left side <-> Back left side") with both lengths, green when they match, red when one side is eased in;
  hover a seam or an edge to see what it is sewn to. Duplicate, Mirror Copy (the other sleeve), Mirror, rename,
  particle distance, fabric and thickness; export SVG or DXF. Ctrl+Z / Ctrl+Shift+Z over the window undo and redo
  pattern edits. In English, Spanish and Chinese.
- **Garment templates** (#garments): trousers, straight and A-line skirts (with waistband), hoodie (long sleeves,
  two-piece hood, kangaroo pocket), dress (bodice and A-line skirt), cloak (half-circle cape with hood) and gloves (a
  pair), next to the T-shirt. Flat panels at real size from a size chart saved in the project (chest, waist, hip,
  neck, shoulder, arm, inseam, rise, head, hand and more: `garment_size`), sewn the way each garment is made with the
  seam lengths matched; `garment_new template=... add=true` builds an outfit in one pattern. Our own proportions,
  documented in docs/design/garment-templates.md. Their seams read by name in the Pattern window ("Left front inseam
  <-> Left back inseam", "Left sleeve cuff <-> ..."), as the T-shirt's do; mirrored panels keep the names. **Printing
  patterns at home**: `garment_export` writes tiled PDF (or one SVG per page) for A4 or Letter, portrait or landscape:
  trim lines, half triangles that complete across each join, page names (A1, A2, ... B1), the 50 mm check square on
  A1; blank pages are left out and a mirrored or repeated piece prints once, marked "cut 2, 1 reversed". The
  whole-sheet SVG and the DXF for cutters stay. Guide chapter [Garments](docs/guide/garments.md).
- **Generate 3D candidates**: **Candidates** (1-3) makes several shapes in one go, side by side as a shaded thumbnail
  over a flat black silhouette, so the strongest form is picked before any sculpting. Free and local services start at
  3; fal.ai, Meshy and Hyper3D start at 1 (each candidate is one generation on your own key, and the window says how
  many are billed). The pick becomes a Part (only an untouched untitled scene is replaced); the others are discarded
  or, with **Keep the others as hidden Parts**, kept hidden. "Same shape as A" when a service returns one shape twice.
- **Use as sculpting base** (Generate 3D): one click after picking runs Voxel Remesh, then Quad Remesh (**Then Quad
  Remesh**, on by default), for a clean, even mesh with one subdivision level. **Keep a high-detail copy (hidden)**
  keeps the model as generated as a hidden Part below it; **Project Detail from the high-detail copy** brings its
  detail back after Add Level.
- **Pose all Parts** (Tool > Parts, `figure_pose_begin` / `figure_pose_apply` / `figure_pose_cancel`): the visible
  Parts become one light Pose Mesh (each Part reduced to its share of 300k triangles, one face group per Part, so Mask
  by Face group picks Parts; a Part with subdivision levels joins from a lower level, so it is ready in seconds). Pose
  it with the Gizmo, masks, soft joints and symmetry; Undo steps back through the pose. Apply Pose moves every Part at
  full resolution the same way (sculpt layers, subdivision and micro levels follow), as one undo step across all
  Parts; Cancel Pose leaves everything as it was. Requested by @ABHITAR.
- **Tube shapes** (Tool > Tube > Shape): Circle, Square (sharp corners), Triangle and Ribbon cross-sections, Thickness
  for ribbons, Turn, and Taper start / Taper end; they set new tubes and the selected Tube Part (live while a slider
  is held, one undo step), and the Symmetry X copy is the exact mirror image. `tube_new` / `tube_set` take `profile`,
  `thickness`, `turn`, `up`, `taper_start`, `taper_end`. Requested by GABO on Discord.
- **Point spacing** (Tool > Tube > Stroke) sets how far apart a drawn tube's points are, and **Simplify** (Tool > Tube
  > Edit, `tube_simplify`) takes about a third of a tube's points out per press, the ones the curve needs least.
  Requested by GABO on Discord.
- **View toggles on the right shelf**: Turntable (View > Orbit lock: Y or Free), Ortho (Numpad 5), Floor (View > Floor
  grid) and Flat (Render > Materials > Smooth | Flat), lit while on. Customised layouts get them below Rotate and
  Paint. In a window too short for the column they fold into one View button (a list), and shorter still the column's
  buttons shrink to their icons: nothing is cut off. Thanks foirgairwolf on Discord.
- **Live brush ring on the Size slider**: while the top shelf's, the Draw palette's or the quick menu's Size slider
  drags, the brush ring shows on the canvas at the new size (where the cursor last lay on the model, else the middle
  of the view) and fades out about half a second after release. Thanks ABHITAR on Discord.
- **Sandboxed plugins** (#20): a pack's `plugins/` folder may hold `.sfplugin` files, WebAssembly modules that turn a
  mesh into a mesh (deformers: one undo step, mask-aware; generators: a new Part). They run in the wasmtime sandbox
  with no imports at all (no files, network, clock or system access), a fresh instance per run, a time limit (20 s,
  plus Stop), a work limit and a memory cap; one that crashes or runs too long is turned off with the reason, and the
  mesh is never touched. Their settings come from the plugin's manifest and are drawn by SculptForge; the pack review
  shows each plugin with "Permissions: none". They run only when you run them (never on install, when a project opens,
  from a macro or from an AI agent). `plugin_list`, `plugin_run`, `plugin_enable`; a sample Noise Displace plugin, a C
  header (sdk/plugin) and tools/plugin/sfplugin.py for authors. Thanks to @bribblej-web for the request.
- **Finer UI scale steps** (Preferences > Interface): 110%, 115% and 135% between 100% and 150%, and 250% and 300% for
  5K and 5K2K monitors run at their native resolution. Thanks @fenixpoints for the suggestion.
- **Masked only** (Tool > Geometry > Merge by Distance): only masked points merge, with each other; the count and the
  orange preview dots follow it. `weld_points masked_only=true` for scripts. Thanks foirgairwolf on Discord.
- Quad Remesh, Voxel Remesh, Decimate and Merge by Distance ask before working on a partly hidden Part, in the app:
  **Unhide and continue** shows everything and goes on (Undo hides it again), **Cancel** changes nothing. Merge by
  Distance with Masked only and the mask shown doesn't ask. Thanks @TheLycanFenrir (#163).
- **Preferences > Tablet > Troubleshooting details**: the SDL version, the video driver (wayland or x11 on Linux), the
  session and package, the pen's events since launch, lost pressure samples, the system's own mouse events while the
  pen was in range, the last pressure and the last stroke's range; **Copy details** copies them. Running through
  XWayland, it says how to start SculptForge on Wayland (`SDL_VIDEO_DRIVER=wayland`). **Classic pen timing** there
  (off by default) brings back the pen timing of 0.11.15 and before on every platform, a one-click workaround should
  the new timing misbehave with a tablet.

### Changed
- Generate 3D no longer remeshes as part of generating: generate for form, then Use as sculpting base for topology.
  The Remesh drop-down is replaced by the Sculpting base section; the CLI's image_to_3d / text_to_3d
  (remesh=quad|voxel|false) are unchanged.
- **Gizmo look** (fenix on Discord's design, @fenixpoints): All (Shift+W) is cleaner and easier to read. Each axis is
  a thin line with its scale ball part way out and its move arrow at the end, with clear space before the rings (the
  scale boxes past the rings are gone); rings seen edge-on and their back halves fade so they don't pile up over the
  centre; the plane squares are lighter; the centre, view ring and even-scale tabs are light grey (charcoal on a light
  flat background) so the yellow highlight stands out on every handle. Move and Scale use the same arrows and balls,
  and every handle has a dark edge so it reads on light backgrounds too. Keys, handles and behaviour are unchanged.
- **Tube**: **Snap to surfaces** is on by default, so a stroke drawn across a model follows its surface instead of
  sinking into it, and pulled off the model it carries on into the air from the edge. With **Draw Tube** on, a plain
  drag always draws, even over an old tube's dot; Shift+drag (or Ctrl, Alt) edits the dot. Reported by GABO on
  Discord.
- **Tool > Reshape** tidied (foirgairwolf on Discord): **Size**, then the **Stack**, each under its heading. The
  Polish / Relax / Smooth / Balloon sliders and "Smooth keeps volume" above the Stack are removed. Polish, Relax,
  Smooth and Balloon are now steps in Tool > Reshape > Stack (Add step > Surface); their menu items and keys still run
  them at 50. Smooth's step has **Keep volume**; the `deform_polish`, `deform_relax`, `deform_smooth` and
  `deform_balloon` commands are unchanged.
- The interface's font texture is about 5x smaller (6 MB instead of 32 MB at 100%, 40 MB instead of 256 MB at 300%):
  only the icons the app uses are loaded.
- Tool sub-palettes renamed and reordered by task: Reshape, Texture Image and Viewport Look (were the generic deform,
  texture and display names); Reshape ordered Smooth, Relax, Polish, Balloon; Mirror moved to Tool > Geometry >
  Symmetry; Make Shell (Extract's button), Find Problems (Tool > Geometry > Cleanup, also in F3), Preferences > Saving
  & Layouts. Sub-palette and action ids are unchanged (tool.deformation, tool.texture, tool.display,
  preferences.config, deformation.mirror, part.extract), so saved layouts, hotkeys, shelves and scripts keep working;
  translations updated (Spanish, Traditional and Simplified Chinese).
- Headless runs (the offscreen or dummy video driver, UI test scripts, or `SF_NO_DIALOGS=1`) never open a system file
  dialog, a message box or the browser: every one goes through one wrapper, which logs the request and answers cancel
  (or a test's scripted path). A source check (`native_dialogs`) fails on any direct call.
- No press on a floating card or panel over the 3D view starts a stroke underneath, even before the UI has seen the
  pointer arrive (the first-run cards, the Quick Tour, banners).
- **Quad Remesh is faster**, most of all on small models (reported by factoberplay on YouTube): most of it used to run
  on one core. Its solvers (the multigrid setup, the conjugate-gradient passes, the grid map's matrix assembly), the
  proxy surface's new vertices and the BVH build of the sculpt now run on all threads. Measured on the floor-tier
  laptop at the defaults (5000 quads, median of 5): 1k triangles 1242 to 740 ms, 10k 1544 to 1001 ms, 100k 1524 to
  1087 ms, 1M 2562 to 1793 ms, 10M 11.9 to 7.3 s. Smooth forms (no hard edges) also get a coarser helper surface,
  which they remesh as well on (14 test sculpts: distance to the sculpt, poles, angles and edge spread the same or
  better): 1k 1242 to 541 ms, 10k 1544 to 709 ms, 100k 1524 to 794 ms. Models with hard edges keep the denser one and
  the same mesh as before.
- **Voxel Remesh and the legacy Quad Remesh are faster on big sculpts**: the open-border check, the shell
  rasterisation and the BVH build run on all threads (Voxel Remesh at 10M triangles 4.0 to 1.6 s, 1M 546 to 352 ms;
  legacy Quad Remesh at 10M 11.0 to 7.1 s). Same results.

### Fixed
- **Leaving the gizmo**: Esc, or picking a brush, now leaves Move, Scale, Rotate and All; before, strokes kept driving
  the gizmo until Draw (Q) was picked.
- Cut: the flat cap and the walls a cut leaves were fans of long thin triangles between the outline's points (the cut
  looked messy in Wire and only a remesh fixed it). They are now filled with even triangles about the mesh's own edge
  length, and the triangles inside each cut mesh triangle are rearranged the same way. The outline stays exactly where
  it was, so the result is as watertight as before (thanks dm4nd on Discord).
- The Arch package builds with strict compiler flags (`-Werror=format-security`, as CachyOS's makepkg sets them): text
  and tooltips are drawn as plain text, never read as printf formats, so a translation or name holding `%` can't
  garble a label or crash (thanks @itotochi1107, #162). Every Linux and macOS build now fails on a non-literal format,
  so it stays fixed.
- Text and icons were soft on Retina and other 2x displays (5K monitors, scaled Wayland outputs): the font atlas was
  rasterised at 1x and stretched. It now follows the window's pixel density, and moving the window between a 1x and a
  2x monitor rebuilds it.
- Picking a brush, Draw, Move, Scale or Rotate leaves **Brush > Capture from view** while it waits for its square;
  before, the next drag on the model captured instead of sculpting.
- After **Tool > Forge Spheres > New Forge Sphere**, picking a brush, Draw, Move, Scale or Rotate, or pressing Esc
  goes back to the Part you were sculpting (a Forge Sphere Part isn't sculpted until Make Mesh); before, brushes did
  nothing until you picked that Part again.
- Picking a brush, Draw, Move, Scale or Rotate leaves **Color > Line Gradient** (a gradient just painted stays as it
  is); before, drags on the model kept drawing gradient lines after another brush was picked.
- The **Quad Remesh Guides** brush is left by Esc and by Draw, Move, Scale and Rotate too (before, only another brush
  left it), and the status bar then says so instead of still describing the Guides brush.
- **Tool > Facial Shapes > Place Landmarks** is left by Esc, any brush and Draw, Move, Scale or Rotate (before, clicks
  kept placing landmarks after another brush was picked).
- With a menu or pop-up open (a palette from the top menu or a task menu, the command palette, a right-click menu), a
  click on a shelf brush, a tool button or a stroke on the model now does what it says and closes the menu; before,
  the first click (and with the top menus every click on the brush shelf) only closed it, or did nothing.
- Esc closes the **Export timelapse** dialog (Sculpt Timeline), like the other dialogs.
- Paint mode ignored the Intensity slider: Intensity 1 and 100 painted the same, with every stroke type, with or
  without an alpha. The slider set the sculpt strength, which Paint doesn't use. In Paint mode Intensity is now the
  Colour intensity (the same value as I); sculpting keeps its own Intensity, and RGB while sculpting still paints at
  the Colour intensity (#164). Thanks @fxrtst
- **Quad Remesh on thin, pointed parts** (reported by Oslen on Discord): a long part that tapers to a sharp tip, like
  a horn, a claw or a blade, could take a very long time at high **Target Quads** and come out with folded, tangled
  quads along its whole length, and the quads stopped short of the point. The tip now gets its own handling: the quads
  converge on it like a pencil's and end on the point. Measured on a 2.5 m horn (41k triangles) on our floor-tier
  laptop: at 5,000 quads 3.5 s to 2.4 s, the distance to the sculpt 7.9 % to 0.5 % of its size, poles 1,533 to 846; at
  20,000 quads 16.1 s to 7.1 s, 6.7 % to 0.4 %, 6,036 to 4,580 poles; at 50,000 quads 45.6 s to 13.3 s, 0.5 % to 0.3
  %, 18,714 to 13,430 poles. Models without sharp tips give the same mesh as before; the full-body starter's
  fingertips fit closer (1.5 % to 1.1 %).
- **Starter meshes**: the Human Head and Stylised Head had two thin spikes hanging below the neck at every subdivision
  level; the cut now follows one of the base mesh's edge loops, so the open rim is an even ring. The Bust's chest and
  arm cuts, the Hand's wrist and the Ear's skin edge are clean too, and the Torso now ends in a ring round each upper
  thigh and at the top of each upper arm. The faces themselves are unchanged.
- The first-run tier card ("Tuned for your machine"): **Got it** could do nothing on Linux, and the card came back on
  every start (#161, thanks @chipset-tech). A press that lands on the card the same frame the pointer gets there (a
  trackpad tap, a pen touching down, a click in one batch of events with the move) now always reaches its buttons, and
  never the model underneath; the card also has a close button and goes away with Esc or Enter. It is remembered for
  each graphics card a laptop starts on, and for memory sizes the driver reports a little differently between starts.
- **Tube**: Ctrl+Z (or Redo) in the middle of a tube stroke or a point drag left the tube's mesh and its points out of
  step (two tubes on screen, one set of points, and later edits refused). Esc, Undo and Redo now drop the stroke or
  put the point back. Esc during a stroke also drops it now instead of making the tube.
- **Tube**: with a pen, a tube always started at full thickness however lightly it touched down (the pressure from
  before the touch was used); it starts at the pen's first real pressure now. Reported by GABO on Discord.
- Draw Tube (Tool > Tube) and Parts > Append > Insert on the model stayed on after picking another brush or tool, so
  the next drag still drew a tube or inserted a mesh and the other tools seemed dead. Any brush (shelf, picker,
  Library, its key, the same brush again), Draw / Move / Scale / Rotate, or Esc now leaves both; with a Tube Part
  selected by its points, a brush goes back to the Part before it. Reported by GABO on Discord.
- **Pen pressure on Wayland and X11** (Huguillon on YouTube): the Linux packages now carry SDL 3.4.18 (was 3.2.30) and
  the AUR package needs it: a Wayland pen no longer counts as a new pen each time it comes into range, and its eraser
  end inverts as set in Preferences > Tablet instead of drawing as the tip. A stroke sample's dabs now get that
  sample's own pressure: SDL sends the motion before the pressure (every platform), and the stroke took the sample
  before's. On Linux a sample whose pressure SDL lost (X11 valuators left out, a Wayland frame with a hover distance)
  keeps the last pressure instead of dropping to 0 mid-stroke; a NaN or infinite pressure is ignored everywhere. Touch
  events SDL made from the pen are off, so a finger on a pen display beside the pen no longer ends the stroke as a
  two-finger gesture.
- **Merge by Distance** (Tool > Geometry): the orange dots for the points that would merge show only while the
  distance is dragged, hovered or focused with the keyboard, and fade out after. Before, they showed whenever the
  panel was open, which on a dense model looked like a rendering fault.

### Security
- Plugins: the design and threat model (sandbox escape, denial of service, spoofing, supply chain) are in
  docs/design/plugins.md. The runtime is wasmtime 50.0.1, pinned by SHA-256 and updated every release.
- Interface text, translations and tooltips are never interpreted as printf formats (#162).

## [0.11.16] - 2026-11-07

### Added
- **Gizmo All mode** (Shift+W, top shelf, Sculpt > Symmetry & Gizmo; the `gizmo_mode` command): move, rotate and scale
  handles at once, with clear priorities where they cross and no overlaps at any zoom or UI scale. Snapping, the
  readout, posing with a mask, Alt+drag for the pivot and one undo step per drag work as in the single modes. Idea by
  fenix on Discord (thanks @fenixpoints, #118).
- **Keep original** checkbox under **Tool > Parts > Split** (Hidden / Masked / Unmasked; `keep=true` on
  `part_split_hidden`, `part_split_masked`, `part_split_unmasked`): the triangles are copied into the new Part (UVs,
  face groups, vertex paint, sculpt layers; the copy is cut at the current subdivision level) and the source Part
  stays untouched, levels and mask included. One undo step removes the copy. Requested by _tfx on Discord.
- Website: **Most-wanted brushes** on the brushes page (sculptforge.app/brushes.html#requests): brush requests from
  the #ideas forum on our Discord, ranked by 👍 votes and refreshed every 30 minutes, with shipped ones marked. The
  data is also published as sculptforge.app/brush-requests.json.

### Changed
- Release packages are built once per set of build inputs: a commit whose packaged files are byte for byte an earlier
  green build's (a website or release-paperwork change) reuses that build's packages, verified and attested as before,
  instead of building them again (docs/releasing.md, "Build once, promote").
- **Monthly challenge** on Discord: submissions close on the 27th at 23:59, then everyone votes in a Discord poll
  until the end of the month; the winner (Featured Artist and a spot on sculptforge.app) is announced on the 1st. The
  website and the guide's Discord page say so.
- **Member builds** in the app now come from the SculptForge organization; update to keep signing in.
- Tool > Geometry is reorganised by task (Levels, Micro Detail, Resolution, Cleanup, Symmetry) with clearer names;
  your hotkeys, scripts and search keep working.
- Membership wording in the app, the guide and the README now matches the membership page: **Member Requests** are one
  request a month, built first and credited to you (Help > Support, the welcome screen and the membership card;
  Spanish and Chinese too). Bugs are always free, and every idea goes in the public feature queue.

### Fixed
- Blender input style: Ctrl+Shift did nothing on the model (Ctrl is Blender's invert, so Ctrl+Shift became an inverted
  smooth), so the Cut shapes and the Ctrl+Shift hide/isolate gestures were out of reach. Ctrl+Shift now keeps its
  meaning in every style (drag: hide or cut; add Alt: inside / the other side; Ctrl+Shift+click the brush tile or
  Ctrl+Shift+B: the selection and Cut shapes). Ctrl alone still inverts, Shift alone still smooths. Alt+Q (pick the
  Part under the cursor, Blender's own key) is now listed with the Blender keys, since Alt+click masks there (thanks
  dm4nd on Discord).
- Cut with symmetry on: a shape whose mirror image overlaps it (a rectangle across the middle, a centred one seen at
  an angle) sometimes cut one side only, In and Add went deeper on one side, and a later cut across those caps could
  leave a crack. The mirror image now measures its depth on the Part as it was, cuts stop at once instead of leaving
  one side done, and the zero-area slivers the overlap left in the cap are cleaned up so the next cut closes (thanks
  dm4nd on Discord).
- **Tool > Parts > Extract** and **Project** labels were cut off in a narrow tray (since 0.11.11). The settings now
  read **Thickness**, **Edge Smooth**, **Both Sides**, **Corners** and **Border**; a pair shares a row only when both
  labels fit, else each takes its own, and Both Sides / Corners / Border wrap. Scripts use `triangle_corners` /
  `triangle_border`; the old argument and action names still work.
- macOS: **SculptForge > Preferences...** in the app menu was greyed out; it now opens Preferences, as Cmd+, and Edit
  > Preferences do (#111, thanks @fenixpoints).
- Popups, menus, tooltips, the Command Palette, the Quick Menu and floating windows are fully opaque in every look
  (Classic, Forge Terminal and its schemes, Phosphor, Blueprint); at 200% the Preferences popup showed the Sculpt
  Timeline's "Save with project" and the shelf text through it. Overlays on the 3D view keep their translucency.

## [0.11.15] - 2026-11-07

### Added
- **Tube** (Tool > Tube): draw tubes freely in 3D space, like a 3D pen. Strokes lie on a plane facing the view (at the
  surface where the stroke starts, the pivot, or a fixed distance), optionally snapped onto surfaces; pen pressure
  sets the radius and the stroke is smoothed into a curve. The tube is swept along it with rings placed closer in
  bends and frames that never twist, round or flat ends, and Symmetry X. Each stroke is a Tube Part whose points are
  dragged (Ctrl: thickness, Alt+click: delete), inserted and deleted until **Bake**, or goes straight into the active
  Part with even edge lengths for dynamic topology; one undo step each. Script and AI commands `tube_new`,
  `tube_list`, `tube_set_point`, `tube_insert_point`, `tube_delete_point`, `tube_set`, `tube_bake`. Guide chapter
  "Tube: draw in 3D space". Requested by GABO on Discord.
- **Hardware tiers** (Edit > Preferences > Performance): the detected tier and why ("RTX 3060, 12 GB VRAM, 32 GB RAM:
  High"), what it turns on, Auto or any tier by hand. The Essential tier works exactly as before; Standard and up keep
  more Focus Detail in memory, and High and up allow 16K paint and bake textures. A small card on first launch (and
  after a hardware change) shows the tier once; the chip in the top bar opens the panel. A memory guard steps the tier
  down a level when free memory runs low, and back up when there is room. **Turbo** (the bolt in the top bar, View >
  Turbo, Ctrl+Alt+T): antialiasing off and a lighter view while you sculpt and orbit, the detected tier's top budgets;
  full quality when the view stops, and Off changes nothing you set. Remembered per computer. **Graphics card** picker
  on computers with two (applies on the next start, through the system's own setting; a card that fails to start falls
  back to the system's choice). **Compute: Auto / CPU / GPU**, with each task's path shown (all CPU for now).
  SF_HW_SIMULATE=floor|standard|high|extreme, SF_TIER_<NAME>, SF_COMPUTE=cpu|gpu, SF_TURBO=1 for benches. **Memory in
  the top bar**, beside the triangles and frame rate: SculptForge's RAM and the system's free RAM, and the graphics
  card's memory in use where its driver reports it (NVIDIA; AMD shows what's free), updated once a second; hover for
  the detail. Thanks, Oslen on Discord.
- **Split > Hidden / Masked / Unmasked** (Tool > Parts; `part_split_hidden`, `part_split_masked`,
  `part_split_unmasked`): the triangles leave the active Part for a new Part below it (same folder, visibility and
  shading), one undo step that also removes the new Part. A triangle is masked when its corners' average mask is above
  50%. UVs, face groups, vertex paint and sculpt layers carry over; subdivision levels are cut at the current level
  (undo restores them). Thanks foirgairwolf on Discord.
- **Delete masked** (Tool > Visibility; `delete_masked`), the mirror of Remove Hidden. Thanks foirgairwolf on Discord.
- A per-Part shading mark in the Parts list (Scene / Smooth / Flat; `part_shading`). Thanks foirgairwolf on Discord.
- **Merge by Distance** (Tool > Geometry; `weld_points distance=... preview=true`): the points of the active Part
  within the distance merge into the nearest kept one; collapsed triangles go; one undo step. The count follows the
  slider and the points that would merge show in orange. Thanks foirgairwolf on Discord.
- **Find Problems** (Tool > Geometry; `mesh_check`, `mesh_fix issue=...`): non-manifold edges (Fix loosens the extra
  triangles), holes (capped as Fill Holes), duplicate and degenerate triangles (deleted), zero-area triangles
  (shortest edge collapsed), flipped triangles and inside-out closed pieces (rewound), isolated points (removed). Fix
  all fixes everything except holes, as one undo step. Thanks foirgairwolf on Discord.
- **Recent colours** (Oslen on Discord): under the left shelf's two swatches, the last eight paint colours a stroke or
  Fill used, newest first, in rows of four; click one to paint with it. Kept in the preferences, so they survive a
  restart.
- **View speed** (Preferences > Tablet): how far the pen turns, pans and zooms the view per pixel, 0.10 to 2.00 (1.00,
  the default, moves it as the mouse does). A tablet spreads its whole area over the screen, so a short pen movement
  crosses many pixels and orbiting seemed far faster than with the mouse (Huion on Windows; thanks to
  GonzaloHernandez3D on Reddit). Saved in the preferences; Tablet defaults sets it back to 1.00.

### Fixed
- Holding a rebound Smaller brush / Larger brush key (and the 1 px Shift steps) repeats again, like [ and ] always
  did; after a rebind it changed the size once and stopped. Those keys also step through an insert set. Not while
  typing in a field (thanks Ziel on Discord).
- Forge Spheres with Symmetry on: a new sphere placed near the centre line (closer than its own radius) now snaps onto
  it as one sphere instead of two overlapping twins that skinned as separate branches. Spheres growing from a mirrored
  arm or leg still get their twin (thanks dm4nd on Discord).
- **Frame** (F, the Frame button), a new model and **Reset View** fit the model into the canvas the open Library and
  the Sculpt Timeline strip leave free: at 200 % on a 1080p screen the sphere's top sat under them. Opening or closing
  the Library doesn't move the view; the next Frame uses the new free area.
- The left shelf's brush tiles and colour picker shrink to fit its column (and the wheel scrolls it past that): at 150
  % on a 1080p screen the colour swatches ran under the bottom shelf.
- Pen taps on menus and buttons work the first time, also a tap within one frame or right after the pen comes into
  range, with Windows Ink and with Huion's extra mouse events (reported by GonzaloHernandez3D on Reddit; fixed by the
  pen click rework in 0.10.9, now covered by a test).
- **Micro levels**: big Clay, Clay Block, Flatten and Trim strokes on a Part with a Micro level could break the
  surface into shards across the whole brush (since 0.11.10), and sculpting there had become much slower on very dense
  meshes. Both are back to how 0.11.9 worked, and so is its one known issue: where several Clay strokes end on the
  same spot of a rough surface, a few short spikes can show. A proper fix for that comes in 0.11.16.
- The side columns (the shelf with Undo and Redo, the brush and colour shelf) and the trays no longer scroll sideways.
  At some UI scales their tiles were a few pixels wider than the column, so a sideways swipe or scroll moved them; the
  tiles now fit the column and the trays only scroll up and down (dm4nd on Discord).

## [0.11.14] - 2026-11-07

### Added
- **Deformation stack** (Tool > Reshape > Stack, requested by foirgairwolf on Discord): thirteen whole-Part shape
  operations in four groups (Transform: Move, Turn, Scale, Squash; Bend & Twist: Bend, Twist, Taper, Shear; Shape:
  Round, Box, Flatten, Sag, Depth; plus the Surface ones), each along any of X, Y and Z about the Part's centre (or
  the gizmo's placed pivot), with Direction for Bend and Shear and Soft for an eased profile. The preview is the stack
  applied as one undo step and follows each change; Apply, Apply Selected, Reset and Repeat. The mask holds what it
  covers. Commands `deform_move` ... `deform_depth` and `deform_stack` for scripts and the AI.
- **Size** (Tool > Reshape, requested by foirgairwolf on Discord): the active Part's X, Y and Z dimensions in
  scene units, to drag or type exactly; it resizes about its centre (Uniform: in proportion), one undo step. Command
  `deform_size`.
- Top bar heart (Help > Support SculptForge); a small badge for verified members. Hidden in studio builds and before
  it would crowd a narrow top bar.
- Note fragments take `member_request: true` (and `member_credit: @handle` when the member opted in): What's New tags
  the change "Requested by a member".
- Website: **Feature queue** (sculptforge.app/queue.html, footer link on the membership page): every feature idea with
  its status (building, queued, shipped, already in the app, declined) and a member flag.

### Changed
- **Blender add-on and Maya script have their own versions** and can be released on their own, as soon as they change
  (GitHub releases `blender-vX.Y.Z` / `maya-vX.Y.Z`); the website's download links follow the newest one. The add-on
  is now 1.0.0 (it used to carry the app's version). Each SculptForge release still includes both.
- Membership card: milestone moments (10th start, 10 hours of stroke time, 100th save; local counts only, nothing
  sent), an optional honest line from the update feed's `support` block (funded this month, Founding Members taken,
  member requests this release), and a backoff (monthly after the 3rd "Not now", only after an update after the 6th).
  Every existing rule stays: never modal, never mid-stroke or during a job, never in tests, renders or CI, never for
  members, at most once a week.
- **Compact bars** (factoberplay on YouTube): the Library strip and the Sculpt Timeline take 158 logical px instead of
  228 (31% less). The Library's thumbnails are 64 px by default (were 96) and its tab row 26 px; drag the strip's edge
  toward the canvas to size them (48-128 px, kept with the layout; also **Edit > Preferences > Library >
  Thumbnails**). The timeline is one row (step count, track, icon buttons) with thin branch lanes; the full strip with
  its header row comes back from the unfold icon or **Edit > Preferences > Interface > Compact timeline**.
- The membership cards link to the membership terms (**Terms & refunds**, sculptforge.app/terms.html), and open only
  their fixed links.
- The built-in **Stone Carve** brush (Sculpt > Brush, and the community gallery) has a name of our own. Shelves, key
  bindings, pie menus and scripts saved with its old name still find it.
- Website: **How requests work** on the membership page (sculptforge.app/support.html#requests): bugs are always free;
  every idea goes in a public queue; members' requests are built first, one guaranteed request a month each (built and
  credited); Founding Members and Studio go first; a monthly member vote sets the order; popular ideas from everyone
  still get built. A "Built for members" list shows what member requests became.
- Website: Studio membership now lists your own private repository and build (a branch with the features and fixes you
  ask for, rebased onto every release), a private Discord space for your team and a seat on the Studio steering
  committee (one representative per studio).

### Fixed
- The one-line Linux installer (`curl -fsSL .../install/install.sh | sh`) downloads the AppImage again: it asked for
  `SculptForge-X.Y.Z-linux-x86_64.AppImage` (and `linux-arm64`) while releases name it
  `SculptForge-X.Y.Z-x86_64.AppImage` and `-aarch64.AppImage`, so it stopped with a 404. Checked against
  SHA256SUMS.txt as before. A test and a release check now fail when an install script asks for an asset the release
  doesn't have (#159, thanks @justblack1556).

## [0.11.13] - 2026-11-07

### Added
- **Generate 3D > Remove flat background** (on by default): a thin wall behind or ground under the subject, covering
  most of the model's bounds, is removed from the generated mesh before remeshing, as its own undo step; a relief or a
  coin on its own is kept. `image_to_3d` / `text_to_3d` take `remove_background=false`, and the new
  `remove_flat_background` command does it on the active Part. Requested by fenix on Discord.
- **Linked Duplicates** (#94, asked for by ebarranco; the idea of placing copies on their own by NicTanghe): **Tool >
  Parts > Linked Duplicate** adds a Part sharing the active one's mesh, undo history and GPU buffers, with its own
  transform. Strokes, masking and painting on any copy edit the shared mesh (symmetry about the copy's own axes); the
  Gizmo and `transform` move a linked copy as a whole (a mask poses the shared mesh instead); **Make Unique** breaks
  the link; deleting one keeps the others. The Parts list shows a link and the count. Projects keep the links (project
  format 8); glTF reuses one mesh with a node per copy, USD writes instanceable prims referencing the first copy (and
  reads them back), OBJ / STL / FBX / PLY write each copy where it is. Commands `part_duplicate_linked` and
  `part_make_unique`. 500 linked copies use about the memory of one mesh.
- **Orbit lock** (View > Orbit lock, a request from foirgairwolf on Discord): besides Free and Y (the turntable),
  rotation drags can turn the view about the world X or Z axis only, with no drift over long drags. A key for each
  (Alt+Shift+F / Y / X / Z) and the `orbit_lock axis=free|y|x|z` command; the axis gizmo draws the locked axis through
  its disc with a lock badge. Shift still snaps to side views. Saved with the preferences (also Preferences >
  Interface / Input > Rotation; older versions read Y as Turntable).
- Website: **Jobs** (sculptforge.app/jobs.html, footer link): open sculpting and 3D modeling roles worldwide (games,
  film and VFX, animation, collectibles, 3D printing, jewelry and dental CAD, AR/VR) from the companies' own public
  job feeds, filtered by region, sector, role, Remote and contract. A daily GitHub Action (tools/jobs/fetch.py,
  sources in tools/jobs/studios.yaml) refreshes it; titles, companies and places only, never job descriptions.

### Changed
- The window title starts with the project name and a "*" for unsaved changes (taskbars stay readable), then
  SculptForge and its version, and the top bar shows the version, small and dim, beside the name in both layouts (a
  narrow window drops it). A narrow window or a high UI scale (a 1024 px window at 200 %) never draws top bar items
  over each other any more: the version goes first, then the layout presets and Save Snapshot, the search field
  shrinks to its icon, and the menus that don't fit open from a >> button. Betas show their tag ("0.12.0 beta.2") in
  the accent colour; builds from a git checkout show "dev" and their commit.

### Fixed
- The membership card now shows the Studio membership's flat price for any number of artists.
- **Rebuild Lower Levels** (Tool > Geometry): the levels it builds keep each face inside one UV island when the imported
  high-poly's seams run through the lower level's faces (UVs made on the high-poly, automatic unwraps with overlapping
  islands). Such faces used to take each corner's UV from a different island and stretch across UDIM tiles.
  Reconstructing again on such a mesh also finds the right lower level now. Thanks @RodTDai for the report, the
  screenshots and the diagnosis that pointed to the fix (#145).
- Timelapse: a PBR timelapse of a high-detail sculpt flickered a little in the video (not in the view): after each
  step of the history the frames switched between the full mesh and its lighter preview levels. Timelapse frames and
  `--render` turntables now always draw the full mesh. Thanks @ebarranco.
- The Sculpt Timeline strip's header no longer runs under **Save with project** in a narrow window or at a high UI
  scale (a 1024 px window at 200 %): the step count, branches, step name and message are shortened with "..." to fit.
- **Member builds sign-in** accepts GitHub's longer tokens (its new ~520-character format, and up to 4096 characters):
  before, a long token was refused and the sign-in failed without a word. On Windows, where one Credential Manager
  entry holds at most 2560 bytes, a longer sign-in is kept across several entries (studio sign-ins too); sign-ins
  stored by earlier versions still load.

## [0.11.12] - 2026-11-07

### Added
- **Garments, part 1** (scripting and the AI assistant for now; the Pattern window follows): pattern panels at real
  size (millimetres, curves, notches, grain line, seam allowance) saved in the project, a T-shirt template (front,
  back, two sleeves, sewn), seams with length and direction checks, even triangulation at a chosen particle distance
  with seams matched point for point, export as a real-size SVG for printing and sewing (with a 50 mm scale square) or
  DXF for cutters, and SVG panels read back in. Commands: garment_new, garment_info, garment_add_panel, garment_sew,
  garment_export, garment_import and more.

### Changed
- The app icon is the orange outline cube again, as it was before 0.11 (window, taskbar, dock, Start menu, Linux
  packages).
- Linux packaging: every AppImage build checks that it starts on 4, 16 and 64 KiB memory pages (no bundled jemalloc,
  64 KiB ELF alignment on ARM64). On Apple Silicon Linux (Asahi) the guide points to the ARM64 build: the x86_64 one
  runs there only through the FEX emulator, whose own jemalloc stops with "Unsupported system page size".
- **Maya Live link** (#42): on very large meshes the mid-stroke updates to Maya come less often (at most about 4% of
  the app's time), so sculpting stays smooth; the link is now tested on Windows' named pipe as well.
- A check (tools/check_public_email.py, in CI and the test suite) keeps our email address off the website, docs and
  README.
- Site: Richard Throgmorton (MakIt3D) is credited as our first Founding Member (members page, membership page; Help >
  About in the next app build).
- Site: favicons and the press kit's app icon are the orange outline cube again, as it was before 0.11.
- Guide: SculptForge installs from FlatPark (x86_64 and ARM64) and then updates with `flatpak update`; the package
  table, the Flatpak section and the press kit's platform list say so.
- Site: our email address is no longer in any public page; the Studios page's "Start a Studio membership" and "email
  us" links build it when clicked (Discord without JavaScript), and the guide points to the SculptForge Discord.
- Studio membership is now a flat $300 a month for the whole studio, any number of artists (no per-artist charge);
  Founding Studio is a flat $150 a month for the first 10 studios. The Studios and Membership pages get a direct
  "Studio: $300 / month" checkout button.

### Fixed
- Text people type (Part, folder, layer and brush-set names, file names, Library items, pasted text) with a character
  outside the bundled fonts drew as "?": the Chinese fonts carry only the interface's own characters. Such a character
  is now taken from the system's fonts the first time it is drawn (Windows: Microsoft YaHei, JhengHei, Yu Gothic,
  Malgun Gothic; macOS: PingFang, Hiragino, Apple SD Gothic Neo; Linux: fontconfig, e.g. Noto Sans CJK), including
  characters past U+FFFF such as 𠀋. Nothing is loaded while no such text is shown, and the interface uses about 15 MB
  less memory (the fonts are no longer copied per size). Shortened names (the title bar) no longer end in a broken
  character when cut in Chinese.
- The new-version notice in a Flatpak from FlatPark shows `flatpak update` (with a Copy button) instead of telling you
  to download a new file.
- Focus Detail (experimental, `--focus-detail`): the app loop now loads full detail where the camera looks (it only
  did in UI tests); a stroke that widened the displacement range left the tiles not yet loaded at the old scale (they
  now follow, and Save writes them at the new scale).
- macOS: the app's small icon (Finder's Get Info thumbnail, list view, 16 and 32 px) was noise; the .icns now stores
  those sizes in the classic RGB + mask form Finder draws everywhere (#156, thanks @tristratos).
- The window's size and place and project.json (recent files, Discord and autosave settings) are written to a
  temporary file and then renamed, so a quit that ends mid-write (a stalled disk) keeps the last whole copy.
- Linux AppImage: after Restart to update, the previous version hid its window and never quit, so it kept running in
  the background (and, with Show SculptForge on Discord on, kept "Playing SculptForge" on your profile after you
  closed SculptForge). It now quits as soon as the new version is up. Updating from 0.11.10 or earlier, the new
  version ends the previous one itself (thanks @ebarranco).
- Show SculptForge on Discord: quitting clears it first and it never comes back during the teardown; a quit that
  stalls ends the process after 15 s; the status shows the outline cube icon instead of the old SF letters.
- **SpaceMouse**: turning the cap slowly no longer ends in a zoom that flies the view away (3DxWare's scrolling got
  through while the cap was inside the dead zone, and just after the cap was let go). Thanks @fenixpoints.
- **Stencil** edges sculpt as clean grooves instead of a beaded zipper: a built-in pattern made into a Stencil (**Use
  alpha**) is generated again at 1024 px instead of the alpha's 128, whose pixels showed as stair-steps along every
  edge it sculpted.
- **Stencil** photos on lighter meshes sculpt and paint evenly instead of as noise: where a dab's vertices are many
  image pixels apart, each reads the average of a disc as wide as their spacing (from the image or a half / quarter
  size copy, never below 512 px), not the one pixel it lands on, so a fine photo doesn't speckle or shimmer as you
  move the stencil.

## [0.11.11] - 2026-11-06

### Added
- **Brush sets** (#143, asked for by @NicTanghe): **Sculpt > Brush > Brush set > Save** keeps the bottom shelf's
  brushes, in order, and their keys as a named set in your brush library; picking a set (the Brush palette, the bottom
  shelf's right-click menu **Brush set**, or **Brush Set: Wood** in the command palette) swaps the shelf and the keys
  in one step, and the set you switched to comes back at the next start. Custom and pack brushes on the bottom shelf
  can now hold a key too (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click the tile, then press the key). Commands: `brush_set`,
  `brush_set_save`, `brush_set_delete`.
- **Creature bones** (Append > Insert on the model, #154): a built-in insert set of 13 original fantasy creature bones
  (Skull, Jaw, Fang, Blade tooth, Swept horn, Spiral horn, Claw, Vertebra, Tail vertebrae, Rib, Limb bone, Wing bone,
  Wing finger), clean closed quad meshes for dividing and sculpting, generated in-house by tools/creature-bones.
  Commands: `insert_set_items set=builtin:creature-bones`, `insert_mesh set=builtin:creature-bones index=<n>`.
  Translated piece names. Thanks @kenttumanda123-netizen.
- **Floor grid** (#115, **View > Floor grid**, also **Tool > Viewport Look** and the command palette): a faint grid on the
  floor under the model (its lowest point, Y up; it moves when a stroke or a move ends), the X and Z axes through the
  origin marked, squares stepping by ten with the zoom and fading into the distance. It never draws over the model.
  Off by default; remembered in the preferences. Suggested by @fenixpoints.
- **Maya Live** (#42, thanks @tristratos): **File > Maya > Live** sends the Parts a stroke, undo or edit changed to
  the exchange folder by itself once the scene is still for 300 ms (never mid-stroke), written in the background; the
  Maya shelf's new **Live** button does the same for Maya edits (node-dirty callbacks, debounced). Echo guard:
  manifests carry the writer's session id, a Get is never sent back, and an object that comes back unchanged is
  skipped (no undo step). Both sides pick up sends every 250 ms (was once a second); OBJs are written to a temporary
  name and renamed.
- **Maya Live link** (#42): opt-in, off at every start and never remembered; a warning every time it is turned on; an
  ember **Live link on** chip in the top bar (and the Forge status bar) turns it off. A per-user Unix socket or named
  pipe (never a TCP or UDP port), a 128-bit pairing code (**File > Maya > Copy pairing code**, pasted into the shelf's
  new **Link** button), one client at a time, mesh data only in strictly parsed frames (1 GiB cap, timeouts,
  fuzz-tested). SculptForge streams the vertices a stroke moves about ten times a second and sends whole meshes when
  faces change; Maya edits come back as whole meshes. Studios can forbid it with `"maya": {"link": "never"}`. Guide:
  Working with Maya > Live, Live link.
- **Stencil** (Sculpt > Alpha > Stencil, #155, thanks @kenttumanda123-netizen): an image over the view (loaded like an
  alpha, or the brush's alpha) that the brushes work through. It stays put on screen while the model turns under it;
  every brush that takes an alpha (built-in, custom and Community brushes, the Paint brush, micro levels) takes its
  brightness (sculpt) or its colours (Vertex Paint, or its brightness masking the paint colour) at the place each
  vertex shows, and nothing off the image. Adjust: drag to move, a corner to scale, the round handle to turn; arrows,
  - =, , . on the keyboard. Invert, Tile, Opacity, Hide while navigating. Symmetry mirrors it. Commands
  `stencil_load`, `stencil_set`, `stencil_clear`. Texture painting doesn't take it yet.

### Changed
- Install guide: a **Package managers** table with each one's status (Homebrew available; winget and Omarchy in
  review; AUR ready, not published yet; apt and dnf repositories built, live once their signing key exists). Asked for
  by @NicTanghe (#149).
- Big brushes on dense meshes keep a shorter queue of pending dabs when each dab is slow (about 150 ms of work, at
  least one dab, instead of always four): a big Clay stroke on a dense scan lags less and settles sooner after pen-up,
  with the same stroke; small brushes are unchanged.
- Site: the README links to GitHub Releases and the guide on GitHub for networks that block sculptforge.app;
  sculptforge.app/security.txt (RFC 9116) points to GitHub private vulnerability reporting.

### Fixed
- Dynamic topology: after strokes in several places (zooming in between them makes many more triangles), the view
  could draw a long spike out of the mesh and a patch of dark, broken or missing faces until the next Undo or a full
  redraw. When the renderer's buffers for the remeshed areas grew, the areas drawn before kept their vertices but lost
  their triangles, which then drew whatever the new buffer held. The mesh itself was never damaged. Thanks
  @editordenis4-collab (#153).

### Security
- New local listener, the Maya Live link: per-user socket / pipe only, opt-in with a warning each time, pairing code,
  mesh data only (SECURITY.md).

## [0.11.10] - 2026-11-06

### Added
- **Benchmark takes** (`sculptforge --benchmark-script take.sfui [project]`): a UI test script's strokes, orbits, keys
  and waits played in real time (wall-clock pacing, 240 Hz stroke samples) in the interactive loop, with the benchmark
  HUD, the benchmark video path and a per-frame CSV (frame ms, stroke lag) beside it. Steps a take can't play in real
  time (shots, expectations) are skipped; steps that run commands or read other files are refused. docs/benchmark.md.
- **Insert sets** (Parts > Append > Insert on the model): a model file with several objects (`.obj`, `.glb`) or a
  folder of `.obj` / `.ply` / `.glb` files becomes a set to insert from, one item at a time, with a thumbnail picker;
  `[` and `]` step to the previous / next item while inserting. Symmetry, undo and the up axis work as with one mesh.
  Commands `insert_mesh set=<path> index=<n>` and `insert_set_items set=<path>`. Asked by a viewer on askNK's video.
- **Sculpt the Part under the cursor** (Alt+Q in every input style, the same key as Blender's; Preferences > Hotkeys
  changes it; in the command palette): the visible Part under the pointer becomes the active one, without moving the
  camera; each Part keeps its mask. The status bar says "Sculpting: <Part>", or that no Part is under the cursor.
  Asked for by ziel9627 on Discord.
- **Units** (Edit > Preferences > Interface, #148, thanks @Swar55): sizes show in mm, cm, m or inches: the brush size,
  the active Part's width x height x depth under its name in Tool, and the size exports write in File > Import /
  Export. **OBJ/PLY in** reads the numbers in OBJ and PLY files (which have no units) as mm, cm, m (the default) or
  inches; `open`, `open_obj` and `part_append` take `unit=` for scripts, and STL files may be read as inches too.

### Changed
- **OBJ and PLY exported from an STL are now the right size: 1000x bigger than in 0.11.9.** 0.11.9 read the STL in
  millimetres but wrote OBJ and PLY in metres, so a 50 mm part came out as 0.05 (#148, thanks @Swar55). A scene opened
  from an STL, OBJ or PLY now writes OBJ and PLY in that file's own unit, an STL opened in another unit is written
  back in it, and decimal sizes come back exactly (12.7 mm stays 12.7, not 12.700001). Projects remember the unit. If
  you scaled these exports by 1000 in another app, you no longer need to.
- **USD stages in centimetres now open at their true size: 100x smaller than in 0.11.9.** 0.11.9 ignored
  `metersPerUnit`, so a stage from Maya or Houdini (`metersPerUnit = 0.01`) opened 100x too large: a 180 cm figure was
  180 m. It is now 1.8 m (#148, thanks @NicTanghe). Stages in metres, and USD files SculptForge wrote, open as before.
- Frames overlap the CPU and the GPU: the processor builds the next frame while the graphics card draws the last, at
  most one frame ahead, so the input lag doesn't grow. Reading each frame's GPU time used to wait for the whole frame
  on some drivers (Mesa on Apple silicon), which made CPU and GPU take turns. Floor tier, a 28M-triangle scan: orbit
  56 to 90 fps; a 200M-triangle scene: orbit 84 to 186 fps, big-brush strokes 51 to 191 fps.

### Fixed
- **Translations**: about 430 labels showed in English in Spanish, Traditional and Simplified Chinese, most visibly
  the whole **Generate 3D** panel in zh-CN; they now go through the translator in every language. The i18n test now
  fails on any label that skips it (only product names may), so this can't come back unnoticed.
- Micro levels: fewer spiky shards from Clay (and the other plane brushes: Clay Block, Flatten, Trim) where a stroke's
  dabs pile up over rough, folded surface such as strands meeting skin, also on the symmetric side. A big brush's dab
  that would fold the base mesh over now sculpts the detail instead, and the detail's own plane brushes no longer push
  points they can't bring to the plane. Clay over rough, leaning strands can still leave small steps; a fuller fix is
  coming.
- Vertex paint ignored the brush alpha: Drag Stamp, Slide Stamp, Dots and Spray painted a soft round blob with any
  alpha (texture painting already followed it). The paint dab now samples the alpha on the view plane, turned as
  sculpt dabs turn it, and reaches into the stamp's corners (#152, thanks @fxrtst).
- Adding a paint layer on a dense top subdivision level (4M+ triangles) could leave the model drawn at its coarse
  preview level at rest, looking like level 1, through level changes and project reloads until a restart (#151, thanks
  @fxrtst). The GPU time used to decide whether full detail fits a still frame no longer counts the CPU work of the
  frame that added the layer, and one slow frame can no longer tip it.
- The bottom shelf's **Add a brush** button draws its + exactly in the middle of the circle at every UI scale and on
  Retina screens; it was a font glyph that sat off centre (#150, thanks @tristratos).
- SpaceMouse beside a pen (Discord, @fenixpoints, the first report from a real SpaceMouse): while the cap is pushed,
  the pointer motion and wheel a SpaceMouse driver sends for programs it doesn't know (3DxWare's defaults) no longer
  move the cursor off the pen or zoom toward it; with Hold during strokes off, a Move, Snake Hook or KelvinGrab drag
  no longer pulls the grabbed part across the view when the view turns under a still pen; gizmo, selection and stamp
  drags hold the view whatever the setting. Guide: Navigation > SpaceMouse troubleshooting.

## [0.11.9] - 2026-11-06

### Added
- **Line Gradient** (Color, #99, requested by @Devon0209): a screen-space colour ramp for vertex paint and textures,
  its ends adjustable before you keep it (Enter; Esc takes it back); respects the mask, one undo step. Script:
  `paint_gradient`.
- **Part folders** (#96, thanks @ebarranco): folders in the Parts list; drag Parts in and out, fold, rename, hide and
  show, and move a whole folder with the gizmo's new **Folder** option. Saved in projects; older versions open them
  without folders.
- **Hyper3D Rodin (direct)** in Generate 3D and the `image_to_3d` / `text_to_3d` commands (`service=hyper3d`, `model=`
  a Gen-2.5 tier): Hyper3D's own API with your key (system keychain only; no environment variable, no shared or trial
  key). One pinned https host for the API, the key sent nowhere else; the GLB only from Hyper3D's domains, capped at
  200 MB and checked before import, kept in a private temporary folder deleted after import; polling backs off to 30
  s. Studios: provider id `hyper3d` in the policy, one audit log line per generation. Thanks to ElgoogUdiab for
  suggesting it (#123); re-implemented in house from Hyper3D's public API documentation.
- **Cancel** for Auto UV (Tool > Texture Image), beside its progress bar.

### Changed
- The console keeps one line for a job's progress ("Auto UV: 42%") instead of one line per percent.

### Fixed
- Dense meshes over the triangle budget lost their vertex colour (paint layers included) and mask while the view moved
  (the coarse level 0.11.7 draws then had none) and showed them again at rest. The coarse level now carries each
  cell's mean colour and mask from the full mesh, updated when a paint stroke ends.
- The input style's keys for the Size slider, the Material picker and Save Snapshot came back as the Classic defaults
  after a restart (Blender style: F no longer showed the Size slider, S and M opened the Size slider and the Material
  picker) until the style was picked again in Preferences > Input; your own rebinds were not affected (#147, thanks
  @Velisariy).
- Auto UV from the console, a script or an AI tool (`uv_auto`) froze the app while it unwrapped: about 10 s on a
  327k-triangle sculpt. It now runs on the same worker as the Auto UV button, so frames keep drawing (longest frame
  under 100 ms, floor tier included). The next statement on the line, the AI tool's reply and a UI test's next step
  wait for the UVs, so `uv_auto; texture_create` still works in one go. Macros keep running it in place.
- UV Editor > Pack froze the app on UDIM grids (about 18 s for a 10 x 2 grid on the lion head on a laptop, 11 s for 4
  x 3): the shape packer's search now runs several densities at once, coarse first, stops as soon as an island can't
  be kept whole, and keeps its quarter-resolution layout instead of packing everything again at full size. 10 x 2 on
  the lion: 18 s -> 0.04 s. Pack, Pack at Density and every Unwrap now run on a worker thread with a progress bar and
  Cancel under the view.
- Packing could shrink some islands and lay them over their neighbours: the shape packer's quarter turns mirror an
  island, which the fit back onto the island couldn't follow. Islands are now only turned to their smallest box first;
  layouts are up to 6% denser and no longer overlap.
- Pack's status line could report more than 100% of the space used (an island bigger than a tile is shrunk to fit
  one); it is now measured.

## [0.11.8] - 2026-11-06

### Changed
- The app icon is an orange cube on a dark tile (a new drawing, with a small cut for 16-32 px and the macOS grid):
  window, taskbar, dock, .ico, .icns, Flatpak, AppImage, title bar. The bust and anvil is the brand (About, What's
  New, site header, social previews); the SF letters are retired. docs/brand.md says which mark goes where.
- **Library**: a single click uses a brush, alpha, VDM or material. Double-click still works and applies it once.
  Models, projects, folders, images and splats still open with a double-click (#142, thanks @NicTanghe).
- Maya bridge (#42, thanks @tristratos): **Get automatically** is on once an exchange folder is chosen; a **Send to
  Maya** button sits in the top bar while the bridge is on; the Maya shelf gets **Auto** (on by default), which loads
  what SculptForge sends by itself. Both sides check only the other side's manifest (time and size), once a second;
  still no ports or connections. Reinstall the shelf (`install_shelf()`) to get the Auto button.
- **Corner axes** (Draw > Camera > Axes): positive ends are filled, lettered circles (X / Y / Z), negative ends
  smaller rings (their letter under the pointer); far ends are smaller and fainter and the nearest is drawn on top; a
  translucent disc behind them (stronger under the pointer) keeps them readable on any background. A second click on
  the end facing you snaps to the opposite view, a drag on the disc orbits, and the name of an exact side view shows
  under the axes. No brush ring over the axes. Small / Medium / Large and the UI scale still apply.
- The monthly Discord challenge posts say when it ends: entries close on the last day of the month and the winner
  (most 🔥 votes, ties picked by the team) is picked on the 1st, together with next month's theme.
- Micro levels: big brushes spend about a third less time preparing each frame's shading (a 240 px stroke on a 200
  million triangle head: 11 to under 8 ms a dab on the floor tier). Each changed patch is shaded on its own; its
  neighbours, whose shared border rarely changes, are brought up to date when the stroke ends.

### Fixed
- The guide's **Edit > Preferences** paths now say where Preferences is in the Classic layout (its own palette in the
  palette bar), and the Classic layout's **Edit** palette has **Preferences...** too. Thanks NicTanghe (#144).
- **Rebuild Lower Levels** (Tool > Geometry) on open meshes (a border, e.g. a UDIM-tiled panel) no longer reads the
  mesh's corners and face centres the wrong way round, which failed with "a face point joins 1 quads" or gave a wrong
  lower level; the lower level keeps every face's UVs across seams and UDIM tiles, and stepping back up gives back the
  imported UVs exactly (#145, thanks @RodTDai).
- **Drag Stamp / Slide Stamp with a pen** (#146, Huion RTP-700 on Windows): the stamp went weak or vanished when the
  pen lifted. A tablet's last samples before the lift fall towards 0, and the stamp is pressed again at every move; it
  now keeps the firmest pressure of the drag (Windows Ink, Wintab and Linux alike).

## [0.11.7] - 2026-11-06

### Added
- Coarse levels for dense meshes: a plain mesh over the hardware tier's triangle budget draws all of it at one lighter
  level while the view moves (the finest that fits the budget), and in full detail once the view has rested for 150
  ms, when one full frame fits the still-frame budget (100 ms); otherwise it stays light and the info line says
  **Preview detail**. A stroke draws the mesh in full detail; its edits reach the lighter level right after it.
  `SF_COARSE_LOD=0` turns it off.

### Fixed
- **Preferences > AI** and live mode's step confirmations were partly in English in Spanish and Chinese: Enable AI
  features, its description and privacy line, the Hugging Face token field, AI services & privacy, and the reasons a
  step asks first ("Writes ... outside the project folder", "Overwrites ...", "Connects to the network"...) are now
  translated. A new check stops labels from reaching the screen without going through the translator.

## [0.11.6] - 2026-11-06

### Added
- **Managed policy for studios** (docs/guide/studios.md "IT deployment"): `policy.json` in `/etc/sculptforge`,
  `/Library/Application Support/SculptForge` or `C:\ProgramData\SculptForge` (or `SF_POLICY` when there is none) locks
  AI on or off, network features, the allowed AI providers and addresses, sign-in for AI, AI tools (MCP on or off,
  never controlling the window, allowed kinds of command), the update check, channel and feed. Locked settings say
  **Managed by your studio**; a broken policy fails closed. `sculptforge-cli --policy` shows what it manages.
- **AI audit log**: every AI tool (MCP) command, refused ones too, and every Brush Lab and Generate 3D request is a
  JSON line (time, user, host, client, command, a summary of its settings, result) in a local file or a share the
  policy names, rotated by size. No artwork, keys or long text. `sculptforge-cli --audit-log` prints the path.
- **Studio sign-in** (Okta, Microsoft Entra ID, Google Workspace, Auth0, Keycloak): OpenID Connect device sign-in in
  the artist's browser, no local listener; tokens in the keychain, sent only to the studio's own AI gateway. **Edit >
  Preferences > AI > Studio sign-in**, or `sculptforge-cli --sign-in`.
- **AI provider presets** in **Edit > Preferences > AI > Provider** (also in Brush Lab): Anthropic, OpenAI, Ollama, LM
  Studio, vLLM, llama.cpp server, LiteLLM proxy (a studio gateway for Bedrock, Vertex AI and Azure OpenAI) and
  OpenRouter.
- Each release carries a CycloneDX software bill of materials (`SculptForge-<version>-sbom.cdx.json`), covered by the
  signed checksums. A one-page security overview for IT reviews is in the guide.

### Changed
- An empty shelf is hidden when **Customize Shelves** is off, and the canvas takes its space; it returns while
  customizing so items can be dropped back onto it. Saved layouts with empty shelves load as before (#139, thanks
  ebarranco).

### Security
- AI keys (Brush Lab, Hugging Face, fal.ai, Meshy) are stored in the operating system's keychain (macOS Keychain,
  Windows Credential Manager, Linux Secret Service) instead of `ai.json`; keys already in `ai.json` move there at the
  first start. Without a keychain (`SF_KEYCHAIN=none`), a key lasts for the session; environment variables still work.

## [0.11.5] - 2026-11-06

### Added
- While the **S** (Size) pop-up is open, the brush ring stays on the model at the point where it was opened and shows
  the size being set, live while dragging and before Enter while typing a number (#138). **O** (Falloff) draws the
  inner Falloff ring the same way. The ring lies under the pop-up so its number stays readable.
- **Simplified Chinese interface** (Preferences > Interface > Language): the Traditional Chinese translation by
  yoyomaimai (#114) in simplified characters, reviewed for mainland usage (鼠标, 软件, 文件, 视图, 菜单, 设置, 工具栏, 默认 and 预设, 撤销,
  rows and columns), every label covered. Brush and tool names stay in English. The characters come from a small
  built-in Noto Sans SC subset (SIL OFL), so nothing needs installing. zh-CN, zh-SG and zh-Hans codes select it;
  zh-TW, zh-HK and zh-MO keep Traditional.

### Changed
- **Help > Connect an AI tool** is the in-app guide to live mode: 1. pick your tool (Claude Code, Claude Desktop,
  Cursor, or any other in the list) with the exact command or settings for this install, now for the live-mode server
  (`sculptforge-cli --mcp --attach`, listed as `sculptforge-live` beside a headless `sculptforge` entry); 2. the
  live-mode switch; 3. what happens next; four example prompts with Copy. The plan notes moved to a **Plans and
  sign-in** tooltip; the headless server and the Claude Desktop bundle are in the user guide.
- The live-mode dialogs (**Let an AI tool control this window?**, **Allow this AI step?**) look like the app's other
  dialogs: one text width, two buttons of one width (the answer first), no close box (Deny or Escape), the code in a
  large font instead of a scaled-up small one. While the approval is open the Connect window steps aside, so two
  dialogs never stack; after Approve it closes and the badge takes over.
- **Show SculptForge on Discord** shows the new SF icon.

### Fixed
- **Show SculptForge on Discord** stays up to date. Changes made in quick succession (opening files, typing the
  challenge) used to be dropped by Discord after the fifth in 20 seconds. Now the latest always arrives. After Discord
  restarts, your status is back within seconds instead of a minute.
- Discord no longer shows SculptForge while it isn't running. Runs by scripts and test tools (benchmarks, offscreen
  checks) never show it, whatever the preferences say. On Linux, a process SculptForge starts (an AI worker) can no
  longer keep the connection, and the status, open after SculptForge quits.
- `--measure-idle` reported about 100 % of one core on Windows while the app sat idle: it timed itself with the C
  library's clock, which on Windows counts wall time, not processor time. It now reads the process's real CPU time on
  every OS. Measured from outside on Windows (RTX 3060), idle 0.11.1 and 0.11.2 (a sphere open) use 0.00 % of a core,
  and Linux uses 0.7 %: there was no busy loop. The idle tests now also fail if the app uses more than 2 % of a core
  over 30 s.
- The **Paint** menu jittered on a short screen (reported at 1280x720 fullscreen, thanks ebarranco): taller than the
  space under the menu bar, it scrolled, its colour picker shrank under the scrollbar, the menu stopped scrolling and
  grew again, every frame. A menu that scrolls now widens by its scrollbar instead.

## [0.11.4] - 2026-11-05

### Added
- **Live mode: an AI tool sculpts in the open window while you watch.** Turn on **Edit > Preferences > AI > Let AI
  tools control this window** (off by default) and connect Claude Code or any MCP client with
  `sculptforge-cli --mcp --attach`. You approve each connection (a 6-digit code), an **AI connected** badge has
  Disconnect, every step lands in Undo labelled **AI:**, and files outside the project, overwrites, scripts and the
  network ask you first. The AI can see its work (`viewport_image`, `view_turntable`, `view_set`), draw strokes in
  screen space (`stroke_screen`), block out forms from Forge Spheres in one call (`blockout`) and check proportions
  (`measure`, `scene_overview`). Local only: a socket / pipe just your user can open, no network port.

### Fixed
- **Shading after strokes**: a data race in the GPU vertex upload (several threads writing the same vertex's cavity
  bits) could leave rare wrong shading after a stroke. Each vertex is now written once.

## [0.11.3] - 2026-11-05

### Added
- **Help > Connect an AI tool** (while AI features are on): an "I have a subscription" picker (Claude, ChatGPT,
  Gemini, Grok, other tools) and, for this install, the exact command or configuration for Claude Desktop, Claude
  Code, Codex CLI, Antigravity CLI, Gemini CLI, Grok Build, Cursor, VS Code, Visual Studio, JetBrains AI Assistant,
  Zed, Windsurf (Devin Local), GitHub Copilot CLI, Cline, Continue, Kiro, Amazon Q Developer CLI, Warp, Goose,
  opencode, Rovo Dev CLI, LM Studio, Jan and the OpenAI Agents SDK, each checked against the vendor's documentation,
  with Copy buttons. Remote-only apps (the ChatGPT and Grok apps, Rovo on the web) are named as such: SculptForge runs
  no network server.
- A Claude Desktop bundle (`SculptForge-*.mcpb`) for Windows, macOS and Linux on every release, and `server.json` for
  the official MCP registry.
- The MCP server (`sculptforge-cli --mcp`) gains `viewport_image` (a clay-shaded picture of the scene from any angle,
  to check each step) and recipes as MCP prompts: a creature head, a stylised bust from a reference image, a
  game-ready asset (retopology, UVs, bake, LODs), rock variations and a step-by-step lesson. Its server info carries a
  title and clearer instructions.
- AppImage: `SculptForge.AppImage --cli ...` runs the command-line tool inside it (so MCP clients can start it).
- Guide (Scripting > AI agents): every client's setup, your own agent, and plain function calling with
  `sculptforge-cli --schema` for the OpenAI, Gemini and xAI APIs.
- Freeze Levels (`subdiv_freeze`, `dyntopo freeze_levels=true`): turning on Dynamic on a Part with subdivision levels
  offers to keep the level shown as one plain mesh and turn Dynamic on, one undo step, instead of a dead-end message.
  The Draw palette shows the same button while Dynamic waits on levels.
- View > Views: Front, Back, Left, Right, Top and Bottom buttons (Numpad 1 / 3 / 7, Ctrl for the opposite side, in
  every input style), and named saved views: Save View (Ctrl+Alt+Numpad 0), click a name to go back, Numpad 0 for the
  last one. Saved with the project.
- Mask darkness (Preferences > Interface and Tool > Masking).

### Changed
- The app icon is the orange SF mark (as on sculptforge.app) in the task bar, Dock, Start menu, launchers and the
  title bar; the bust stays as the illustration in Help > About, What's New and the membership card.
- Masked areas show as a cool dark tint with a soft edge instead of near-black, so the form stays readable.
- The wireframe fades edges shorter than a few pixels: a dense mesh shows a light veil, not a dark moire.
- Options that are on (toggles) have an accent-tinted fill in every colour scheme, not just a lighter grey.
- Quad Remesh guide curves draw smooth, with stroke smoothing while drawing and a dark outline for contrast.
- Tool > Geometry (from Discord): **Add Level** and **Subdivide (removes levels)** are named for what they
  do and sit apart: Micro in its own Micro Detail group, Subdivide under Change the Mesh (no levels). On a Part with
  levels, Subdivide asks first ("This removes your N subdivision levels"), with Don't ask again; Remove Below and
  Remove Above ask too (Ctrl+Z still brings the levels back).
- Tool > Parts > Extract says to mask first when there's no mask, instead of only greying out its buttons.

### Fixed
- **Quick Remesh** and **Tool > Remesh > Voxel Remesh** of an open Part (the Stylised Head starter, open at the neck,
  especially after a Move pull near the neck) gave a thin shell full of holes: the outside flowed in through the
  opening. Open borders are now capped before the voxels are made (only for the remesh; the Part's own surface is not
  touched), and a Part that still isn't a closed solid (borders that can't be capped) is left unchanged with a message
  pointing to **Tool > Geometry > Fill Holes**, instead of a broken result.
- **UV Editor** at UI scale 2.4 (a 4K screen): the **On model: Colours / Checker** row was cut off and **Frame** was
  drawn over it, so clicking Frame toggled Colours. The toolbar now measures each control (in the interface language)
  and wraps onto more lines when it needs to; every control is where it is drawn.
- **UV Editor**: the "No UVs yet: pick a method and Unwrap" hint was cut off on both sides in a narrow view or at a
  big UI scale (240 %); it now wraps inside the view, and it is translated.

## [0.11.2] - 2026-11-05

### Fixed
- Orbiting a dense scan with a micro level right after big strokes is smoother on slower computers: the shading
  that catches up after the strokes now takes a few milliseconds a frame while the view moves, and the rest when it
  stops (on scans of around 100 million triangles; smaller ones were already smooth).
- **Help > Benchmark** no longer stops at "settling" on slower computers when the scene takes a few seconds to open.
- Move, Scale and Rotate (the Transform gizmo and the `transform` command) with a sculpt layer active now record into
  that layer, masked or not, like a stroke (#133, thanks Devon0209): the base mesh stays, the layer's weight dials
  the move in and out, undo works. With Base mesh active they move the base and every layer, as before.
- **Micro levels: Sharp Crease, Crease and Pinch** at high intensity left prominent random noise. They now pull the
  height profile together the way the brushes pull a mesh, resampling the heights from where the pull brings the
  surface, so nothing grows from dab to dab; big brushes stay clean too (#125, reported by RodTDai).
- **Wire** on a micro level showed nothing; it now draws the edges of the level below the micro level (quad diagonals
  left out, as on a mesh) on the micro surface (#128, reported by RodTDai).
- With **Edit > Preferences > AI > Enable AI features** off, the Mesh menu still showed a **Generate 3D** section; it
  is now **From Image** and holds only the offline Relief, Inflate and Stamp (the AI entries were already gone). The
  status messages after opening an image point there too (reported by ebarranco on Discord).
- A pen lying still in range (laid on the tablet, or held still above it) kept sending a hair of tilt and sub-pixel
  motion, and each event counted as the pen in use: the mouse and trackpad could neither move the cursor nor click for
  as long as it lay there, and the pen pulled the cursor back to itself. With a Wacom, a SpaceMouse and a trackpad on
  a Mac the window seemed frozen and the mirror's X out of reach (reported by phoenix on Discord). Only touching,
  buttons, coming into range, pressure and real movement count now.

### Added
- **Rotation for vector displacement stamps** (#136, thanks DonnieAlex): with the VDM brush, **Sculpt > Brush > Vector
  Displacement > Stamp rotation** has Follow stroke direction (Drag Stamp turns with the drag; off, the drag only
  sizes the stamp), Angle and Angle jitter (a random turn per stamp), kept per brush like the alpha options.
- Brush files keep the alpha's **Angle** and **Angle jitter** as well as Follow stroke direction: Save Brush Settings
  on a custom brush writes all three into it, so shared brushes (brush codes, .sfpack) turn their alpha the same way.
- **Maya style view keys** (#134, thanks RodTDai): hold Alt, then add Ctrl, and a left drag rolls the view (left /
  right); Alt+F frames the active Part; Alt+H resets the view (front, upright, perspective, everything in view). Ctrl
  first (or both together) still masks with Ctrl+Alt, on the model and beside it.
- **Edit > Preferences > Input > Wheel zooms toward the pointer** (#134): the wheel zooms toward the point of the model
  under the pointer, which stays put. On with the Maya style; the other styles keep zooming toward the middle (the box
  turns it on in any style). **Reset view** is also an action in the Controls window, for a key of your own.
- **Help > Record a performance trace** (#127, thanks RodTDai): while on, `sculptforge-trace.log` beside
  `sculptforge.log` records every frame over 33 ms with where its time went (events, dabs, UI, GPU uploads, drawing,
  GPU time), each stroke's timings, commands that held up the frames, background jobs starting and ending, the mesh
  size, subdivision and micro level, and the GPU, driver and CPU. Off by default and costs nothing when off; two files
  of 2 MB at most (`sculptforge-trace-previous.log`); a local file only, nothing is sent anywhere.
- **Traditional Chinese interface** (#114, translation by yoyomaimai): Preferences > Interface > Language (the button
  with its Chinese name; saved as `zh-TW`). About 740 labels, Taiwan usage, brush and tool names kept in English;
  anything not translated yet stays in English. The Chinese glyphs come from a small embedded subset of Noto Sans CJK
  TC (SIL OFL), loaded only while the interface is in Chinese.
- The title bar's triangle count adds the micro triangles of the visible Parts while there are any ("20.5k + 5.24M
  micro"), and so does the classic look's ActiveTris readout (#125).

### Changed
- Micro levels: a big brush stroke on a dense scan finishes sooner after the pen lifts. The dabs still waiting at the
  lift go through the plain mesh's own batched path (exactly what the plain brush does), and the stroke stays one undo
  step.
- **UV Editor > Pack: Columns x Rows** (#135, thanks RodTDai): the UDIM Tiles slider is now a **Columns** (1-10) and
  **Rows** (1-10) grid, so a 10 x 2 layout fills 1001-1020 and 4 x 3 stays four tiles wide. Pack at Density spills
  within the Columns too. Projects keep their grid; new ones take the last one used. Scripts: `uv_pack` and
  `uv_unwrap` take `columns=` and `rows=` (`tiles=` works as before).
- New icon and logo.
- **Spanish and Traditional Chinese cover the whole interface**: about 950 labels and help texts added to each (UV
  Editor, Quick Remesh, Preferences > Input, Packs, Bake, Fill layers, Forge Spheres, SpaceMouse...), from 47% and 44%
  of the interface to 100%. A new test keeps it there: a label without both translations fails the build.
- **Tool > Remesh > Voxel Remesh**: the Resolution slider goes from 32 to 512 (was 64 to 400), as the `remesh
  resolution=` command does (asked by fxrtst on Discord). 512 is safe on 8 GB: a 28M-triangle scan in about 8 s (14 s
  on 4 cores) into about 760k triangles; a round 2M sculpt in about 3 s into about 2.5M triangles, 2.2 GB at the peak.
- **Big brushes keep up better on 6+ core machines**: with a brush of 160 px or more, the lighting now follows the
  brush's own shape while you drag, and the exact shading lands behind it and at pen-up, so big strokes stay closer to
  the pen. Machines with fewer cores keep the previous behaviour.

## [0.11.1] - 2026-11-05

### Added
- **Rename and organise your Lit Spheres** (#124, thanks Pumaman101): **Rename...** and **Move to folder** (existing
  folders, **New folder...**) in the right-click menu of the material picker, **Render > Materials** and the Library's
  Material tab rename or move the image in the Lit Spheres folder; a name in use is refused, nothing is overwritten,
  and projects that use a renamed one still find it (by its pixels). Sub-folders of the Lit Spheres folder (one level)
  are headings in all three lists. Pack, studio-folder and project-only Lit Spheres show the items greyed out with the
  reason. Scripts: `rename_litsphere`, `move_litsphere`.
- Dropping an image onto the Library's Material tab loads it as a Lit Sphere (#124); dropped on the canvas it does
  what it did before.
- **Founding Members**: the first 100 members keep a Founding Member badge, their name in the credits (Help > About,
  the website's Members page) and their price for good; the membership page counts the places left
  (website/founding.json).
- **One-click update** (the update banner's **Update** button) for the Linux AppImage and the Windows installer and
  portable zip: background download with progress and Cancel, then Restart now or Later (installs at quit). Files come
  only from SculptForge's GitHub releases (every redirect checked, TLS verified); the release's SHA256SUMS.txt must
  carry an Ed25519 signature under the key built into the app, and the package must match its checksum, before
  anything is written in place or run. The AppImage is replaced in one atomic rename with the old file kept as `.old`
  until the new version has started; a new version that doesn't start is rolled back. Flatpak, .deb/.rpm, AUR and
  Homebrew installs are never replaced (the banner names the command or says to download). macOS keeps the Download
  button for now. See the guide's Installing > Updating.
- **Member builds** (Preferences > Updates): GitHub sign-in with the device flow for the "SculptForge Member Builds"
  app, which can only read the member repository's releases; the token lives in the OS keychain (Windows Credential
  Manager, Secret Service on Linux; macOS later), never in settings or logs, and Sign out removes it. The update check
  then also lists the member releases; they are verified exactly like public ones.
- **Big-mesh tip**: shown once, the first time a scene holds more than about 20 million triangles (micro levels
  counted): what got faster on dense meshes and the settings that matter (Background Preview, View resolution), with
  **Performance settings** and **Got it**. Never during a stroke, a benchmark, the welcome, What's New or the quick
  tour; stored in project.json as soon as it shows, so it never comes back.
- **Pen pressure per brush** (#122, requested by Zoldyck-Kill): every brush has its own **Pen pressure: Size** and
  **Strength** switches, **Use global** / **On** / **Off**, in **Brush > Pen pressure**, Brush Lab and **Edit >
  Preferences > Tablet**. **Use global** follows Pressure > Size / Strength as before; **On** and **Off** override
  them for that brush. Kept per brush with its other settings (**Save Brush Settings** keeps them, **Reset Brush to
  Default** puts back the default) and in a custom brush's `.sfbrush`. Move, KelvinGrab, KelvinTwist, KelvinScale,
  Tendril and Elastic Tendril start with Size off; older saved settings load as Use global. Command:
  `brush_pen_pressure size=on|off|global strength=on|off|global`.
- **Quick Remesh** (Tool > Remesh, #121, thanks Zoldyck-Kill): with it on, Shift+R, or N held while clicking or
  dragging on the background, remeshes the active Part at the Voxel Remesh resolution in the background (progress bar
  and Cancel as the button; Keep Masked and Keep Texture apply). One undo step per remesh; a stroke or undo made while
  it runs is never overwritten (it remeshes again). Both keys are free in every key style and change in Preferences >
  Hotkeys; on the model a press stays a brush stroke.
- **Remesh after each stroke** (Tool > Remesh, off by default, #121): after pen-up, a stroke that stretched edges past
  1.6 times their length remeshes the active Part in the background; sculpting never waits for it. Not on Parts with
  subdivision levels, sculpt layers or a painted texture.
- **Rodin 2.5 (Hyper3D) via fal.ai** (#123, thanks ElgoogUdiab): **Generate 3D > fal.ai > Model** lists **Rodin 2.5**
  and **Rodin 2.5 Fast**. Image and text both work; the untextured GLB goes through the usual import and remesh. Uses
  your fal.ai key (`FAL_KEY`), nothing else is contacted. Commands: `image_to_3d service=fal
  model=fal-ai/hyper3d/rodin/v2.5`, `text_to_3d service=fal model=fal-ai/hyper3d/rodin/v2.5` (other fal models keep
  Hunyuan3D 3.1 Rapid for text).
- **Studio mode**: `--studio studio.json` (or a studio build) starts SculptForge with no outside connections: no
  public update check, AI services, community gallery or Discord presence, and network sockets only on this computer.
  Only the studio's own update channel or mirror is reached. `--offline` (or `SF_OFFLINE=1`) does the same with
  nothing allowed. See the user guide's "Offline deployment".
- **Touch screens and trackpads**: two fingers over the model pinch to zoom and twist to roll the view, and on a touch
  screen slide to pan; they never sculpt. One finger on a touch screen still sculpts. Thanks to phoenix (Discord) for
  the report.
- **Hold during strokes** (Preferences > Input > SpaceMouse, on by default): while you sculpt, paint or mask, the
  SpaceMouse leaves the view alone and its buttons do nothing, so a hand on the cap never moves the model under the
  pen or mouse. Pointer events from a SpaceMouse that the system also lists as a mouse are ignored. Thanks to phoenix
  (Discord).

### Changed
- **Faster builds from source**: on Linux the build links with mold when it is installed and precompiles the standard
  library and glm headers; app code the tests also run is a static library both link, so it compiles once. Packagers:
  `-DSCULPTFORGE_MOLD=OFF` keeps the system linker (the release packages do), `-DSCULPTFORGE_PCH=OFF` turns the
  precompiled headers off. See docs/build-speed.md.
- The command search and the Library filter also find Lit Spheres by "material capture" and "material image".
- Membership: **Member Requests** (one request per member per month: brushes, alphas, materials, small tweaks;
  "Requested by @name" on each item; out to members at month end, free for everyone a month later) replace the monthly
  brush and alpha pack, on the website, in the app, the guide and the README.
- The membership card: the full card shows on the first start only (and from Help > Support); after that a small card
  along the bottom of the canvas comes at a good moment (about 20 minutes of active sculpting in a session, a Save
  Render, the first start after an update, the monthly challenge's results), at most once a week, never mid-stroke or
  during a job. **Become a member**, **Not now** (a week) and **I'm already a member**, which signs in with GitHub
  like member builds: a member never sees it again (and gets member builds); no sign-in hides it for three months.
  Signed-in members never see it. The 10-hour reminder and the old "Maybe later" are gone.
- Memberships: Member ($5 / month or $48 / year) and Studio ($150 / month, up to 10 artists) replace Supporter,
  Insider, Studio and the one-time payment, on the website, in the support card, the Quick tour, the welcome note, the
  update banner ("Members get betas early") and the guide.
- Betas are a member perk: `X.Y.Z-beta.N` is published in the private members' repository and posted in
- **Release packages on staging**: the merge batch builds the release packages on the `staging` branch beside CI, and
  main reuses them. A release batch carries the version bump, so the release promotes packages that were already built
  and tested for the exact tagged commit: tag to published takes about 5 minutes instead of about 25.
- **Studio membership**: $300 a month for up to 10 artists (+$25 per extra artist; Founding Studio $150 for the first
  10 studios), now with the studio's own isolated build, a pinned version with security fixes, an offline signed
  installer and up to 3 custom items a month.
- **Testing flow** (docs/testing-flow.md): each change is tested once per tier instead of four times. T0 is
  `tools/test/impacted.py`, the tests a change can break, from the build graph. T1 is the whole suite once per merge
  batch on a GPU machine, in parallel with T2, CI on the `staging` branch; main fast-forwards only when both pass, and
  CI on main reuses that run. T3 is a nightly label and workflow for benchmarks and heavy meshes. `tools/test/run.py`
  wraps ctest with a content-hash test cache, one rerun for flaky tests (logged in tests/flaky.md), and a 20-second
  budget per test. A release bump that only adds paperwork reuses CI instead of waiting for it again.
- **Preferences save themselves** (#117, thanks fenixpoints): the key style, shortcuts, tablet, interface, performance
  and other preferences, and the radial menus, are written to ui.json half a second after they change (and at quit),
  through a temporary file. The layout is kept only by the new **Save Layout** (Edit > Preferences > Saving & Layouts). The old
  save command and its Ctrl+Shift+I key are gone; keymaps and macros that name its id (`config.store`) still load, as
  **Save Preferences Now** in the command palette.
- **SculptForge keys: I is Falloff** (as O); U stays Intensity. The Classic sculpting keys keep S size, U intensity, I
  colour intensity, O falloff (#117).
- **Plain names** for controls that carried another application's labels: Size, Intensity, Colour intensity, Falloff,
  Ctrl+hover help, Help box opacity, Customize Shelves, Reset Layout and Shortcuts, Revert to Saved Layout, Export /
  Import Interface, Lower / Higher Level, Remove Below / Above, Project Detail, Lasso Mask, Rectangle Mask, Radial
  Copies and Group from Mask. Script commands, their parameters and saved files are unchanged.

### Fixed
- Pinch and two-finger rotate did nothing on macOS trackpads and touch screens. Thanks to phoenix (Discord).
- **Big brushes on scans with micro detail**: on a micro level over a dense scan, a large Standard, Clay or Inflate
  brush now moves the base surface the way it does on a plain mesh, and the micro detail rides along untouched (one
  undo step, as before). A big Clay stroke there could freeze the app for seconds, throw spikes hundreds of units long
  and coarsen the whole level's detail; orbiting after such strokes could drop below one frame a second. Masks, hidden
  faces and alphas keep the detail brush.
- Orbiting a dense model right after sculpting no longer hitches on slower processors. The lighter copy that is drawn
  while the view moves was rebuilt in batches that a slow CPU took about 160 ms to finish, so the first frames of the
  orbit froze. Batches are now sized to fit a few milliseconds a frame. After a stroke the copy is also brought up to
  date while the view is still, so the next orbit starts with it ready (15M triangles, simulated integrated-graphics
  profile on slow cores: worst orbit frame 166 -> 24 ms).

### Security
- Release checksums (SHA256SUMS.txt) are now signed with Ed25519 (SHA256SUMS.txt.sig, a 64-byte raw signature); the
  release workflow checks the signing secret against the key compiled into the app.

## [0.11.0] - 2026-11-04

### Added
- **Fast Big Brushes (Experimental)** (Preferences > Performance, off by default): a dab that reaches more than
  150,000 vertices takes its plane from an even sample of them and turns the shading along with the move it makes
  (from the brush's own slope, no neighbour walks), and the dabs waiting for a frame go through the vertices together
  in one pass, so the normals, the picking structure and the GPU copy are updated once a frame instead of once a dab.
  Every vertex still moves by the brush itself; the exact shading follows behind the brush and when the pen lifts.
  Standard, Clay, Flatten, Crease, Pinch and Sharp Crease; Trim takes the sampled plane and shading but not the fused
  pass. Small brushes, Smooth, Layer, Inflate, Nudge, Clay Block, alphas and masked areas are placed dab by dab as
  before. The result matches the dab-by-dab stroke to within about 2% of how far it moved the surface
  (tests/test_beta_boost.cpp).
- **Brush packs**: 60 built-in brushes in six packs (Clay & Form, Hard Surface, Organic & Skin, Cloth & Folds, Damage
  & Wear, Ornament), made from SculptForge's own recipe engine and procedural alphas. A pack brush picked the first
  time brings its alpha (and turns it along the stroke where that helps). Guide chapter [Brush
  packs](docs/guide/brush-packs.md).
- Six new procedural alphas: **Cobblestone**, **Leather Grain**, **Hex Plates**, **Knit**, **Bolt** and **Chips** (21
  built-in alphas in all).
- **Sort** menu on the brush grid and list, the Alpha palette, the Material picker and the Library: A-Z (the default),
  Recent, and Pack (brushes, alphas). Saved in the preferences. The Alpha palette gets a filter field.
- **Mask by peaks**, **Mask by colour** and **Mask by facing** (`mask_by_peaks`, `mask_by_color`, `mask_by_facing`),
  and `brush_use name=...` to pick a library or pack brush from scripts and macros.
- Brush recipes (.sfbrush, brush codes) may name a built-in `alpha` and `alphaFollow`; older files are unchanged.
- **Quick tour** (Help > Quick tour, the welcome, and once when the Classic sculpting keys are picked): ten slides
  with pictures of SculptForge; **Show me** opens and flashes the panel while the tour waits in a bar at the bottom.
  The last slide is about memberships; **Maybe later** closes it for good. Guide chapter **Quick tour for experienced
  sculptors** (replaces the old comparison chapter).
- Experimental toggles in Preferences > Performance, all off by default. **Live Move**: the GPU draws a Move drag from
  the grab's start positions and the whole move is applied once at pen lift (one grab, equal to the per-frame grabs to
  1e-6). It falls back to the normal Move with masks, hidden faces, sculpt layers, micro levels, Connected Only or
  View falloff (`SF_LIVE_MOVE=0/1`). **Background Preview**: the predictive preview is drawn for background dabs
  (meshes of 2M+ vertices), and the frame no longer waits for a batch; the solver's dabs are unchanged
  (`SF_BG_GHOST=0/1`). **Fast Upload**: stroke uploads are packed into persistently mapped staging buffers
  (GL_ARB_buffer_storage) and copied on the GPU (`SF_PERSISTENT_UPLOAD=0/1`). The research behind them, including a
  design for focus detail on billion-polygon assets, is in docs/design/perf-moonshots.md.
- **Cmd+, / Ctrl+, opens Preferences** (#111, suggested by fenixpoints).
- **Show SculptForge on Discord** (Preferences > Integrations, off by default; idea by ebarranco): Discord shows
  SculptForge, its icon and the elapsed time; optionally "Monthly challenge: <theme>" (typed, 64 characters at most)
  and the file's name (never its folder). Nothing else is sent. It talks only to the Discord app on the same computer
  through Discord's local connection (no network connection, no port, no sign-in), sends its status and nothing else,
  and ignores whatever Discord sends back. See the guide's "Show SculptForge on Discord".

### Changed
- **Tool > Masking** is one panel of buttons with pictures and a hint each: Shapes, Mask by and Adjust, with their
  options under **Mask by options**.
- Brushes, alphas, materials and Library files list A-Z by default (alphas and materials were in a fixed order;
  Library projects were newest first, which is still one click away under **Sort: Recent**).
- The command search (F3) finds features by plain sculpting words: remesh, voxel, retopo, quads, multires, groups,
  clip, trim, slice, folders, insert, kitbash, vertex color ("Quad Remesh (also: retopo)").
- Scripts, commands and file names use SculptForge's own names throughout (`part_*`, `forgesphere_*`, `bridge_*`,
  `facegroup_*`, `load_litsphere`, `library_open`, `layout=classic`, `input_style classic`, the `--facegroups` flag,
  the `SCULPTFORGE_LITSPHERE_PATH` variable). Scripts, macros, keymaps, radial menus, packs, projects and preferences
  from older versions still load: their old names are read once and mapped to the new ones. The old command-line flag
  and environment variable names are no longer read.
- **Micro levels on slower computers**: while a stroke runs, the shading of what it changed takes at most about 10 ms
  a frame (measured on the computer), so big strokes keep up with the pen; the rest of the shading follows right
  after, as before.

### Removed
- The library export plug-in for another sculpting app and opening its library files through it. A folder of numbered
  Bridge files still opens as one scene.

### Fixed
- **Pattern brushes on micro levels** (#120, reported by RodTDai): a brush recipe on a micro level was a plain
  Standard dab, without its surface pattern (Dragon Scales, Skin Pores, Cracks...) and building up even when the
  recipe carves. It now keeps the pattern and its depth's sign there; planes, pinch and drag still don't apply to
  micro levels.
- **Windows portable zip starts on a fresh PC** (#109, reported by git-tiger): the Visual C++ runtime (msvcp140,
  vcruntime140, vcruntime140_1) now ships next to sculptforge.exe in the zip and the installer; before, a PC without
  the Visual C++ Redistributable couldn't start it. When SculptForge can't start (no OpenGL 3.3, an old graphics
  driver) it now says why in a message box, with the log's path, instead of quietly exiting.
- **Move with Pressure > Size on a pen** (#122, reported by Zoldyck-Kill): Move and the Kelvinlet grab brushes set the
  area they grab at the press, when a pen barely touches, so with Pressure > Size on they grabbed only a few percent
  of the brush and seemed not to work. They now always grab with the full brush size; Pressure > Strength still
  applies.

[Unreleased]: https://github.com/islandmed/SculptForge/compare/v0.12.4...HEAD
