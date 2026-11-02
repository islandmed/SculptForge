# site/membership

- website/support.html (title "Membership") replaces donate.html; donate.html is a redirect (meta refresh + JS) keeping the query string, so the Stripe success URL donate.html?thanks=1 still works (checked in headless Chromium).
- Tiers are presented as memberships with their benefits; there is a One-time Supporter option (pay what you want, $5 minimum); the tax-deductible line is replaced with "Memberships are subscriptions to SculptForge benefits; cancel anytime in the customer portal".
- All site footers point to support.html with the label "Membership". Also updated: the index, studios, the guide (welcome, What's New), README nav, the sitemap, the in-app Help > Support, welcome, reminder and launch card, and the tests.
- Tests: build OK; ctest -j3 had 2 UI tests fail under load (ui_ctrl_brushes, ui_dyntopo); both pass on rerun.
- No .github/FUNDING.yml exists (not created). No Spanish strings contained donation wording.
- Commit: b3a8bc8
