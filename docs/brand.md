# Brand: which mark goes where

Tony 11-06. Two marks, one job each. The files are built by `tools/brand/make_cube_mark.py` (cube SVGs) and
`tools/brand/make_icons.py` (every PNG, .ico, .icns and upload file). Don't edit the outputs by hand.

## Cube: the mark (small or functional)

An orange cube seen corner on, on a dark rounded tile. It is our own drawing (a regular hexagon and its centre), not
taken from an icon set. It reads at 16 px, where the bust and the SF letters don't (sheet:
`~/Work/sf-launch/brand/compare-cube-sf-bust.png`).

| Where | File |
|---|---|
| App icon, all OS sizes (window, taskbar, dock, Start menu, .desktop, Flatpak, AppImage) | `assets/branding/sculptforge-16..1024.png`, `.ico`, `.icns`, the Flatpak PNG |
| App title bar and status bar (`brand::appMark`) | `sculptforge-256.png`, `sculptforge-32.png` (small cut) |
| Site favicons, apple-touch-icon | `website/favicon.ico`, `website/img/favicon-*.png`, `apple-touch-icon.png` |
| Site header lockup: cube + "SculptForge" wordmark | inline SVG `class="brand-mark"` (pages and `tools/guide/build_site.py`) |
| README logo, press kit icon | `sculptforge-256.png`, `website/press/sculptforge-icon-*.png` |
| Discord Rich Presence image | `website/press/sculptforge-icon-1024.png` (by URL) |
| Discord bot avatar, GitHub org avatar (uploaded by hand) | `make_icons.py --discord DIR`: `discord-bot-avatar-512.png`, `github-avatar-500.png` |

- **Cuts:** `cube-mark.svg` from 48 px; `cube-mark-small.svg` for 16-32 px (bigger cube, thicker seams, no rim);
  `cube-mark-macos*.svg` on Apple's icon grid; `cube-mark-square.svg` full bleed for circle or rounded masks.
- **Minimum size:** 16 px (small cut). In the site header 28 px or more.
- **Clear space:** a quarter of the tile's width on every side. In the lockup the wordmark sits 10 px from a 34 px
  tile (about 0.3 tile), vertically centred, in the site's sans at weight 600.
- **Don't:** recolour the faces, outline the tile, put the cube on an orange background, or set text inside the tile.

## Bust + anvil: the illustration (personality)

The clay bust melting into an anvil. Too detailed for small sizes; used large where it can be seen.

| Where | File |
|---|---|
| About / splash, What's New, support card in the app (`brand::bust`) | `assets/branding/bust/bust-256.png` |
| Site What's New cards | `website/img/bust-256.png`, `bust-512.png` |
| Social previews (site, GitHub) | `website/img/social-preview.png`, `docs/images/social-preview.png` |
| Press kit | `website/press/sculptforge-bust-1024.png` |
| Discord server icon (uploaded by hand) | `discord-server-icon-512.png` |

Never below 96 px, never as the app icon.

## SF letters: retired

The two-tone "SF" tile (0.11.3 to 0.11.7). Sources stay in `tools/brand/make_sf_mark.py` and `sf-mark*.svg` for history;
no shipped file uses them. "SF" is fine as a nickname in text only.

## Colours

| | Hex |
|---|---|
| Brand orange (site accent, cube left face) | `#f28c3c` |
| Cube top face | `#ffb877` |
| Cube right face | `#b8561a` |
| Cube seams | `#141416` |
| Tile (gradient, top to bottom; flat `#1c1b20` inline on the site) | `#26252a` to `#121114` |
| Tile rim (48 px and up) | white at 14 % |

## Rebuilding

    python -I tools/brand/make_cube_mark.py tools/brand
    python -I tools/brand/make_icons.py <bust concept.jpeg> --discord ~/Work/.sf/brand

Then paste `cube-mark.svg`'s shapes into the header SVGs if the mark changed, and run `tools/check_marks.py` and
`tools/check_marks_media.py`. Trademark review of the cube before 1.0.
