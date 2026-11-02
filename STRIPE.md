# Stripe product descriptions (memberships v2)

For the coordinator: paste each description into its product in the Stripe dashboard (Product catalog > product >
Edit > Description). Prices are unchanged except Studio, now **$300 / month**
(https://buy.stripe.com/9B66oH8uXd9w16j2254F205, in `src/app/Support.h` and `website/studio-offer.js`). The $150
link (`.../3cI00j12v5H4g1dgWZ4F204`) is Founding Studio (its description is already updated). When the 10
founding slots are taken, set `kFoundingStudioOpen = false` (Support.h) and `FOUNDING_STUDIO_OPEN = false`
(studio-offer.js), and deactivate the founding payment link (existing founding subscriptions keep their price).

Each description is under 350 characters (checked with `wc -m`).

## Supporter, $5 / month (link .../aFacN5h1t6L8cP14ad4F201)

```
Your name in the SculptForge credits, in the app and on sculptforge.app; the #members channel on Discord; a vote in the monthly feature vote; and the monthly pack of brushes and alphas at month end. SculptForge stays free and nothing in it is ever paywalled. Cancel anytime.
```

## Insider, $20 / month (link .../dRmdR94eH4D06qD9ux4F202)

```
Everything in Supporter, plus: propose options for the monthly vote, a daily drop (a brush, alpha or VDM almost every day), preview builds of the next release about a week early, and the monthly dev Q&A. Every release stays free for everyone; bug-fix and security releases go out to everyone the same day.
```

## Studio, $300 / month (link .../9B66oH8uXd9w16j2254F205)

```
For your whole company, no seat count: Insider for every artist, a custom studio build with your presets, keymaps and pipeline defaults, up to 5 custom items a month, guided development, priority triage (reviewed within 2 business days; no fix-time guarantee), a team channel and your logo. Larger custom features are quoted separately.
```

## Founding Studio, $150 / month (link .../3cI00j12v5H4g1dgWZ4F204; already pasted)

```
Founding Studio, first 10 studios, price locked while subscribed. Studio for your whole company: Insider for every artist, a custom build, up to 5 custom items a month, guided development, priority triage (reviewed within 2 business days; no fix-time guarantee), a team channel and your logo. Larger custom features are quoted separately.
```

## One-time Supporter, pay what you want, $5 minimum (link .../4gM7sL8uXc5s4iv5eh4F200)

```
A one-time gift to SculptForge, with your name in the credits in the app and on sculptforge.app if you like. No subscription. SculptForge stays free and nothing in it is ever paywalled. Thank you.
```
