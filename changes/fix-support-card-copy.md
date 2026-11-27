---
changed: The membership card no longer says early builds are "like SculptForge for Android today" (Android is free for everyone). Its Member perks now read what members get today: one Member Request a month built first (most ship within days), the monthly feature vote, the members' channel on Discord and your name in the credits. The Become a member button opens the membership page (both prices, the terms) instead of the checkout, and the small update card says the same. Translated in all interface languages.
testers:
  - Preferences > Support > Show the memberships: the Member card lists four perks, none about early builds; Become a member opens sculptforge.app/support.html in the browser.
  - Switch the language to Japanese, Chinese or Spanish: the perks and the button's line under the card are translated.
---
Reviewers: the card button now opens the tagged membership page (utm_campaign=app-card, which the page turns into the
checkout's client_reference_id); the "or $5 a month" footer link is gone (the page has the Monthly toggle). Left for a
separate pass: "early builds, like SculptForge for Android today" still in Preferences > Updates, Quick Tour, welcome,
guide (docs/guide/support.md, welcome.md, quick-tour.md) and the Config.cpp help, plus tests/ui/betas_wording, update_check,
quick_tour, support.sfui and tools/ci/check_betas_paused.py.
