---
fixed: The Android APK build no longer fails when Maven Central refuses the CI runner's dependency downloads (the 0.14.1 and 0.14.2 members' releases came out without APKs until the job was rerun): the Gradle dependencies are cached, resolution is retried, and a release that still ends up without its APKs now stops the release script with an error.
---
Infra only (android.yml, android-build.sh, release.sh); no user-facing change.
