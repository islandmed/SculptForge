# feat/whats-new-ui: What's New as a window with feature cards; guide + website condensed

- **What:** Help > What's New / first start after an update opens a themed 920x660 card (cube mark, title, tagline, hero card + row, Try it per card, release notes / Discord / Close; Esc, Tab, Enter; UI-scaled, fades in). Data: a ```features block per version in docs/guide/whats-new.md (core/WhatsNew + build_site.py). Guide chapter and website: newest 3 versions in full (accent-rule band, cards, "All changes" folded but the newest), older ones as groups (0.10.1 to 0.10.4, 0.9.x, 0.8 and earlier, 3-6 highlights each); sidebar lists those 6. The window slides automatically each release.
- **Images:** tools/guide/whats_new_shots.sfui + make_images.py --whats-new, 600x340.
- **Tests:** full ctest -j3: 115/117 passed; ui_bake and ui_pen_windows_ink failed under load and pass on rerun (timing; unrelated). New: ui_whats_new, guide_whats_new_*, guide_site checks features blocks. Then 15 related tests green after the last fix.
- **For the coordinator:** 0.10.8 is seeded ("date: unreleased"; release.sh now stamps the date) with one card; it stays hidden in builds until the version bump. ui_whats_new pins the popup to 0.10.7 (SF_WHATS_NEW_VERSION), so it survives bumps.
- **Screenshots:** before ~/Work/look-mockups/whats-new/before.png; after: after-popup-classic.png, after-popup-forge-terminal.png, after-guide-in-app.png, after-guide-in-app-groups.png, after-web-desktop.png, after-web-phone.png (same folder).
- **SHA:** 4477b79 (branch feat/whats-new-ui, rebased on origin/main e55d3e0).

## Status (stopped 11-01 for usage)
- **Status:** done and pushed, green; waiting for the coordinator to merge into 0.10.8. No uncommitted work.
- **Left:** merge; at the 0.10.8 release add `expect_text "Version 0.10.8"` to tests/ui/whats_new.sfui (release.sh checks it); optionally more 0.10.8 cards as features land (rerun tools/guide/whats_new_shots.sfui + make_images.py --whats-new for pictures).
- **Resume:** `cd ~/Work/Zbrush/.claude/worktrees/whats-new-ui && git fetch && git rebase origin/main`; if it conflicts, fix, then one full ctest (`pgrep -f ctest` first; nice -n 19 ionice -c3, -j3) and push. After merge: `git worktree remove` this worktree.
