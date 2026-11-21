---
changed: **Membership wording**: the Support help text, the Quick Tour, Preferences > Updates, the guide and the README say members get "early builds, like SculptForge for Android today", no longer betas (member betas are paused). **Edit > Preferences > Updates**: the update channel reads **Preview (early builds)** (was Preview (betas)), and its help says plainly that it gets the same releases as Stable for now; your choice is kept. The Spanish, Chinese and Japanese texts follow.
testers:
  - Any OS: Help > Quick Tour, last page; Edit > Preferences > Updates; the Support SculptForge tooltip in the Help menu. No text promises betas.
---
A wording sweep, plus tools/ci/check_betas_paused.py (ctest betas_paused) so a promise of betas can't come back unnoticed.
