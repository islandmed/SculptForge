---
whats_new: **What artists say**: **Help > About** shows what members say about SculptForge, in their own words, and
  the membership card shows one of them.
added: **What artists say** in **Help > About** and on the small membership card (one quote, a different one each time
  it shows): members' own words, shown exactly as they approved them, from the same list as the website.
testers:
  - Any OS: Help > About; "What artists say" lists Richard Throgmorton (MakIt3D)'s quote in full, above Founding Members.
---
The app compiles docs/testimonials-app.json, which release.sh refreshes from website/testimonials.json (site lane, live
at once) at each release; tests/test_support.cpp checks the app copy is a subset of the site file.
