---
added: Website: **Jobs** (sculptforge.app/jobs.html, footer link): open sculpting and 3D modeling roles worldwide (games,
  film and VFX, animation, collectibles, 3D printing, jewelry and dental CAD, AR/VR) from the companies' own public
  job feeds, filtered by region, sector, role, Remote and contract. A daily GitHub Action (tools/jobs/fetch.py,
  sources in tools/jobs/studios.yaml) refreshes it; titles, companies and places only, never job descriptions.
---
Sources are official embed feeds only (Greenhouse, Lever, Ashby, Recruitee, Personio, Teamtailor); robots.txt honoured,
one request per feed per day, a failing feed keeps its last good roles. SmartRecruiters is out (its robots.txt allows
LinkedIn's crawler only). Data lives on the jobs-data branch; pages.yml fills jobs.html from it after each run.
