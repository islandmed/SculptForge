---
whats_new: **Big meshes are faster: a one-time tip**: the first time you open a scene over about 20 million
  triangles, a note along the bottom of the view says what got faster and names the two settings that help most
  on big meshes (**Preferences > Performance > Background Preview** and **View resolution**), with a button that
  opens them. It shows once and never again.
added: **Big-mesh tip**: shown once, the first time a scene holds more than about 20 million triangles (micro levels
  counted): what got faster on dense meshes and the settings that matter (Background Preview, View resolution),
  with **Performance settings** and **Got it**. Never during a stroke, a benchmark, the welcome, What's New or the
  quick tour; stored in project.json as soon as it shows, so it never comes back.
added: **What's New clips**: a feature card can play a short clip (the website's video, the app's frame sheet).
testers:
  - Any OS: open a 20M+ triangle project (or Lucy 112M): the note "Big meshes are faster now" shows at the bottom;
    **Performance settings** opens Preferences > Performance. Restart and open it again: no note.
  - Any OS: Help > What's New after updating to 0.11.1: the first card plays the side-by-side Lucy clip in a loop.
---
Numbers in the card, the site and Discord come only from the 3060 rows in perf-scoreboard REPORT.md (core200m).
