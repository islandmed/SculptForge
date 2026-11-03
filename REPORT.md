# fix/review-voidart: video review (0.10.6) + Discord Divide/Subdivide; ships after 0.10.9 (What's New "## Version 0.10.10")
- Dynamic on a Part with levels: pop-up "Freeze Levels and Turn On Dynamic" (one undo step); `subdiv_freeze`, `dyntopo freeze_levels=true`; Draw palette button.
- Strokes on the divided default sphere (Clay/Standard, size 19, X sym, Dynamic): don't reproduce on main; small strokes look clean (ui_review_fixes shot).
- Mask: soft cool tint, Mask darkness preference (Preferences > Interface, Tool > Masking); wireframe fades sub-pixel edges (no moire).
- Extract: "Mask what to extract first" hint (the buttons were greyed out with no reason). View > Views: 6 axis buttons + named saved views (Ctrl+Alt+Numpad 0 / Numpad 0), saved in the project.
- Toggles that are on: accent-tinted fill in every scheme. Quad Remesh guides: lazy-mouse smoothing, Chaikin polyline, dark outline.
- Discord: "Divide (keeps levels)" / "Subdivide (removes levels)", Micro Detail and Change the Mesh groups; Subdivide confirm + Don't ask again; Del Lower/Higher confirm.
- Not done: icon contrast audit (item 8, beyond toggles); video captions blocked by YouTube 429, so the brief's summary was used instead.
- Tests: test_multires freeze; ui_review_fixes (new); updated ui_levels_rebuild, ui_multires, ui_dyntopo, ui_language, ui_shell_layout, ui_user_guide. Full ctest: see below.
- Full ctest -j3 (heavy.sh) on the rebased tree: 128/128 passed.
