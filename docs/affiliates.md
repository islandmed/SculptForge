# Creator referrals

For creators who send people to SculptForge memberships (not linked from the site menu; we send it to partners).

## How it works

- You get a code (for example `asknk`) and a link: `https://sculptforge.app/support.html?ref=<code>`. The code
  works on any sculptforge.app page.
- The browser remembers your code for 30 days, so it still counts if someone looks around first and joins later.
  It is kept only in that browser (no cookies, no tracking service) and passed to Stripe when they check out a
  yearly membership.
- If someone opens another creator's link later, the newest code counts.

## What you get

- **15% of the first year** of every **yearly** membership bought through your link: for example $7.20 for a
  $48 yearly Member plan. Monthly plans don't count, renewals after the first year don't count, and refunded
  payments don't count.
- A **statement every month**: the yearly sign-ups that month, the total and your 15%.
- **Paid monthly** by the project, for the previous month's sign-ups, after the statement.

## What we ask

- **No obligation to review positively.** Say what you really think; criticism helps us.
- **Disclose** the referral link the way your platform requires (for example YouTube's paid promotion setting,
  or "affiliate link" in the description).
- Don't use the link in paid ads on our name, and don't promise perks that aren't on the membership page.

Either side can stop at any time; payments already earned are still paid. Questions: ask us on Discord or by email.

## For us

- Codes: 1-32 characters, `a-z 0-9 _ -`. Active: `asknk` (askNK, from 2026-11-06).
- Yearly only: `website/ref.js` tags only the links in its `YEARLY` list (Member $48 / year today) and
  `report.py` counts only yearly payments (`YEARLY_PRICES`). A new yearly plan goes in both.
- Statement: `tools/affiliate/report.py --month YYYY-MM --csv <Stripe payments export>`, or with `STRIPE_KEY` (a
  restricted read-only key, in the environment only) set, from the API.
