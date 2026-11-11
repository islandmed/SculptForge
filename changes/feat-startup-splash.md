---
whats_new: **Startup splash**: while SculptForge takes a moment to start, the bust, wordmark and version show on the dark background, and the main window appears once it is ready (no empty window first). A quick start shows nothing. **Preferences > Interface > Show startup splash** turns it off.
added: **Startup splash**: when start-up takes longer than about 0.3 s, a small window with the SculptForge bust, the wordmark and the version shows until the main window has drawn its first frame; the main window starts hidden instead of empty. Never in headless runs, tests or benchmarks (SDL_VIDEO_DRIVER=offscreen). **Preferences > Interface > Show startup splash** (on by default). Splash design idea by @lowenski (#176).
testers:
  - Windows, macOS, Linux (HiDPI too): start SculptForge on a slow disk or with a big project to open; the logo and version show centred on a dark window, then the main window appears with no empty-window flash. Uncheck Preferences > Interface > Show startup splash, restart: the window appears as before.
---
Built in-house (outside PR #176 not merged or run); the art is the brand bust (website/press/sculptforge-bust-1024.png at 576 px, 256 colours, 86 KB) and website/img/logo-wordmark-dark.svg (1280 px, 35 KB) as assets/branding/splash-*.png; nothing from #176's images. `sculptforge --splash-shot out.png --size 1440x810` writes the splash as a picture.
