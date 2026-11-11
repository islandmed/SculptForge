---
whats_new: **Member sign-in says what happened**: after you enter the code on GitHub, **Preferences > Updates > Member builds** now tells you the result: signed in with member builds on, signed in but not on the member list yet (or an invitation still waiting, with a button to the invitations page), declined, or the code ran out.
fixed: **Member builds** (Preferences > Updates): the GitHub sign-in always ends with a visible message, including an account that is not yet on the member list or has an invitation to accept, a declined sign-in and an expired code (reported by a member). The sign-in's polling also retries a GitHub 5xx or rate limit instead of stopping. The device flow is now one tested function (all outcomes, `slow_down`, expiry, cancel).
testers:
  - Any OS: Preferences > Updates > Sign in with GitHub with an account that is not on the member list; after entering the code the line "Signed in as <you>, but this account isn't on the member list yet..." appears.
  - With a pending invitation to the member repository: the pending message and "Open the invitations page".
  - Cancel the code on GitHub: "Sign-in declined"; let the code expire: "The code ran out".
---
