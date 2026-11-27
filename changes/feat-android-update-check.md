---
whats_new: **Android update check**: SculptForge for Android checks once a day whether a newer version is out and shows the usual banner with a link to the download page. **Edit > Preferences > Updates** (and the welcome window) turns it off.
added: **Update check on Android**: once a day at most, the sideloaded APK fetches latest.json from sculptforge.app (then its
  GitHub copy), the same file as the desktop app: no query, no ids, nothing about you or the device. A newer version shows the
  update banner with a link to the download page (the APK is not installed from the app). Off with **Edit > Preferences >
  Updates** or the welcome window's switch. Needs Android's `INTERNET` permission, the only one. Store builds can leave it out
  (`SCULPTFORGE_ANDROID_UPDATE_CHECK=OFF`).
testers:
  - Android tablet, fresh install: the welcome window shows "Check for updates" with the one-line explanation; leave it on, restart; with a newer version in latest.json the banner appears with Download opening the download page.
  - Same device: Edit > Preferences > Updates > Check for updates off, restart (and tap Help > Check for updates): no banner on start, and the manual check still answers.
  - Airplane mode: start the app, it opens normally; Help > Check for updates says it couldn't check.
---
Network surface for cto: SculptForgeActivity.updateFeedGet (Java, https GET of two hosts, no redirects, 1 MB cap, fixed
user agent) and src/app/AndroidHttp.cpp (JNI call, address allowlist). AndroidManifest adds INTERNET.
