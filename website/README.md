# website

The source of [sculptforge.app](https://sculptforge.app): plain HTML, one `style.css` and a little JavaScript. No
framework, no build step for the hand-written pages, no cookies. The whole folder is
deployed as it is by `.github/workflows/pages.yml` (GitHub Pages), and mirrored to a second address for networks that
block new domains: the repository [islandmed.github.io](https://github.com/islandmed/islandmed.github.io) is a copy of
this folder, replaced on every sync, so change things here and never there. Because the folder is published whole,
**this file is published too**: keep it free of anything that should not be public.

## What is here

| Path | What |
|---|---|
| `index.html`, `download.html`, `support.html`, `studios.html`, `why.html`, `privacy.html`, `terms.html`, ... | The pages, written by hand. The header and footer markup is the same everywhere; `tools/guide/build_site.py` holds the canonical copy |
| `style.css`, `motion.js`, `fonts/`, `img/`, `media/`, `press/`, `favicon.ico` | Look, behaviour and artwork. Fonts are served from here, not from a font service |
| `guide/` | **Generated** from [docs/guide](../docs/guide) by `tools/guide/build_site.py`: do not edit the HTML, edit the Markdown |
| `brushes.html`, `gallery/` | Generated from the built-in brush packs (`tools/brush-packs/sync_website.py`) and the community gallery (`tools/brush-gallery/sync_website.py`) |
| `benchmarks/` | The benchmark results page; entries are validated and added by `tools/benchmark/submit.py` ([docs/benchmark.md](../docs/benchmark.md)) |
| `latest.json` | The update feed the app reads once a day: newest version, links, highlights. Written by the release; nothing about the user is sent |
| `jobs.html`, `jobs.json`, `stats/` | The jobs page and the download statistics, fed by scheduled workflows |
| `link/` | The phone's face and deck pages, which the desktop pairs with ([docs/phone-link.md](../docs/phone-link.md)). `link/index.html` is also compiled into the app, so a change to it is an app change, not a site-only one |
| `founding.json`, `featured.json`, `community-videos.json` and other `*.json` | Data the pages and the app read: credits, featured work (the app's Featured panel reads `featured.json`), videos. `founding.json` is compiled into the app |
| `llms.txt`, `robots.txt`, `sitemap.xml`, `security.txt` | For crawlers, assistants and security reporters |

## Work on it

Open the files, or serve the folder and browse it:

```sh
python3 -m http.server -d website 8000      # then http://localhost:8000
python3 tools/guide/build_site.py --check  # rebuild the guide pages and check every link, anchor and image
```

Look at the page itself, at phone width and on a wide screen, before you send a pull request.

## Rules

- **Own names only.** No other company's marks, no comparisons with other products; `tools/check_marks.py` and
  `tools/check_marks_media.py` check text and images, and a new image needs `marks_media_cache.json` committed.
- **Privacy first.** The one analytics script is the cookie-free GoatCounter, named in the footer; add no other
  third-party script, font service or tracker. Embedded video is click-to-play: the player is created on click.
- **Every public image must look good** at 1x and 2x.
- **Facts, not claims.** Numbers on the site are measured ones from [docs/performance.md](../docs/performance.md); say
  nothing about money, user counts or other people without their approval.
- A change that only touches `website/` and the front-page files deploys to the live site as soon as it lands on
  `main`; files compiled into the app (the two noted above) go through the normal release instead.
