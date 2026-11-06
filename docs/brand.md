# Brand: which mark goes where

Tony 11-06: "brand I like the bust but icon in the app I like the cube". Two marks, one job each. Files are built by
`tools/brand/make_cube_mark.py` (cube SVGs) and `tools/brand/make_icons.py` (every PNG, .ico, .icns and upload file);
don't edit the outputs by hand.

## Bust + anvil: the brand

The clay bust melting into an anvil, on its dark rounded tile. It is who we are: wherever the brand is shown.

| Where | File |
|---|---|
| Site header lockup: bust + "SculptForge" (44 px, 1x-3x; 32 px in the footer) | `website/img/bust-mark-44/88/132.png` (`class="brand-mark"`, pages and `tools/guide/build_site.py`) |
| README hero | `website/img/bust-256.png` |
| Social previews (site, GitHub) | `website/img/social-preview.png`, `docs/images/social-preview.png` |
| GitHub org avatar, Discord bot avatar and server icon (uploaded by hand) | `make_icons.py --discord DIR`: `github-avatar-500.png`, `discord-bot-avatar-512.png`, `discord-server-icon-512.png` |
| Splash/About, What's New, support card in the app (`brand::bust`) | `assets/branding/bust/bust-256.png` |
| Site What's New cards, press kit | `website/img/bust-256.png`, `bust-512.png`, `website/press/sculptforge-bust-1024.png` |

- **Minimum size:** 32 px (site footer); 44 px or more wherever it carries the brand alone. Never as an OS icon or favicon.
- **Clear space:** a quarter of the tile's width on every side. In the lockup the name sits 10 px from the tile,
  vertically centred, in the site's sans at weight 600.
- **Don't:** crop the tile, recolour it, or put text on it.

## Cube: the app icon

An orange cube seen corner on, on a dark rounded tile: our own drawing (a regular hexagon and its centre), not the
Lucide "box" icon we used before. It is functional: it has to read at 16 px, where the bust doesn't (sheet:
`~/Work/sf-launch/brand/compare-cube-sf-bust.png`).

| Where | File |
|---|---|
| OS icons, all sizes (window, taskbar, dock, Start menu, .desktop, Flatpak, AppImage) | `assets/branding/sculptforge-16..1024.png`, `.ico`, `.icns`, the Flatpak PNG |
| App title bar and status bar (`brand::appMark`) | `sculptforge-256.png`, `sculptforge-32.png` (small cut) |
| Site favicons, apple-touch-icon | `website/favicon.ico`, `website/img/favicon-*.png`, `apple-touch-icon.png` |
| Discord Rich Presence image, press kit app icon | `website/press/sculptforge-icon-1024.png` (by URL), `-256.png` |

- **Cuts:** `cube-mark.svg` from 48 px; `cube-mark-small.svg` for 16-32 px (bigger cube, thicker seams, no rim);
  `cube-mark-macos*.svg` on Apple's icon grid; `cube-mark-square.svg` full bleed (apple-touch-icon).
- **Minimum size:** 16 px (small cut). Not used as the brand next to the name.
- **Don't:** recolour the faces, outline the tile, put the cube on an orange background, or set text in the tile.

## SF letters: retired

The two-tone "SF" tile (0.11.3 to 0.11.7). Sources stay in `tools/brand/make_sf_mark.py` and `sf-mark*.svg` for
history; no shipped file uses them. "SF" is fine as a nickname in text only.

## Colours

| | Hex |
|---|---|
| Brand orange (site accent, cube left face) | `#f28c3c` |
| Backdrop behind the bust (avatars) | `#1c1a1c` |
| Cube top / right face, seams | `#ffb877` / `#b8561a`, `#141416` |
| Cube tile (gradient, top to bottom), rim from 48 px | `#26252a` to `#121114`, white at 14 % |

## Rebuilding

    python -I tools/brand/make_cube_mark.py tools/brand
    python -I tools/brand/make_icons.py ~/Work/.sf/brand/icon-concept-anvil-bust-A.jpeg --discord ~/Work/.sf/brand

Then `tools/check_marks.py` and `tools/check_marks_media.py`. Trademark review of both marks before 1.0.
