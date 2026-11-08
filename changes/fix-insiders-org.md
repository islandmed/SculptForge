---
changed: **Member builds** in the app now come from the SculptForge organization; update to keep signing in.
testers:
  - Member (signed in to Member builds): Preferences > Updates > Member builds lists the current beta; signing out and
    in again still works.
---
Repo refs, release.sh's exact-name locate check, docs. The app never follows a redirect with its token, so builds
before this one can't list member builds (the old path answers 301) until they take this release through the public
channel. The GitHub App "SculptForge Member Builds" must also be installed on the sculptforge org (owner step).
