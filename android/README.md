# SculptForge for Android (early)

The same app as on the desktop, built for Android tablets and phones (arm64, Android 9 or later, OpenGL ES 3.2).
Milestone 1: it installs, opens a starter mesh, sculpts with a finger or a stylus (pressure), navigates with two
fingers, undoes, and saves and opens projects. A tablet layout of its own comes next.

## Build

```sh
scripts/android-build.sh                 # build-android/SculptForge-X.Y.Z-android-arm64-v8a-test.apk
scripts/android-build.sh --debug         # with debug info in the native code (ndk-stack)
scripts/android-build.sh --unsigned      # for scripts/android-sign.sh
scripts/android-build.sh --abi x86_64    # Intel/AMD Chromebooks and the emulator
```

It needs the Android SDK (`ANDROID_HOME`, platform 35 and build-tools), an NDK, Java 17, CMake, Ninja and Gradle;
GitHub's Ubuntu runners have them all, and `.github/workflows/android.yml` builds the APK on every push that changes
the Android side. What it does: CMake with the NDK's toolchain builds the app as `libmain.so` (with `SDL3` beside it),
then the Gradle project here wraps it with SDL's Java activity (copied from the SDL release the build fetched, so the
two always match).

## Devices we test on

The big tablet makers, their pens and their graphics, so every family is covered by at least one tester:

| Maker (families) | Pen | Graphics | Notes |
|---|---|---|---|
| Samsung (Galaxy Tab S, Tab S FE, Tab A) | S Pen (EMR: hover, side button, no eraser end) | Adreno (Snapdragon), Xclipse/Mali (Exynos) | DeX desktop mode: windows, keyboard and mouse, density changes |
| Lenovo (Tab P, Tab M, Yoga Tab, Legion Tab) | Precision Pen (AES / USI) | Mali/PowerVR (MediaTek), Adreno | budget Tab M models are the floor tier |
| Xiaomi / Redmi (Pad) | Focus / Smart Pen | Adreno, Mali (MediaTek) | HyperOS kills background apps fast: the background save matters |
| OnePlus / OPPO / Realme (Pad) | Stylo | Mali/Immortalis (MediaTek), Adreno | |
| Google (Pixel Tablet) | USI 2.0 pens | Mali (Tensor) | stock Android: the reference |
| Amazon (Fire HD, Fire Max) | none (fingers) | Mali | Fire OS: no Google Play, sideload works; the lowest memory |
| Honor / Huawei (Pad, MatePad on Android/EMUI) | Magic-Pencil / M-Pencil | Mali, Maleoon | HarmonyOS NEXT can't run APKs |
| Chromebooks (Intel/AMD and ARM) | USI pens | Intel/AMD (x86_64 APK), Mali/Adreno | free-form windows, keyboard and trackpad |

Every pen arrives as Android's stylus input (pressure, tilt where the pen has it, hover where it has it, the side
button); a pen without an eraser end (S Pen) erases with its side button once the tablet layout maps it. All of them
need OpenGL ES 3.2, which every tablet of these families since about 2018 has.

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

Tester APKs (CI's artifact) carry the build machine's throwaway debug key: they install as they are, but one from
another build machine needs the old one uninstalled first (projects in its Projects folder go with it: copy them
off). A release APK is signed on the release machine with our own key
(`~/.config/sculptforge/android-release.jks`, never committed): `scripts/android-sign.sh APK`.
