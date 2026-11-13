---
---
scripts/release.sh: a beta no longer stops at the desktop install-asset check (betas live on the private Insiders repository and carry the base version's file names), so release_apk attaches the members' APKs and SHA256SUMS-android.txt without hand steps. Test: tests/release_apk_test.sh case 8 (fails on the old release.sh with the 0.12.13-beta.1 error).
