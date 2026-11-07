---
changed: Website: **How requests work** on the membership page (sculptforge.app/support.html#requests): bugs are always
  free; every idea goes in a public queue; members' requests are built first, one guaranteed request a month each
  (built and credited); Founding Members and Studio go first; a monthly member vote sets the order; popular ideas from
  everyone still get built. A "Built for members" list shows what member requests became.
added: Website: **Feature queue** (sculptforge.app/queue.html, footer link on the membership page): every feature idea
  with its status (building, queued, shipped, already in the app, declined) and a member flag.
changed: Website: Studio membership now lists your own private repository and build (a branch with the features and
  fixes you ask for, rebased onto every release), a private Discord space for your team and a seat on the Studio
  steering committee (one representative per studio).
---
queue.html is rendered by ~/Work/.sf/feedback/render_queue.py <worktree> from ~/Work/.sf/feedback/feature-queue.md
(public fields only: idea, status word, member flag; no names, links or agent names). sfmonitor re-runs it on change.
