# feat/precision-brushes (0.10.9): green, pushed
Steady Stroke (L) on every stroke type + catch-up; World Size (mm); 0.5 px min, Shift+[ ] 1 px, Ctrl+Shift+right-drag
scrub (Ctrl+Shift+drag is hide-select); Shift+Alt mid-stroke precision drag (all input styles); per-brush 2-5 point
pressure curve (brush-curves.json, #78 keys); exact screenRay/view, canvas on fb pixels, pick filter; docs, TESTERS.md.
Tests: full ctest 129/129 (zbrush_feel's blur check now counts partly-masked vertices; rerun green), test_precision,
ui/precision, ui/precision_dense (extreme zoom, HiDPI 2x: worst dab 0.0005 px). No REPLY.md (no user issue).
Not done: 33M by hand (tools/precision_check.sh, at load < 2). Float floor ~0.2 px at a 1.3 mm-wide view.
Follow-up: move the curve into #78's BrushToolSettings.
