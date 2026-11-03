# feat/precision-brushes (0.10.9): rebased on 0.10.8, NOT pushed: full ctest killed (machine out of memory)
Done (commit 619a727): Steady Stroke on every stroke type (L) + catch-up; World Size (mm); 0.5 px min, Shift+[ ] 1 px,
Ctrl+Shift+right-drag scrub (Ctrl+Shift+drag is hide-select); Shift+Alt mid-stroke precision drag; per-brush 2-5 point
pressure curve (own brush-curves.json, #78 keys); exact screenRay/view + canvas snapped to fb pixels; pick filter; docs.
Passing: unit tests precision+camera; ui/precision.sfui; ui/precision_dense.sfui (HiDPI 2x: worst dab 0.0005 px).
Known: float floor ~0.2 px at a 1.3 mm-wide view (fine to 0.05 px down to 6 mm); fold curve into #78 struct after merge.
Left: full ctest (killed by low-memory reaper, not a failure), 33M check, push, message "input".
TESTERS.md written; no REPLY.md (no user issue).
Resume: pgrep -f ctest (none) -> cd build && nice -n 19 ionice -c3 cmake --build . -j3 && SDL_APP_ID=sculptforge-agent
  nice -n 19 ionice -c3 ctest -j3 --output-on-failure; fix (likely ui_guide_shortcuts: regenerate shortcut table);
  at load < 2: tools/precision_check.sh build; then git push -u origin feat/precision-brushes, update SHA here.
