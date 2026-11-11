---
whats_new: **A new What's New window**: each release opens with a hero band (its theme over the bust in forge light) and
  3 to 5 feature cards, each with a drawn picture, a short title and one line. The cards on the website and in the guide
  use the same pictures.
changed: **What's New** (Help > What's New and after an update): a hero band with the release's theme, then its feature
  cards in one row with drawn pictures (no screenshots, no text in the pictures, so every language stays sharp) and a
  measured win where there is one. The window grows to fit the longest card instead of cutting its line off.
testers:
  - Any OS: Help > What's New: the hero band, five cards, nothing cut off; Tab and Enter still try a card; Esc closes.
  - Windows at 150% scaling and a 4K screen: the pictures and words are sharp.
  - Preferences > Interface > Language > Español: the buttons are in Spanish.
---
tools/whats_new_cards.py draws the pictures from each card's icon: key; release.sh (bump) runs `fill` for the version.
