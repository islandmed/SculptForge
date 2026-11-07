---
fixed: macOS: the app's small icon (Finder's Get Info thumbnail, list view, 16 and 32 px) was noise; the .icns now
  stores those sizes in the classic RGB + mask form Finder draws everywhere (#156, thanks @tristratos).
testers:
  - macOS: Finder > Applications > SculptForge > Get Info; the small icon at the top left is the orange cube, as is
    the list view's icon.
---
tools/brand/icns.py writes and checks the .icns (no Pillow); tests icns and icns_self_test. make_icons.py --icns-only
rebuilds just the .icns. The large slots' pixels are unchanged.
