---
whats_new: **Membership links say where they came from**: the in-app links to the membership page now carry a small source tag in the address, so we can see which one helps.
changed: In-app membership links (Help > Support SculptForge, the top-bar heart, the update notice, the membership card, the quick tour, What's New, About) open the membership page with `?utm_source=app&utm_medium=in-app&utm_campaign=<where>`. Only the address the browser opens changes: no new request, counter or prompt.
testers:
  - Any OS: click the top-bar heart and Help > Support SculptForge; the browser opens sculptforge.app/support.html with utm_campaign=topbar-heart and utm_campaign=help-menu.
---
Tests: tests/test_support.cpp (supportPageUrl), UI tests per entry point (support, support_moments, member_card, quick_tour, update_check).
