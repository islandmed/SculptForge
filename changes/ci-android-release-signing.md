---
whats_new: **Android**: release builds are signed with a stable key, so a new version installs over the previous one and keeps your projects.
added: Android release builds are signed with a stable key: from this version an update installs in place. Builds from before need one more uninstall first (save or export projects before). The certificate fingerprint is in the guide's Installing page.
testers:
  - Android: install the previous release build, then this one over it; the app opens with your projects still there.
  - Any computer with the Android SDK: `apksigner verify --print-certs` on the APK shows the fingerprint from the guide.
---
Reviewer: android.yml job android-release-sign (environment android-release, tags v* only), scripts/android-verify.sh + android-cert.sha256, release.sh release_apk, check_workflows.py rules.
