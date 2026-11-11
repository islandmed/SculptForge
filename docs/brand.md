# Brand: which mark goes where

Owner 11-06: "brand I like the bust but icon in the app I like the cube". Two marks, one job each. Files are built by
`tools/brand/make_cube_mark.py` (cube SVGs) and `tools/brand/make_icons.py` (every PNG, .ico, .icns and upload file);
don't edit the outputs by hand.

## Bust + anvil: the brand

The clay bust melting into an anvil, on its dark rounded tile. It is who we are: wherever the brand is shown.

| Where | File |
|---|---|
| Site header/footer brand: the wordmark (cube + SCULPT FORGE; 32 px, 24 px in the footer; cube only on very narrow bars) | `website/img/logo-wordmark-dark.svg`, `logo-cube.svg` (`class="brand-wordmark"`/`brand-cube`, pages and `tools/guide/build_site.py`); light/press copies in `website/press/` |
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

The outline cube: Lucide's "box" icon (ISC, credited in `THIRD_PARTY_NOTICES.md`) in brand orange on a dark rounded
tile, as it shipped in 0.1-0.10 (088f0cd6, site 71cd4f7) and as the Discord developer portal shows it. Owner 11-06 21:1x:
"I want that back as the icon as it was before". The originals are frozen in `tools/brand/cube-outline/` and copied
pixel for pixel; only sizes that never shipped (24 px, the macOS `.icns` on Apple's grid, apple-touch-icon) are rendered
from its SVG. Never redraw or retouch it. The `.icns` keeps 16 and 32 px as RGB + mask (`is32`/`il32`), never PNG:
Finder draws PNG there as noise (#156); `tools/brand/icns.py` checks it (test `icns`). Comparison sheets: `~/Work/.sf/brand/shots/cube-outline-*.png`.

| Where | File |
|---|---|
| OS icons, all sizes (window, taskbar, dock, Start menu, .desktop, Flatpak, AppImage) | `assets/branding/sculptforge-16..1024.png`, `.ico`, `.icns`, the Flatpak PNG |
| App title bar and status bar (`brand::appMark`) | `sculptforge-256.png`, `sculptforge-32.png` |
| Site favicons, apple-touch-icon | `website/favicon.ico`, `website/img/favicon-*.png`, `apple-touch-icon.png` |
| Press kit app icon | `website/press/sculptforge-icon-1024.png`, `-256.png` |
| Discord Rich Presence image (by URL: frozen, never overwritten) | `website/press/sculptforge-app-icon-cube-outline-1024.png` |
| Discord server icon, developer portal app icon (uploaded by hand) | `make_icons.py --discord DIR`: `discord-server-icon-cube-outline-512.png`, `discord-portal-icon-cube-outline-1024.png` |

- **Minimum size:** 16 px (the original's own 16 px frame; softer than the filled cube's).
- **Don't:** recolour it, change the stroke, put it on an orange background, or set text in the tile.

**Alternative, not shipped:** the filled cube, our own drawing (three shaded faces from a regular hexagon):
`tools/brand/make_cube_mark.py` -> `cube-mark*.svg`; `make_icons.py ... --style filled` builds every icon from it
(small cut for 16-32 px, rim from 48 px, Apple grid).

## SF letters: retired

The two-tone "SF" tile (0.11.3 to 0.11.7). Sources stay in `tools/brand/make_sf_mark.py` and `sf-mark*.svg` for
history; no shipped file uses them. "SF" is fine as a nickname in text only.

## Colours

| | Hex |
|---|---|
| Brand orange (site accent, app icon cube) | `#f28c3c` |
| Backdrop behind the bust (avatars) | `#1c1a1c` |
| App icon: cube stroke, tile (gradient, top to bottom) | `#f28c3c`, `#23232a` to `#0e0e11` |
| Filled cube (alternative): top / right face, seams; tile | `#ffb877` / `#b8561a`, `#141416`; `#26252a` to `#121114` |

## Rebuilding

    python -I tools/brand/make_cube_mark.py tools/brand
    python -I tools/brand/make_icons.py ~/Work/.sf/brand/icon-concept-anvil-bust-A.jpeg --discord ~/Work/.sf/brand

(`--style filled` for the alternative.) Then `tools/check_marks.py` and `tools/check_marks_media.py`. Trademark review
of the bust before the SculptForge 2027 launch; the cube is a stock ISC icon, so it can't be registered as our mark.

## Referral links

Creators and partners get a tagged link, `https://sculptforge.app/?utm_source=<name>&utm_medium=referral&utm_campaign=<slug>` (slug: lowercase letters, digits, hyphens); the private stats page shows their visits, downloads, memberships and monthly revenue as their own row (no cookies or pixels), and existing ad tags (`fb1107-*`) keep working.
