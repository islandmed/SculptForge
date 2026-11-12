---
whats_new: **A friendlier User Guide**: every chapter opens with a picture and an "In short" box, menu paths show as
  chips, and the Welcome page is a grid of places to start. **Help > User Guide** (Shift+F1).
changed: The User Guide (in the app and on the website) reads like the site: each chapter opens with a hero picture and
  an "In short" box, long paragraphs are split into short ones, menu paths such as Edit > Preferences > Input show as
  chips, and Welcome starts with a card grid (Your first sculpt, Quick tour, What's new, Install, Get help). The
  membership details moved to a new Support and membership chapter. The guide's colours follow the current look,
  so light desktop themes read too.
testers:
  - Any OS: Help > User Guide. Welcome shows a picture, an In short box and five cards; click each card, it opens its chapter.
  - Support and membership: click "When the small card comes"; it opens and closes.
  - Edit > Preferences > Interface > Look with "Match my desktop theme" on a light Omarchy theme: the guide text is dark on light.
---
Guide renderer: core/Guide (cards, details, callout bullets, menu paths, hero) + app/UserGuide.cpp + tools/guide/build_site.py.
