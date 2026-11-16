---
whats_new: **Membership links say where they came from**: the in-app links to the membership page now carry a small source tag in the address, so we can see which one helps.
changed: In-app membership links (Help > Support SculptForge, the top-bar heart, the update notice, the membership card, the quick tour, What's New, About) open the membership page with `?utm_source=app&utm_medium=in-app&utm_campaign=<where>`. Only the address the browser opens changes: no new request, counter or prompt.
whats_new: **A short note from the members**: once per version, from your second start, a small card says SculptForge runs on its members; Not now closes it. Members never see it.
added: **Appeal card**: a short, once-per-version note asks people who enjoy SculptForge to become members; "Not now" closes it. It shows at startup from the second start on, never to members or Studio builds, with update checks off, or in tests and renders; no counters or telemetry. Its link is tagged utm_campaign=in-app-appeal.
testers:
  - Any OS, second start of a new version: the card shows at the bottom with the SculptForge bust; Not now closes it and it stays gone until the next version; Become a member opens the membership page in the browser.
  - Any OS: click the top-bar heart and Help > Support SculptForge; the browser opens sculptforge.app/support.html with utm_campaign=topbar-heart and utm_campaign=help-menu.
---
Tests: tests/test_support.cpp (supportPageUrl), UI tests per entry point (support, support_moments, member_card, quick_tour, update_check).
