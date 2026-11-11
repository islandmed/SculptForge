# SculptForge for Android (early)

The same app as on the desktop, built for Android tablets and phones (arm64, Android 9 or later, OpenGL ES 3.2).
Milestone 1: it installs, opens a starter mesh, sculpts with a finger or a stylus (pressure), navigates with two
fingers, undoes, and saves and opens projects. A tablet layout of its own comes next.

## Build

```sh
scripts/android-build.sh                 # build-android/SculptForge-X.Y.Z-android-unsigned.apk (arm64-v8a)
scripts/android-build.sh --debug         # a debug-signed APK that installs as it is (adb install)
scripts/android-build.sh --abi x86_64    # for the emulator
```

It needs the Android SDK (`ANDROID_HOME`, platform 35 and build-tools), an NDK, Java 17, CMake, Ninja and Gradle;
GitHub's Ubuntu runners have them all, and `.github/workflows/android.yml` builds the APK on every push that changes
the Android side. What it does: CMake with the NDK's toolchain builds the app as `libmain.so` (with `SDL3` beside it),
then the Gradle project here wraps it with SDL's Java activity (copied from the SDL release the build fetched, so the
two always match).

## How the desktop code runs here

- **Drawing:** OpenGL ES 3.2 (`SCULPTFORGE_GLES`). The shaders stay written once as GLSL 3.30 and `src/render/GL.cpp`
  compiles them as GLSL ES 3.20; the four desktop-only calls have stand-ins there. Wireframe draws filled for now.
  `cmake -DSCULPTFORGE_GLES=ON` builds the same path on a Linux desktop (Mesa), to test it without a device.
- **Files:** settings, the log, autosaves and the brush library live in the app's own storage. **Save** writes to
  the app's `Projects` folder (on the device: `Android/data/org.sculptforge.app/files/Projects`, reachable over USB);
  **Open** shows the system's document picker and opens a copy of the chosen file. No storage permission.
- **Left out:** the plugin runtime, the live face feed, the update check and the tools that run other programs
  (splat training, photo scans, video): starting one says it can't run on Android.
- **Permissions:** none.

## Signing

CI builds unsigned APKs. A release APK is signed on the release machine with our own key
(`~/.config/sculptforge/android-release.jks`, never committed): `scripts/android-sign.sh APK`.
