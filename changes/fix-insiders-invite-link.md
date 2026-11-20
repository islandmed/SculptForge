---
whats_new: **Member builds**: "Open the invitations page" now opens the organization invitation, so members can accept it.
fixed: **Member builds**: the "Open the invitations page" button opened a repository invitations page that shows "page not found" for a member who is invited to the SculptForge organization on GitHub. It now opens the organization invitation page, where the invitation is accepted.
testers:
  - Any OS: Settings > Update > Member builds, sign in with a GitHub account that has a pending organization invitation; "Open the invitations page" opens the invitation, not a 404.
---
Link only; the sign-in detection of pending repository invitations is unchanged (an organization invite shows as "no access", which also offers the button).
