# feat/first-sculpt: "Your first sculpt in 10 minutes" (re-shot after review, rides 0.10.8)
- Lesson (docs/guide/first-sculpt.md, 12 steps): Mask Rectangle + big Move for the walls, colour Fill through the mask (crisp red/cream edge), Blur + Move for an overhanging cap, Inflate, **Divide twice**, Crease + Alt-Clay door, Clay framed windows, Inflate raised spots, hard-edged Paint brush (Falloff -70) for white spots, wooden door, yellow windows, White + **PBR** shading, Save Render (2210x1548 at 1600x960). ZBrush-style layout notes; What's next links.
- Shots: tests/ui/first_sculpt.sfui (real strokes, default look) -> `make_images.py --first-sculpt` -> docs/guide/images/first-sculpt/ (14 JPEGs). ctest: ui_first_sculpt (also Help menu item + welcome button).
- In-app: welcome "Start the 10-minute lesson"; Help > Your first sculpt (10 min); Render > Materials > Save Render.
- Site: homepage hero link, guide index entry, guide rebuilt. Guide images may live in images/<chapter>/.
- CHANGELOG [Unreleased] + whats-new `## Version 0.10.8` (whats_new.sfui checks it). Rebased as one commit on main b2a1896.
- Tests: full ctest -j3 green (118/118 run, 1 disabled as before), incl. ui_first_sculpt, ui_whats_new (now checks 0.10.8), guide_site.
