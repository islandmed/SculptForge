---
whats_new: **Help > Connect an AI tool** is now the one guide to working with an AI tool, on one screen: pick your
  tool and copy its command, turn on **Let AI tools control this window** right there, see what happens next (the
  6-digit code, the **AI connected** badge, **AI:** steps in Undo, what asks you first), and copy an example prompt.
  The AI blocks out a starting point; you do the art.
highlight: Connect an AI tool in three steps
changed: **Help > Connect an AI tool** is the in-app guide to live mode: 1. pick your tool (Claude Code, Claude
  Desktop, Cursor, or any other in the list) with the exact command or settings for this install, now for the
  live-mode server (`sculptforge-cli --mcp --attach`, listed as `sculptforge-live` beside a headless `sculptforge`
  entry); 2. the live-mode switch; 3. what happens next; four example prompts with Copy. The plan notes moved to a
  **Plans and sign-in** tooltip; the headless server and the Claude Desktop bundle are in the user guide.
changed: The live-mode dialogs (**Let an AI tool control this window?**, **Allow this AI step?**) look like the
  app's other dialogs: one text width, two buttons of one width (the answer first), no close box (Deny or Escape),
  the code in a large font instead of a scaled-up small one. While the approval is open the Connect window steps
  aside, so two dialogs never stack; after Approve it closes and the badge takes over.
testers:
  - Any OS, 100% and 200% UI scale: Help > Connect an AI tool fits on screen; each tool's Copy pastes a command that
    works (Claude Code: claude mcp list shows sculptforge-live).
  - Turn on step 2, ask your AI tool "Block out a creature head with horns for me to sculpt": the approval shows
    alone, the code is sharp, Approve connects and the badge shows at the bottom right.
---
Reviewers: before/after screenshots in REPORT.md.
