---
whats_new: **Featured creator**: the Welcome window (Help > Welcome) shows one video by an artist who made something about SculptForge, with its picture, title and a **Watch** button that opens it in your browser. The small x hides that video. It comes with the daily update check, so it is off with **Preferences > Updates > Check for updates** and when you are offline.
highlight: Featured creator card in the Welcome window
added: **Featured creator** card in the Welcome window: a picture, the video's title and creator, and Watch (opens the browser; no embedded player). The list is `featured.json` beside `latest.json` on the same host, read with the daily update check and kept with its thumbnails in the preferences folder; a different entry each start, the x hides one for good. Watch links go only to https youtube.com, youtu.be and vimeo.com, thumbnails only from sculptforge.app; nothing is shown when the list is empty, the site can't be reached or update checks are off. First featured: askNK.
testers:
  - Any OS: Help > Welcome shows the Featured creator card after the first update check (Help > Check for updates); Watch opens the video in your browser; the x hides it.
  - Preferences > Updates > Check for updates off, restart: no card. Offline: no card.
---
Site lane: website/featured.json and website/featured/ are edited by the coordinator; the card needs no app release for new entries.
