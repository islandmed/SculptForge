# fix/member-page-clean

**What:** the lion is gone from the membership page (website/support.html, where donate.html redirects), the studios
page (studios.html) and the in-app membership card. In its place is a calm header: type, a warm orange glow and a faint grid.
On the site it follows light and dark mode, it is centred on desktop, and on phones the words come first.
The intro text now shows on phones too (a global rule used to hide it). The light-mode accent and buttons now have
proper contrast. The 6 support-hero media files and assets/branding/support-card.jpg (embedded in the app) are deleted.
og:image was already social-preview.png, not the lion, so it is unchanged. The lion sculpt in demos, the guide and the press kit is
untouched (it's product footage, not the membership hero). There is no memberships image in What's New, so nothing changed there.

**Add-on (vote perk):** "Vote on what we build next" is on all paid tiers: Supporter (on Discord), Insider (and
propose the options), Studio (for every artist). It is also in the comparison table, plus a line: "Anyone can suggest a
feature in #ideas on Discord; members' votes in #feature-vote pick what ships". The in-app card's Supporter perk reads
"Vote on what we build next (#feature-vote)", and the Support panel, the reminder and the Help menu tooltip use the same wording.

**Tests:** C++ touched (PanelSupportCard.cpp, App.cpp/h, CMake embed), so I ran only the affected tests: ui_support and
ui_support_card both pass (the support.sfui expected text was updated). The app builds clean.

**SHA:** 764c3602

**Screenshots** (looked at all of them): /home/tony/Work/Zbrush/.claude/worktrees/member-page/build/review-shots/
- support-desktop-{dark,light}.png, support-phone-{dark,light}.png (390 px)
- studios-desktop-{dark,light}.png, studios-phone-{dark,light}.png
- first_run.png, updated.png (the in-app card, Classic look)
