---
fixed: The Android APK build no longer fails when Maven Central refuses the CI runner's dependency downloads (the 0.14.1 and 0.14.2 members' releases came out without APKs until the job was rerun): the Gradle dependencies are cached, resolution is retried, and a release that still ends up without its APKs now stops the release script with an error.
changed: The Android APKs (arm64-v8a for phones and tablets, x86_64 for emulators) are attached to each public release too, with SHA256SUMS-android.txt; members still get them first on their own release. The release script counts the files on both releases and stops with an error if one is missing.
---
Infra (android.yml, android-build.sh, release.sh); the public upload is the owner's OK of 11-27.
