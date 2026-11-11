---
changed: The membership links in the app (Become a member, the yearly Member checkout, Founding Studio) carry a source
  tag, `client_reference_id=app-card`, so a checkout started in the app is attributed to it. Nothing else is sent; no
  tracking, no new preferences.
---
The app only opens the three fixed Stripe links in the browser (`supportLinkAllowed` accepts exactly the tagged URLs and refuses
untagged, other tags, extra params). The site's links stay untagged in the HTML (motion.js tags them at runtime), so test_support
compares them without the query.
