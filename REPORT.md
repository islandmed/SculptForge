# fix/phone-ar-reexport: ui_phone_link was failing locally only. The app is fine; the test is fixed.
Cause: on this machine the Capture window gets an extra "Network" row because there are two addresses (wlan0 + tailscale0). The taller window covered the strip of sphere the stroke aimed at, so no stroke landed and no re-export ran. CI has one address, which is why it was green at 60001d4. Nothing between 60001d4 and main is involved (src there only touches the support card), so there was nothing to bisect.
Fix (tests/ui/phone_link.sfui): drag the window aside by its title bar, then stroke across the sphere. Passes at 1600x960, 1920x1080 and 1280x800.
Tests: full ctest -j3 116/116 (main-equivalent build) + the fixed ui_phone_link. SHA: see branch head (fix 40895b2).
