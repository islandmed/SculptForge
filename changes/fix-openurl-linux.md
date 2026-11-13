---
fixed: Support, the website and other links opened from SculptForge on Linux (the AppImage on Omarchy) no longer start a pile of crashing Chromium windows. The browser now starts in the environment SculptForge was launched with, without the AppImage's own library paths, once per click (the same link twice within 2 seconds opens once), and also for the folders the app opens in your file manager.
testers:
  - Linux AppImage (Omarchy) with Chromium as the default browser: Help > About > Support SculptForge opens one window with the page, no extra windows.
  - Click it twice fast: still one page.
---
Reviewers: src/app/OpenUrl.cpp (clean environment, detached xdg-open > gio open > $BROWSER, debounce), NativeDialog.cpp openUrl, tests/test_open_url.cpp (shim xdg-open, no real browser).
