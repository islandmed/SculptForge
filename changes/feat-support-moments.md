---
whats_new: **Membership moments**: the small membership card can now also come at a milestone (your 10th start,
  10 hours of sculpting, your 100th save; counted on your computer only, never sent) and says the true number. After
  a third **Not now** it waits a month, after a sixth it only comes with an update. A quiet heart at the right of the
  top bar opens **Help > Support SculptForge...**; members see a small badge there instead.
whats_new: **Requested by a member**: changes members asked for through Member Requests are marked in What's New.
changed: Membership card: milestone moments (10th start, 10 hours of stroke time, 100th save; local counts only,
  nothing sent), an optional honest line from the update feed's `support` block (funded this month, Founding Members
  taken, member requests this release), and a backoff (monthly after the 3rd "Not now", only after an update after
  the 6th). Every existing rule stays: never modal, never mid-stroke or during a job, never in tests, renders or CI,
  never for members, at most once a week.
added: Top bar heart (Help > Support SculptForge); a small badge for verified members. Hidden in studio builds and
  before it would crowd a narrow top bar.
added: Note fragments take `member_request: true` (and `member_credit: @handle` when the member opted in): What's New
  tags the change "Requested by a member".
testers:
  - Any OS, 1440 px wide window: the heart sits left of the 1 2 3 presets in the top bar (Classic and SculptForge
    looks); hover shows a tooltip, click opens the membership page. At 1024 px in the SculptForge look it is gone.
  - Signed in for member builds: the heart is a small badge, no card ever shows.
---
Brief: BRIEF.md (Tony 11-07: "nag people who repeatedly open the app to support us, not in an annoying way").
