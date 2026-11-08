---
changed: Website: the Membership page thanks every Founding Member by their credit name (the same list as the Members
  page), and `build_site.py --supporters` rewrites that line with the list, so a newly credited founder shows up in both.
---
The line comes from members-live.json `names` (else founding.json): opt-in credit names only. Past 10 names it ends "and N more". `--check` fails if the line is missing or stale.
